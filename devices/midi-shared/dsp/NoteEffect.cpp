#include "NoteEffect.h"

#include <algorithm>
#include <fd/music/Following.h>

namespace fd::devices
{
// --- Schedule ------------------------------------------------------------------------------------

bool Schedule::add(std::int64_t at, const Event& e, std::int32_t source) noexcept
{
    if (size_ >= items_.size())
        return false;
    items_[size_++] = {.at = at, .event = e, .source = source, .order = order_++};
    return true;
}

std::size_t Schedule::nextIndex() const noexcept
{
    std::size_t best = 0;
    const auto before = [](const Item& a, const Item& b)
    {
        if (a.at != b.at)
            return a.at < b.at;
        const bool aOff = a.event.type == EventType::NoteOff, bOff = b.event.type == EventType::NoteOff;
        if (aOff != bOff)
            return aOff;
        return a.order < b.order;
    };
    for (std::size_t i = 1; i < size_; ++i)
        if (before(items_[i], items_[best]))
            best = i;
    return best;
}

const Schedule::Item* Schedule::next() const noexcept
{
    return size_ == 0 ? nullptr : &items_[nextIndex()];
}

Schedule::Item Schedule::take() noexcept
{
    const auto i = nextIndex();
    const Item item = items_[i];
    items_[i] = items_[--size_];
    return item;
}

void Schedule::dropStarts(std::int32_t source) noexcept
{
    for (std::size_t i = 0; i < size_;)
        if (items_[i].source == source && items_[i].event.type == EventType::NoteOn)
            items_[i] = items_[--size_];
        else
            ++i;
}

bool Schedule::dropStart(std::int32_t source, std::uint8_t key) noexcept
{
    for (std::size_t i = 0; i < size_; ++i)
        if (items_[i].source == source && items_[i].event.type == EventType::NoteOn &&
            items_[i].event.key == key)
        {
            items_[i] = items_[--size_];
            return true;
        }
    return false;
}

// --- Moments -------------------------------------------------------------------------------------

Moments::Moments(const ProcessContext& c, const BlockBeats& beats, Follower& follower) noexcept
    : events_(c.events), listen_(c.listen), follower_(follower)
{
    changeCount_ = follower.songChanges(beats, changes_);
}

bool Moments::played(const Event& e) const noexcept
{
    // Follow on Played: the notes below the split set the chord and aren't played.
    return follower_.follow() == 2 && (e.type == EventType::NoteOn || e.type == EventType::NoteOff) &&
           follower_.takesKey(e.key);
}

int Moments::next() const noexcept
{
    int at = INT_MAX;
    if (events_ != nullptr && event_ < events_->size())
        at = std::min(at, (*events_)[event_].sampleOffset);
    if (listen_ != nullptr && heard_ < listen_->size())
        at = std::min(at, (*listen_)[heard_].sampleOffset);
    if (change_ < changeCount_)
        at = std::min(at, changes_[change_]);
    return at;
}

Moments::Moment Moments::take() noexcept
{
    const int at = next();
    if (at == INT_MAX)
        return {};
    if (settled_ != at)
    {
        // This sample's followed notes first: the listened track's, those below the split.
        settled_ = at;
        bool changed = false;
        if (listen_ != nullptr)
            while (heard_ < listen_->size() && (*listen_)[heard_].sampleOffset <= at)
                follower_.hear((*listen_)[heard_++]);
        if (events_ != nullptr)
            for (std::size_t i = event_; i < events_->size() && (*events_)[i].sampleOffset <= at; ++i)
                if (played((*events_)[i]))
                    follower_.takePlayed((*events_)[i]);
        changed = follower_.settle();
        while (change_ < changeCount_ && changes_[change_] <= at)
        {
            ++change_;
            changed = true;
        }
        if (changed)
            return {.kind = Kind::Change, .offset = at};
    }
    while (events_ != nullptr && event_ < events_->size() && (*events_)[event_].sampleOffset <= at)
    {
        const auto& e = (*events_)[event_++];
        if (e.type == EventType::AllNotesOff)
            follower_.endPlayed();
        if (!played(e))
            return {.kind = Kind::Input, .offset = at, .event = e};
    }
    return {.kind = Kind::Nothing, .offset = at};
}

// --- NoteEffect ----------------------------------------------------------------------------------

void NoteEffect::prepare(const ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    reset();
}

void NoteEffect::reset() noexcept
{
    ledger_.clear();
    owners_.fill(0);
    schedule_.clear();
    clock_ = blockClock_ = 0;
    ownBeat_ = 0.0;
    wasPlaying_ = false;
    roll_ = 1;
    selfRolls_ = 0;
    clearNotes();
}

void NoteEffect::startPass(std::uint64_t roll) noexcept
{
    hostRoll_.store(roll, std::memory_order_relaxed);
    hostRolled_.store(true, std::memory_order_release);
}

void NoteEffect::process(ProcessContext& c) noexcept
{
    if (c.outEvents == nullptr)
        return;
    const auto& t = c.transport;
    if (t.playing && !wasPlaying_)
    {
        // A new pass: the host's roll, or one of its own (ADR 0062: with no host calling
        // startPass, a device rolls by itself each time playback starts).
        if (hostRolled_.exchange(false, std::memory_order_acquire))
            roll_ = hostRoll_.load(std::memory_order_relaxed);
        else
            roll_ = music::mix(1, ++selfRolls_);
    }
    wasPlaying_ = t.playing;

    // The song's beats while it plays; its own, running on at the song's tempo, while it doesn't.
    // A host reporting no tempo (some plugin hosts, stopped) plays at 120.
    BlockBeats b;
    b.perSample = (t.tempoBpm > 1.0 ? t.tempoBpm : 120.0) / 60.0 / sampleRate_;
    b.numSamples = c.numSamples;
    b.first = t.playing ? t.ppqPosition : ownBeat_;
    ownBeat_ = b.end();

    blockClock_ = clock_;
    processNotes(c, b);
    clock_ += c.numSamples;
}

void NoteEffect::startNote(ProcessContext& c, const Event& on) noexcept
{
    auto& owners = owners_[slot(on.channel, on.key)];
    if (owners < 255)
        ++owners;
    Event e = on;
    e.type = EventType::NoteOn;
    e.value = std::clamp(e.value, 1.0f / 127.0f, 1.0f);
    ledger_.write(*c.outEvents, e);
}

void NoteEffect::stopNote(ProcessContext& c, int offset, std::uint8_t channel, std::uint8_t key) noexcept
{
    auto& owners = owners_[slot(channel, key)];
    if (owners == 0 || --owners > 0)
        return;
    // As often as it was started, so a synth stacking the same note ends each.
    const Event off{.sampleOffset = offset, .type = EventType::NoteOff, .channel = channel, .key = key};
    while (ledger_.sounding(channel, key))
        if (!ledger_.write(*c.outEvents, off))
            break;
}

void NoteEffect::endAll(ProcessContext& c, int offset) noexcept
{
    ledger_.endAll(*c.outEvents, offset);
    owners_.fill(0);
    schedule_.clear();
}

bool NoteEffect::passOn(ProcessContext& c, const Event& e) noexcept
{
    switch (e.type)
    {
    case EventType::NoteOn:
    case EventType::NoteOff:
    case EventType::PolyPressure:
        return false;
    case EventType::AllNotesOff:
        endAll(c, e.sampleOffset);
        clearNotes();
        break;
    case EventType::Controller:
    case EventType::PitchBend:
    case EventType::ChannelPressure:
    case EventType::AllSoundOff:
        break;
    }
    c.outEvents->push(e);
    return true;
}

void NoteEffect::playScheduled(ProcessContext& c, int offset, bool offsOnly) noexcept
{
    while (const auto* item = schedule_.next())
    {
        const auto at = offsetOf(item->at);
        if (at > offset || (offsOnly && item->event.type != EventType::NoteOff))
            return;
        auto e = schedule_.take().event;
        e.sampleOffset = static_cast<int>(std::max<std::int64_t>(0, at));
        if (e.type == EventType::NoteOff)
            stopNote(c, e.sampleOffset, e.channel, e.key);
        else
            startNote(c, e);
    }
}

int NoteEffect::nextScheduled(int numSamples, bool& isOff) const noexcept
{
    const auto* item = schedule_.next();
    if (item == nullptr || offsetOf(item->at) >= numSamples)
        return INT_MAX;
    isOff = item->event.type == EventType::NoteOff;
    return static_cast<int>(std::max<std::int64_t>(0, offsetOf(item->at)));
}
} // namespace fd::devices
