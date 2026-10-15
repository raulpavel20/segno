#include "engine/ClipChance.h"

#include "engine/ClipNotes.h"
#include "model/Chance.h"

#include <cmath>
#include <deque>
#include <map>
#include <mutex>

namespace fd::engine
{
namespace te = tracktion;

namespace
{
/// The Edits' passes and their clips' last rolls, held by whoever uses them (the Session, the
/// filters): an entry outlives its Edit only while something still holds it.
struct Registry
{
    std::mutex lock;
    std::map<const te::Edit*, std::weak_ptr<ChancePass>> passes;
    std::map<std::pair<const te::Edit*, std::uint64_t>, std::weak_ptr<ChanceHeard>> heard;
};

Registry& registry()
{
    static Registry r;
    return r;
}

std::uint64_t freshRoll()
{
    std::uint64_t r = 0;
    while (r == 0)
        r = static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64());
    return r;
}

/// Decides a clip's notes left to chance as its node plays them. Each event of the node's sequence
/// is known by its index: a note-on by the note it plays (where in the clip's content and on which
/// of its laps), a note-off by its note-on.
class ChanceFilter final : public te::MidiPlaybackFilter
{
public:
    struct Event
    {
        bool decides = false; // a note-on left to chance
        model::Note note;
        double place = 0.0;
        int lap = 0;
        int noteOn = -1; // a note-off: its note-on's index
    };

    ChanceFilter(std::vector<Event> events, std::uint32_t seed, std::uint64_t clip,
                 std::optional<KeptChance> kept, std::shared_ptr<ChancePass> pass,
                 std::shared_ptr<ChanceHeard> heard)
        : events_(std::move(events)), plays_(events_.size(), 1), seed_(seed), clip_(clip), kept_(kept),
          pass_(std::move(pass)), heard_(std::move(heard))
    {
        decideSeed();
    }

    void startBlock(double editTime, bool isPlaying) noexcept override
    {
        if (const auto roll = pass_->roll.load(std::memory_order_relaxed); roll != roll_)
        {
            roll_ = roll; // a new pass
            lap_ = 0;
        }
        else if (isPlaying && editTime < lastTime_ - 1.0e-6)
            ++lap_; // the loop came round (or the song jumped back): fresh dice
        lastTime_ = editTime;
        decideSeed();
        if (isPlaying && !kept_)
        {
            heard_->roll.store(roll_, std::memory_order_relaxed);
            heard_->lap.store(lap_, std::memory_order_relaxed);
            heard_->heard.store(true, std::memory_order_relaxed);
        }
    }

    bool shouldPlay(int index, juce::MidiMessage& m) noexcept override
    {
        if (index < 0 || static_cast<size_t>(index) >= events_.size())
            return true;
        const auto& e = events_[static_cast<size_t>(index)];
        if (m.isNoteOn())
        {
            if (!e.decides)
                return true;
            const auto fate = model::noteFate(e.note, e.place, e.lap, seedNow_, clip_);
            plays_[static_cast<size_t>(index)] = fate.plays ? 1 : 0;
            if (!fate.plays)
                return false;
            if (e.note.velocitySpread > 0.0f)
                m.setVelocity(fate.velocity);
            return true;
        }
        if (m.isNoteOff() && e.noteOn >= 0)
            return plays_[static_cast<size_t>(e.noteOn)] != 0; // a note left out ends unheard
        return true;
    }

    bool plays(int index) const noexcept override
    {
        return index < 0 || static_cast<size_t>(index) >= plays_.size() ||
               plays_[static_cast<size_t>(index)] != 0;
    }

private:
    void decideSeed() noexcept
    {
        seedNow_ =
            kept_ ? model::passSeed(seed_, kept_->roll, kept_->lap) : model::passSeed(seed_, roll_, lap_);
    }

    std::vector<Event> events_;
    std::vector<std::uint8_t> plays_; // each note-on, as last decided
    std::uint32_t seed_ = 0, seedNow_ = 0;
    std::uint64_t clip_ = 0;
    std::optional<KeptChance> kept_;
    std::shared_ptr<ChancePass> pass_;
    std::shared_ptr<ChanceHeard> heard_;
    std::uint64_t roll_ = 0;
    int lap_ = 0;
    double lastTime_ = 0.0;
};
} // namespace

std::shared_ptr<ChancePass> chancePassOf(const te::Edit& edit)
{
    auto& r = registry();
    const std::scoped_lock hold(r.lock);
    for (auto it = r.passes.begin(); it != r.passes.end();)
        it = it->second.expired() ? r.passes.erase(it) : std::next(it);
    if (auto found = r.passes[&edit].lock())
        return found;
    auto pass = std::make_shared<ChancePass>();
    pass->roll.store(freshRoll());
    r.passes[&edit] = pass;
    return pass;
}

std::shared_ptr<ChanceHeard> chanceHeardOf(const te::Edit& edit, std::uint64_t clip)
{
    auto& r = registry();
    const std::scoped_lock hold(r.lock);
    for (auto it = r.heard.begin(); it != r.heard.end();)
        it = it->second.expired() ? r.heard.erase(it) : std::next(it);
    if (auto found = r.heard[{&edit, clip}].lock())
        return found;
    auto heard = std::make_shared<ChanceHeard>();
    r.heard[{&edit, clip}] = heard;
    return heard;
}

std::optional<KeptChance> keptChanceOf(const te::Clip& clip)
{
    if (!clip.state.hasProperty(chanceIds::keptRoll))
        return std::nullopt;
    return KeptChance{
        static_cast<std::uint64_t>(clip.state[chanceIds::keptRoll].toString().getLargeIntValue()),
        static_cast<int>(clip.state.getProperty(chanceIds::keptLap, 0))};
}

bool usesChance(const te::MidiClip& clip)
{
    std::vector<model::Note> notes;
    for (const auto* n : clip.getSequence().getNotes())
        notes.push_back(noteOf(*n));
    return model::usesChance(notes);
}

std::shared_ptr<te::MidiPlaybackFilter>
chanceFilter(te::MidiClip& clip, const juce::MidiMessageSequence& sequence, te::MidiList::TimeBase timeBase)
{
    // The clip's notes as the node's sequence was made from them (laps laid end to end for a
    // looped clip), each note's velocity standing for its place in the list so each note-on can be
    // traced back to its note: the same sequence, event for event, but for velocities.
    const auto& list = clip.getSequenceLooped();
    te::MidiList traced(list.state.createCopy(), nullptr);
    std::vector<model::Note> notes;
    for (auto* n : traced.getNotes())
        notes.push_back(noteOf(*n));
    if (!model::usesChance(notes))
        return {};
    {
        int i = 0;
        for (auto* n : traced.getNotes())
            n->setVelocity(1 + (i++ % 127), nullptr);
    }
    auto ours = te::MidiList::createDefaultPlaybackMidiSequence(traced, clip, timeBase, clip.getMPEMode());
    ours.updateMatchedPairs();
    if (ours.getNumEvents() != sequence.getNumEvents())
    {
        jassertfalse; // not the node's sequence: every note plays as written
        return {};
    }

    // Each note-on's note: those of its pitch and velocity code, in order.
    std::map<std::pair<int, int>, std::deque<size_t>> waiting;
    for (size_t i = 0; i < notes.size(); ++i)
        waiting[{notes[i].pitch, 1 + static_cast<int>(i % 127)}].push_back(i);
    for (auto& [key, queue] : waiting)
        std::stable_sort(queue.begin(), queue.end(),
                         [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });

    const bool laps = clip.isLooping();
    const double loopLength = clip.getLoopLengthBeats().inBeats(),
                 loopStart = clip.getLoopStartBeats().inBeats();
    std::vector<ChanceFilter::Event> events(static_cast<size_t>(ours.getNumEvents()));
    std::map<const juce::MidiMessageSequence::MidiEventHolder*, int> indexOf;
    for (int i = 0; i < ours.getNumEvents(); ++i)
        indexOf[ours.getEventPointer(i)] = i;
    for (int i = 0; i < ours.getNumEvents(); ++i)
    {
        const auto* meh = ours.getEventPointer(i);
        const auto& m = meh->message;
        const auto& theirs = sequence.getEventPointer(i)->message;
        if (m.isNoteOn() != theirs.isNoteOn() || m.isNoteOff() != theirs.isNoteOff() ||
            ((m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() != theirs.getNoteNumber()))
        {
            jassertfalse;
            return {};
        }
        if (!m.isNoteOn())
            continue;
        auto& queue = waiting[{m.getNoteNumber(), m.getVelocity()}];
        if (queue.empty())
            continue;
        const auto& note = notes[queue.front()];
        queue.pop_front();
        auto& e = events[static_cast<size_t>(i)];
        e.decides = note.chance < 1.0f || note.velocitySpread > 0.0f;
        e.note = note;
        e.lap = laps && loopLength > 0.0 ? static_cast<int>(std::floor(note.start / loopLength + 1e-9)) : 0;
        e.place = laps ? note.start - e.lap * loopLength + loopStart : note.start;
        if (meh->noteOffObject != nullptr)
            if (const auto off = indexOf.find(meh->noteOffObject); off != indexOf.end())
                events[static_cast<size_t>(off->second)].noteOn = i;
    }
    return std::make_shared<ChanceFilter>(std::move(events), chanceSeedOf(clip), clip.itemID.getRawID(),
                                          keptChanceOf(clip), chancePassOf(clip.edit),
                                          chanceHeardOf(clip.edit, clip.itemID.getRawID()));
}
} // namespace fd::engine
