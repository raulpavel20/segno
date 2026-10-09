// Take lanes and Capture MIDI (M2 step C5b, ADR 0031, docs/design/recording.md §6, §8): every pass
// and loop lap kept under its track; the main lane plays plain clips made from them.

#include "engine/SessionImpl.h"

#include "model/NoteEditing.h"

namespace fd::engine
{
namespace
{
const juce::Identifier kClipColourId{"fdColour"};

std::uint64_t idProperty(const juce::var& v)
{
    return static_cast<std::uint64_t>(static_cast<juce::int64>(v));
}

/// A recorded clip's laps as plain clip states (no takes inside), and whether each is complete.
struct Lap
{
    juce::ValueTree state;
    bool complete = true;
};

juce::ValueTree plain(juce::ValueTree s)
{
    if (auto takes = s.getChildWithName(te::IDs::TAKES); takes.isValid())
        s.removeChild(takes, nullptr);
    s.removeProperty(te::IDs::currentTake, nullptr);
    return s;
}

std::vector<Lap> lapsOf(te::Edit& edit, te::Clip& c)
{
    std::vector<Lap> out;
    const auto takes = c.state.getChildWithName(te::IDs::TAKES);
    if (dynamic_cast<te::WaveAudioClip*>(&c) != nullptr && takes.isValid())
    {
        // One file per lap; tracktion lists the first lap twice.
        juce::StringArray seen;
        const double length = c.getPosition().getLength().inSeconds();
        for (auto take : takes)
        {
            const auto source = take[te::IDs::source].toString();
            if (source.isEmpty() || seen.contains(source))
                continue;
            seen.add(source);
            auto s = plain(c.state.createCopy());
            s.setProperty(te::IDs::source, source, nullptr);
            const auto file = te::SourceFileReference::findFileFromString(edit, source);
            const double recorded = te::AudioFile(edit.engine, file).getLength() - static_cast<double>(s[te::IDs::offset]);
            const bool complete = recorded >= length - 0.05;
            if (!complete) // a lap stopped short: its clip ends where its recording does
                s.setProperty(te::IDs::length, std::max(0.01, recorded), nullptr);
            out.push_back({s, complete});
        }
    }
    else if (dynamic_cast<te::MidiClip*>(&c) != nullptr && takes.isValid())
    {
        for (auto sequence : takes)
            if (sequence.hasType(te::IDs::SEQUENCE))
            {
                auto s = plain(c.state.createCopy());
                s.appendChild(sequence.createCopy(), nullptr);
                out.push_back({s, true});
            }
    }
    if (out.empty())
        out.push_back({plain(c.state.createCopy()), true});
    return out;
}
} // namespace

// ---- take lanes ----------------------------------------------------------------------------

juce::ValueTree Session::Impl::takesOf(te::AudioTrack& track, juce::UndoManager* um)
{
    auto lanes = track.state.getChildWithName(takesId);
    if (!lanes.isValid() && um != nullptr)
    {
        lanes = juce::ValueTree(takesId);
        track.state.appendChild(lanes, um);
    }
    return lanes;
}

juce::ValueTree Session::Impl::takeTree(te::AudioTrack& track, std::uint64_t take)
{
    for (auto t : track.state.getChildWithName(takesId))
        if (te::EditItemID::fromID(t).getRawID() == take)
            return t;
    return {};
}

model::ClipInfo Session::Impl::storedClipInfo(const juce::ValueTree& tree, int colour) const
{
    model::ClipInfo info;
    info.id = te::EditItemID::fromID(tree).getRawID();
    info.kind = tree.hasType(te::IDs::MIDICLIP) ? model::ClipInfo::Kind::Midi : model::ClipInfo::Kind::Audio;
    info.name = tree[te::IDs::name].toString().toStdString();
    info.start = tree[te::IDs::start];
    info.length = tree[te::IDs::length];
    info.offset = tree[te::IDs::offset];
    info.colour = static_cast<int>(tree.getProperty(kClipColourId, colour));
    if (info.kind == model::ClipInfo::Kind::Midi)
    {
        info.contentStartBeat = edit->tempoSequence.toBeats(te::TimePosition::fromSeconds(info.start - info.offset)).inBeats();
        for (auto note : tree.getChildWithName(te::IDs::SEQUENCE))
            if (note.hasType(te::IDs::NOTE))
                info.notes.push_back({static_cast<int>(note[te::IDs::p]), static_cast<double>(note[te::IDs::b]),
                                      static_cast<double>(note[te::IDs::l]), static_cast<float>(note[te::IDs::v]) / 127.0f});
        std::sort(info.notes.begin(), info.notes.end(), model::noteOrder);
    }
    else
    {
        const auto audio = te::SourceFileReference::findFileFromString(*edit, tree[te::IDs::source].toString());
        info.sourcePath = audio.getFullPathName().toStdString();
        info.sourceLength = te::AudioFile(engine, audio).getLength();
    }
    return info;
}

std::vector<model::TakeInfo> Session::Impl::takeInfos(te::AudioTrack& track, int colour) const
{
    std::vector<model::TakeInfo> out;
    for (auto t : track.state.getChildWithName(takesId))
    {
        model::TakeInfo info{.id = te::EditItemID::fromID(t).getRawID(),
                             .name = t[te::IDs::name].toString().toStdString(),
                             .star = t[starId]};
        for (auto c : t)
            info.clips.push_back(storedClipInfo(c, colour));
        std::sort(info.clips.begin(), info.clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        out.push_back(std::move(info));
    }
    return out;
}

juce::ValueTree Session::Impl::addTake(te::AudioTrack& track, const juce::String& name, const std::vector<juce::ValueTree>& clips,
                                       juce::UndoManager* um)
{
    auto lanes = takesOf(track, um);
    juce::ValueTree take(takeId);
    edit->createNewItemID().writeID(take, nullptr);
    take.setProperty(te::IDs::name, name, nullptr);
    for (const auto& c : clips)
    {
        auto copy = c.createCopy();
        te::EditItemID::remapIDs(copy, nullptr, *edit);
        copy.removeProperty(fromTakeId, nullptr);
        take.appendChild(copy, nullptr);
    }
    lanes.appendChild(take, um);
    return take;
}

te::Clip* Session::Impl::useTakeClip(te::AudioTrack& track, const juce::ValueTree& takeClip, juce::UndoManager*)
{
    auto copy = takeClip.createCopy();
    te::EditItemID::remapIDs(copy, nullptr, *edit);
    copy.setProperty(fromTakeId, static_cast<juce::int64>(te::EditItemID::fromID(takeClip).getRawID()), nullptr);
    auto* clip = track.insertClipWithState(copy); // through the undo manager
    if (clip == nullptr)
        return nullptr;
    const auto pos = clip->getPosition();
    clearUnder(track, pos.getStart().inSeconds(), pos.getEnd().inSeconds(), {clip->itemID.getRawID()});
    return clip;
}

std::vector<std::uint64_t> Session::Impl::importTakes(te::AudioTrack& track, const juce::ReferenceCountedArray<te::Clip>& clips)
{
    auto* um = &edit->getUndoManager();
    std::vector<std::uint64_t> out;
    std::set<std::uint64_t> pass;
    for (auto* c : clips)
        if (c != nullptr)
            pass.insert(c->itemID.getRawID());
    for (auto* c : clips)
    {
        if (c == nullptr)
            continue;
        const auto infos = takeInfos(track, 0);
        std::set<std::uint64_t> takeClips;
        for (const auto& t : infos)
            for (const auto& tc : t.clips)
                takeClips.insert(tc.id);
        // What the pass recorded over, unless it came from a take: kept as a take first (§6).
        const auto range = c->getPosition().time;
        std::vector<juce::ValueTree> covered;
        for (auto* other : track.getClips())
            if (!pass.contains(other->itemID.getRawID()) && other->getPosition().time.overlaps(range) &&
                !takeClips.contains(idProperty(other->state[fromTakeId])))
                covered.push_back(other->state.createCopy());
        const int n = model::nextTakeNumber(infos);
        if (!covered.empty())
            addTake(track, "Before take " + juce::String(n), covered, um);
        // A take lane per lap; the main lane plays the newest complete one.
        const auto laps = lapsOf(*edit, *c);
        juce::ValueTree chosen;
        for (size_t i = 0; i < laps.size(); ++i)
        {
            auto take = addTake(track, "Take " + juce::String(n + static_cast<int>(i)), {laps[i].state}, um);
            if (laps[i].complete || !chosen.isValid())
                chosen = take.getChild(0);
        }
        if (laps.size() > 1) // several takes to choose from: their lanes open (view state)
            track.state.setProperty(showTakesId, true, nullptr);
        c->removeFromParent();
        if (chosen.isValid())
            if (auto* placed = useTakeClip(track, chosen, um))
                out.push_back(placed->itemID.getRawID());
    }
    return out;
}

// ---- Session -------------------------------------------------------------------------------

void Session::showTakes(std::uint64_t trackId, bool show)
{
    if (auto* t = impl_->track(trackId); t != nullptr && static_cast<bool>(t->state[Impl::showTakesId]) != show)
        t->state.setProperty(Impl::showTakesId, show, nullptr); // view state
}

bool Session::useTake(std::uint64_t trackId, std::uint64_t takeId)
{
    auto* t = impl_->track(trackId);
    const auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid())
        return false;
    impl_->beginEdit("Use take");
    for (auto clip : take)
        impl_->useTakeClip(*t, clip, &impl_->edit->getUndoManager());
    return true;
}

bool Session::stepTake(std::uint64_t trackId, double from, double to, int direction)
{
    auto* t = impl_->track(trackId);
    if (t == nullptr)
        return false;
    const auto takes = impl_->takeInfos(*t, 0);
    std::vector<model::ClipInfo> main;
    for (auto* c : t->getClips())
        main.push_back({.id = c->itemID.getRawID(),
                        .start = c->getPosition().getStart().inSeconds(),
                        .length = c->getPosition().getLength().inSeconds(),
                        .fromTake = idProperty(c->state[Impl::fromTakeId])});
    const auto next = model::stepTake(takes, model::takeAt(takes, main, (from + to) / 2.0), from, to, direction);
    if (!next)
        return false;
    impl_->beginEdit("Use take");
    const auto take = impl_->takeTree(*t, takes[*next].id);
    for (auto clip : take)
        if (static_cast<double>(clip[te::IDs::start]) <= from + 1e-6 &&
            static_cast<double>(clip[te::IDs::start]) + static_cast<double>(clip[te::IDs::length]) >= to - 1e-6)
            impl_->useTakeClip(*t, clip, &impl_->edit->getUndoManager());
    return true;
}

void Session::renameTake(std::uint64_t trackId, std::uint64_t takeId, const std::string& name)
{
    auto* t = impl_->track(trackId);
    auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid() || take[te::IDs::name].toString().toStdString() == name)
        return;
    impl_->beginEdit("Rename take");
    take.setProperty(te::IDs::name, juce::String(name), &impl_->edit->getUndoManager());
}

void Session::setTakeStar(std::uint64_t trackId, std::uint64_t takeId, bool star)
{
    auto* t = impl_->track(trackId);
    auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid() || static_cast<bool>(take[Impl::starId]) == star)
        return;
    impl_->beginEdit(star ? "Star take" : "Unstar take");
    take.setProperty(Impl::starId, star, &impl_->edit->getUndoManager());
}

void Session::deleteTakes(std::uint64_t trackId, const std::vector<std::uint64_t>& takeIds)
{
    auto* t = impl_->track(trackId);
    if (t == nullptr)
        return;
    bool begun = false;
    for (auto id : takeIds)
        if (auto take = impl_->takeTree(*t, id); take.isValid())
        {
            if (!std::exchange(begun, true))
                impl_->beginEdit(takeIds.size() == 1 ? "Delete take" : "Delete takes");
            take.getParent().removeChild(take, &impl_->edit->getUndoManager());
        }
}

void Session::deleteUnusedTakes(std::uint64_t trackId)
{
    auto* t = impl_->track(trackId);
    if (t == nullptr)
        return;
    std::vector<model::ClipInfo> main;
    for (auto* c : t->getClips())
        main.push_back({.id = c->itemID.getRawID(),
                        .start = c->getPosition().getStart().inSeconds(),
                        .length = c->getPosition().getLength().inSeconds(),
                        .fromTake = idProperty(c->state[Impl::fromTakeId])});
    std::vector<std::uint64_t> unused;
    for (const auto& take : impl_->takeInfos(*t, 0))
        if (!model::isUsed(take, main))
            unused.push_back(take.id);
    deleteTakes(trackId, unused);
}

std::vector<std::uint64_t> Session::takesToTracks(std::uint64_t trackId, const std::vector<std::uint64_t>& takeIds, bool move)
{
    std::vector<std::uint64_t> out;
    auto* t = impl_->track(trackId);
    if (t == nullptr || takeIds.empty())
        return out;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(move ? "Move takes to new tracks" : "Copy takes to new tracks");
    te::Track::Ptr after = t;
    for (auto id : takeIds)
    {
        auto take = impl_->takeTree(*t, id);
        if (!take.isValid())
            continue;
        // A copy of the track: its modules and mix, without its clips, takes or automation.
        auto copy = impl_->edit->copyTrack(t, te::TrackInsertPoint(nullptr, after.get()));
        auto* track = dynamic_cast<te::AudioTrack*>(copy.get());
        if (track == nullptr)
            continue;
        for (auto* c : track->getClips())
            c->removeFromParent();
        if (auto lanes = track->state.getChildWithName(Impl::takesId); lanes.isValid())
            track->state.removeChild(lanes, um);
        te::AutomationCurve::removeAllAutomationCurvesRecursively(track->state);
        track->state.setProperty(Impl::armedId, false, nullptr);
        track->setName(t->getName() + " " + take[te::IDs::name].toString());
        for (auto clip : take)
            impl_->useTakeClip(*track, clip, um);
        if (move)
            take.getParent().removeChild(take, um);
        out.push_back(track->itemID.getRawID());
        after = copy;
    }
    return out;
}

std::uint64_t Session::captureMidi()
{
    if (impl_->edit->getCurrentPlaybackContext() == nullptr || isRecording())
        return 0;
    impl_->beginEdit("Capture MIDI");
    // MIDI inputs only (tracktion's audio capture needs its own project system).
    juce::Array<te::Clip*> clips;
    for (auto* instance : impl_->edit->getAllInputDevices())
    {
        const auto type = instance->getInputDevice().getDeviceType();
        if (type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice)
            clips.addArray(instance->applyRetrospectiveRecord(true));
    }
    impl_->lastCaptureAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
    std::uint64_t made = 0;
    const bool stopped = !isPlaying();
    for (auto* c : clips)
    {
        auto* midi = dynamic_cast<te::MidiClip*>(c);
        auto* track = midi != nullptr ? dynamic_cast<te::AudioTrack*>(midi->getTrack()) : nullptr;
        if (track == nullptr || midi->getSequence().getNumNotes() == 0)
            continue;
        // Trimmed to its notes, on whole beats (§8).
        auto& tempo = impl_->edit->tempoSequence;
        const auto pos = midi->getPosition();
        const double content = tempo.toBeats(pos.getStart() - pos.getOffset()).inBeats();
        double first = 1e9, last = 0.0;
        for (auto* n : midi->getSequence().getNotes())
        {
            first = std::min(first, n->getStartBeat().inBeats());
            last = std::max(last, n->getEndBeat().inBeats());
        }
        const double from = std::floor(content + first), to = std::ceil(content + last);
        // Stopped: at the playhead (on its beat), the first note's place in its beat kept.
        const double at = stopped ? std::floor(tempo.toBeats(impl_->edit->getTransport().getPosition()).inBeats()) : from;
        const auto start = tempo.toTime(te::BeatPosition::fromBeats(at));
        const auto end = tempo.toTime(te::BeatPosition::fromBeats(at + (to - from)));
        const auto offset = tempo.toTime(te::BeatPosition::fromBeats(from)) - tempo.toTime(te::BeatPosition::fromBeats(content));
        midi->setPosition({{start, end}, offset});
        midi->setName(track->getName());
        impl_->clearUnder(*track, start.inSeconds(), end.inSeconds(), {midi->itemID.getRawID()});
        if (made == 0)
            made = midi->itemID.getRawID();
    }
    return made;
}

bool Session::canCaptureMidi() const
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double last = impl_->lastNoteAt.load();
    return !isRecording() && last > impl_->lastCaptureAt && now - last < 60.0;
}
} // namespace fd::engine
