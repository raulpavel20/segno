// Take lanes and Capture MIDI (M2 step C5b, ADR 0031, docs/design/recording.md §6, §8): every pass
// and loop lap kept under its track; the main lane plays plain clips made from them.

#include "engine/ClipFades.h"
#include "engine/SessionImpl.h"

#include "model/NoteEditing.h"
#include "model/TrackEdits.h"

#include <limits>

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

// ---- stacks: storage ------------------------------------------------------------------------

namespace
{
std::uint64_t idOf(const juce::ValueTree& v)
{
    return te::EditItemID::fromID(v).getRawID();
}

double startOf(const juce::ValueTree& clip)
{
    return clip[te::IDs::start];
}

double endOf(const juce::ValueTree& clip)
{
    return static_cast<double>(clip[te::IDs::start]) + static_cast<double>(clip[te::IDs::length]);
}

/// A clip state trimmed to [from, to] (its offset follows its start); invalid if nothing's left.
juce::ValueTree trimmedState(const juce::ValueTree& clip, double from, double to)
{
    const double start = std::max(startOf(clip), from), end = std::min(endOf(clip), to);
    if (end - start < 1e-6)
        return {};
    auto copy = clip.createCopy();
    copy.setProperty(te::IDs::offset, static_cast<double>(clip[te::IDs::offset]) + (start - startOf(clip)), nullptr);
    copy.setProperty(te::IDs::start, start, nullptr);
    copy.setProperty(te::IDs::length, end - start, nullptr);
    return copy;
}

/// The ids of a stack's take clips.
std::set<std::uint64_t> takeClipsOf(const juce::ValueTree& stack)
{
    std::set<std::uint64_t> out;
    for (auto take : stack)
        for (auto clip : take)
            out.insert(idOf(clip));
    return out;
}

/// A stack's takes as the model's numbering sees them.
std::vector<model::TakeInfo> namesOf(const juce::ValueTree& stack)
{
    std::vector<model::TakeInfo> out;
    for (auto take : stack)
        out.push_back({.name = take[te::IDs::name].toString().toStdString()});
    return out;
}

/// A dangling stack id for model::settleStacks: a clip naming a take clip that isn't there.
constexpr std::uint64_t kDangling = std::numeric_limits<std::uint64_t>::max();
} // namespace

juce::ValueTree Session::Impl::takesOf(te::AudioTrack& track, juce::UndoManager* um)
{
    auto takes = track.state.getChildWithName(takesId);
    if (!takes.isValid() && um != nullptr)
    {
        takes = juce::ValueTree(takesId);
        track.state.appendChild(takes, um);
    }
    return takes;
}

juce::ValueTree Session::Impl::stackTree(te::AudioTrack& track, std::uint64_t stack)
{
    for (auto s : track.state.getChildWithName(takesId))
        if (s.hasType(stackId) && idOf(s) == stack)
            return s;
    return {};
}

std::pair<te::AudioTrack*, juce::ValueTree> Session::Impl::findStack(std::uint64_t stack)
{
    for (auto* t : te::getAudioTracks(*edit))
        if (auto s = stackTree(*t, stack); s.isValid())
            return {t, s};
    return {nullptr, {}};
}

juce::ValueTree Session::Impl::takeTree(te::AudioTrack& track, std::uint64_t take)
{
    for (auto s : track.state.getChildWithName(takesId))
        for (auto t : s)
            if (t.hasType(takeId) && idOf(t) == take)
                return t;
    return {};
}

juce::ValueTree Session::Impl::stackOf(te::Clip& clip)
{
    const auto from = idProperty(clip.state[fromTakeId]);
    auto* track = dynamic_cast<te::AudioTrack*>(clip.getClipTrack());
    if (from == 0 || track == nullptr)
        return {};
    for (auto s : track->state.getChildWithName(takesId))
        for (auto t : s)
            for (auto c : t)
                if (idOf(c) == from)
                    return s;
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
    info.muted = tree[te::IDs::mute];
    info.fadeIn = {static_cast<double>(tree.getProperty(te::IDs::fadeIn, 0.0)), static_cast<float>(tree.getProperty("fdFadeInBend", 0.0f))};
    info.fadeOut = {static_cast<double>(tree.getProperty(te::IDs::fadeOut, 0.0)), static_cast<float>(tree.getProperty("fdFadeOutBend", 0.0f))};
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
        info.sound.reversed = tree.hasProperty(soundIds::forward);
        info.warp = warpOf(tree, edit->tempoSequence.getBpmAt(te::TimePosition::fromSeconds(info.start)));
        info.pitch = pitchOf(tree);
        info.fileLength = te::AudioFile(engine, audio).getLength();
        info.beatsPerSecond =
            edit->tempoSequence.getBeatsPerSecondAt(te::TimePosition::fromSeconds(info.start));
        info.sourceLength = info.fileLength > 0.0 ? model::contentMap(info).contentLength() : 0.0;
    }
    info.loop = loopOf(tree, edit->tempoSequence); // ADR 0048
    if (info.loop.on() && info.kind == model::ClipInfo::Kind::Midi)
        info.contentStartBeat = edit->tempoSequence.toBeats(te::TimePosition::fromSeconds(model::clipViewOrigin(info))).inBeats();
    return info;
}

model::TakeInfo Session::Impl::takeInfo(const juce::ValueTree& take, int colour) const
{
    model::TakeInfo info{.id = idOf(take), .name = take[te::IDs::name].toString().toStdString()};
    for (auto c : take)
        info.clips.push_back(storedClipInfo(c, colour));
    std::sort(info.clips.begin(), info.clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return info;
}

std::vector<model::TakeInfo> Session::Impl::takeInfos(te::AudioTrack& track, int colour) const
{
    std::vector<model::TakeInfo> out;
    for (auto s : track.state.getChildWithName(takesId))
        for (auto t : s)
            out.push_back(takeInfo(t, colour));
    return out;
}

std::vector<model::TakeStackInfo> Session::Impl::stackInfos(te::AudioTrack& track, int colour) const
{
    std::vector<model::TakeStackInfo> out;
    for (auto s : track.state.getChildWithName(takesId))
    {
        if (!s.hasType(stackId))
            continue;
        model::TakeStackInfo info{.id = idOf(s), .start = s[fromId], .end = s[toId], .open = s[stackOpenId]};
        for (auto t : s)
            info.takes.push_back(takeInfo(t, colour));
        const auto clips = takeClipsOf(s);
        std::vector<std::pair<double, std::uint64_t>> parts;
        for (auto* c : track.getClips())
            if (clips.contains(idProperty(c->state[fromTakeId])))
                parts.emplace_back(c->getPosition().getStart().inSeconds(), c->itemID.getRawID());
        std::sort(parts.begin(), parts.end());
        for (const auto& [at, id] : parts)
            info.parts.push_back(id);
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}

juce::ValueTree Session::Impl::makeStack(te::AudioTrack& track, double from, double to, juce::UndoManager* um)
{
    juce::ValueTree stack(stackId);
    edit->createNewItemID().writeID(stack, nullptr);
    stack.setProperty(fromId, from, nullptr);
    stack.setProperty(toId, to, nullptr);
    takesOf(track, um).appendChild(stack, um);
    return stack;
}

juce::ValueTree Session::Impl::addTake(juce::ValueTree stack, const juce::String& name, const std::vector<juce::ValueTree>& clips,
                                       juce::UndoManager* um)
{
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
    stack.appendChild(take, um);
    return take;
}

te::Clip* Session::Impl::useTakeClip(te::AudioTrack& track, const juce::ValueTree& takeClip, double from, double to, bool asTake)
{
    auto copy = trimmedState(takeClip, from, to);
    if (!copy.isValid())
        return nullptr;
    te::EditItemID::remapIDs(copy, nullptr, *edit);
    if (asTake)
        copy.setProperty(fromTakeId, static_cast<juce::int64>(idOf(takeClip)), nullptr);
    else
        copy.removeProperty(fromTakeId, nullptr);
    auto* clip = track.insertClipWithState(copy); // through the undo manager
    if (clip == nullptr)
        return nullptr;
    // A take kept aside while the song's key changed plays in the key it is now (§8.3).
    if (auto* audio = dynamic_cast<te::AudioClipBase*>(clip); audio != nullptr && followsKey(audio->state))
        syncPitch(*audio);
    const auto pos = clip->getPosition();
    clearUnder(track, pos.getStart().inSeconds(), pos.getEnd().inSeconds(), {clip->itemID.getRawID()});
    return clip;
}

bool Session::Impl::fillFromTake(te::AudioTrack& track, const juce::ValueTree& take)
{
    auto* um = &edit->getUndoManager();
    const auto stack = take.getParent();
    const double from = stack[fromId], to = stack[toId];
    bool any = false;
    for (auto clip : take)
        any = any || trimmedState(clip, from, to).isValid();
    auto parts = partsOf(track, stack);
    if (!any || parts.empty())
        return false;
    const auto ends = clipEnds(track, stack);
    // Nothing of what played stays (a bounce's audio tail goes when its MIDI comes back, ADR 0035).
    for (auto* c : parts)
        c->removeFromParent();
    for (auto clip : take)
        useTakeClip(track, clip, from, to);
    restoreEnds(track, stack, ends, um);
    settle(track, um);
    return true;
}

std::pair<juce::ValueTree, juce::ValueTree> Session::Impl::clipEnds(te::AudioTrack& track, const juce::ValueTree& stack)
{
    const auto parts = partsOf(track, stack);
    if (parts.empty())
        return {};
    return {parts.front()->state.createCopy(), parts.back()->state.createCopy()};
}

void Session::Impl::restoreEnds(te::AudioTrack& track, const juce::ValueTree& stack,
                                const std::pair<juce::ValueTree, juce::ValueTree>& ends, juce::UndoManager* um)
{
    const auto parts = partsOf(track, stack);
    auto set = [&](const juce::ValueTree& was, te::Clip* now, std::initializer_list<juce::Identifier> ids) {
        for (const auto& id : ids)
        {
            if (was.isValid() && was.hasProperty(id))
                now->state.setProperty(id, was[id], um);
            else
                now->state.removeProperty(id, um);
        }
    };
    const std::initializer_list<juce::Identifier> in{te::IDs::fadeIn, fadeIds::inBend},
        out{te::IDs::fadeOut, fadeIds::outBend};
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (dynamic_cast<te::AudioClipBase*>(parts[i]) == nullptr)
            continue;
        set(i == 0 ? ends.first : juce::ValueTree(), parts[i], in);
        set(i + 1 == parts.size() ? ends.second : juce::ValueTree(), parts[i], out);
        if (i + 1 == parts.size())
            set(ends.second, parts[i], {fadeIds::crossLength, fadeIds::crossFrom, fadeIds::crossShape});
    }
}

void Session::Impl::joinSeamlessParts(te::AudioTrack& track, const juce::ValueTree& stack, juce::UndoManager* um)
{
    juce::ignoreUnused(um); // clip positions and removal go through the edit's undo manager
    for (;;)
    {
        std::vector<model::ClipInfo> infos;
        for (auto* c : partsOf(track, stack))
        {
            const auto pos = c->getPosition();
            infos.push_back({.id = c->itemID.getRawID(),
                             .start = pos.getStart().inSeconds(),
                             .length = pos.getLength().inSeconds(),
                             .offset = pos.getOffset().inSeconds(),
                             .fromTake = idProperty(c->state[fromTakeId])});
        }
        const auto pairs = model::seamlessParts(infos);
        if (pairs.empty())
            return;
        auto* a = clip(pairs.front().first);
        auto* b = clip(pairs.front().second);
        const auto pos = a->getPosition();
        const auto end = b->getPosition().getEnd();
        b->removeFromParent();
        a->setPosition({{pos.getStart(), end}, pos.getOffset()});
    }
}

std::vector<te::Clip*> Session::Impl::partsOf(te::AudioTrack& track, const juce::ValueTree& stack)
{
    std::vector<te::Clip*> out;
    for (auto* c : track.getClips())
        if (stackOf(*c) == stack)
            out.push_back(c);
    std::sort(out.begin(), out.end(),
              [](auto* a, auto* b) { return a->getPosition().getStart() < b->getPosition().getStart(); });
    return out;
}

void Session::Impl::openStack(te::AudioTrack& track, juce::ValueTree stack, bool open)
{
    if (open) // one set of strips over any stretch of time
        for (auto other : track.state.getChildWithName(takesId))
            if (other != stack && static_cast<bool>(other[stackOpenId]) &&
                static_cast<double>(other[fromId]) < static_cast<double>(stack[toId]) &&
                static_cast<double>(stack[fromId]) < static_cast<double>(other[toId]))
                other.setProperty(stackOpenId, false, nullptr);
    if (static_cast<bool>(stack[stackOpenId]) != open)
        stack.setProperty(stackOpenId, open, nullptr); // view state
}

void Session::Impl::moveStack(std::uint64_t stack, te::AudioTrack& target, double delta, juce::UndoManager* um)
{
    auto [track, tree] = findStack(stack);
    if (track == nullptr)
        return;
    if (std::abs(delta) > 1e-12)
    {
        for (auto take : tree)
            for (auto clip : take)
                clip.setProperty(te::IDs::start, startOf(clip) + delta, um);
        tree.setProperty(fromId, static_cast<double>(tree[fromId]) + delta, um);
        tree.setProperty(toId, static_cast<double>(tree[toId]) + delta, um);
    }
    if (track != &target)
    {
        tree.getParent().removeChild(tree, um);
        takesOf(target, um).appendChild(tree, um);
    }
}

juce::ValueTree Session::Impl::copyStack(const juce::ValueTree& stack, te::AudioTrack& target, double delta,
                                         std::map<std::uint64_t, std::uint64_t>& map, juce::UndoManager* um)
{
    auto copy = stack.createCopy();
    te::EditItemID::IDMap ids;
    te::EditItemID::remapIDs(copy, nullptr, *edit, &ids);
    for (const auto& [from, to] : ids)
        map[from.getRawID()] = to.getRawID();
    for (auto take : copy)
        for (auto clip : take)
            clip.setProperty(te::IDs::start, startOf(clip) + delta, nullptr);
    copy.setProperty(fromId, static_cast<double>(copy[fromId]) + delta, nullptr);
    copy.setProperty(toId, static_cast<double>(copy[toId]) + delta, nullptr);
    copy.removeProperty(stackOpenId, nullptr);
    takesOf(target, um).appendChild(copy, um);
    return copy;
}

juce::ValueTree Session::Impl::splitStack(te::AudioTrack& track, juce::ValueTree stack, double at, juce::UndoManager* um)
{
    auto right = makeStack(track, at, stack[toId], um);
    right.setProperty(stackOpenId, stack[stackOpenId], nullptr);
    std::map<std::uint64_t, std::uint64_t> halves; // a split take clip's left id -> its right half's
    std::vector<juce::ValueTree> emptied;
    for (auto take : stack)
    {
        juce::ValueTree rightTake(takeId);
        rightTake.copyPropertiesFrom(take, nullptr); // its name
        edit->createNewItemID().writeID(rightTake, nullptr);
        std::vector<juce::ValueTree> whole;
        for (auto clip : take)
        {
            if (endOf(clip) <= at + 1e-9)
                continue;
            if (startOf(clip) >= at - 1e-9)
            {
                whole.push_back(clip); // moves whole: parts playing it keep its id
                continue;
            }
            auto half = trimmedState(clip, at, endOf(clip));
            te::EditItemID::remapIDs(half, nullptr, *edit);
            halves[idOf(clip)] = idOf(half);
            rightTake.appendChild(half, nullptr);
            clip.setProperty(te::IDs::length, at - startOf(clip), um);
        }
        for (auto clip : whole)
        {
            take.removeChild(clip, um);
            rightTake.appendChild(clip, nullptr);
        }
        if (rightTake.getNumChildren() > 0)
            right.appendChild(rightTake, um);
        if (take.getNumChildren() == 0)
            emptied.push_back(take);
    }
    for (auto take : emptied)
        stack.removeChild(take, um);
    stack.setProperty(toId, at, um);
    // Main clips from `at` on play the right halves.
    for (auto* c : track.getClips())
        if (c->getPosition().getStart().inSeconds() >= at - 1e-9)
            if (const auto it = halves.find(idProperty(c->state[fromTakeId])); it != halves.end())
                c->state.setProperty(fromTakeId, static_cast<juce::int64>(it->second), um);
    return right;
}

void Session::Impl::removeTakes(te::AudioTrack& track, const std::set<std::uint64_t>& takes, juce::UndoManager* um)
{
    // Parts playing a deleted take switch to another one of their stack (or become plain). By id:
    // a replacement removes the part it covers.
    std::vector<std::uint64_t> parts;
    for (auto* c : track.getClips())
        parts.push_back(c->itemID.getRawID());
    for (const auto part : parts)
    {
        auto* c = clip(part);
        if (c == nullptr)
            continue;
        auto stack = stackOf(*c);
        if (!stack.isValid())
            continue;
        const auto from = idProperty(c->state[fromTakeId]);
        bool deleted = false;
        for (auto take : stack)
            if (takes.contains(idOf(take)))
                for (auto clip : take)
                    deleted = deleted || idOf(clip) == from;
        if (!deleted)
            continue;
        const auto pos = c->getPosition();
        const double a = pos.getStart().inSeconds(), b = pos.getEnd().inSeconds();
        model::TakeStackInfo info{.id = idOf(stack)};
        for (auto take : stack)
            info.takes.push_back(takeInfo(take, 0));
        if (const auto other = model::replacementTake(info, takes, a, b))
        {
            for (auto takeClip : stack.getChild(static_cast<int>(*other)))
                useTakeClip(track, takeClip, a, b); // covers the part, which goes
            if (auto* left = clip(part)) // what the other take doesn't cover plays nothing of a deleted take
                left->state.removeProperty(fromTakeId, um);
        }
        else
            c->state.removeProperty(fromTakeId, um);
    }
    for (auto s : track.state.getChildWithName(takesId))
        for (int i = s.getNumChildren(); --i >= 0;)
            if (takes.contains(idOf(s.getChild(i))))
                s.removeChild(i, um);
    settle(track, um);
}

std::vector<model::ClipPlace> Session::Impl::places(te::AudioTrack& track)
{
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> owners; // take clip -> stack, take
    for (auto s : track.state.getChildWithName(takesId))
        for (auto take : s)
            for (auto clip : take)
                owners[idOf(clip)] = {idOf(s), idOf(take)};
    std::vector<model::ClipPlace> out;
    for (auto* c : track.getClips())
    {
        const auto from = idProperty(c->state[fromTakeId]);
        const auto it = owners.find(from);
        const auto pos = c->getPosition();
        out.push_back({.clip = c->itemID.getRawID(),
                       .track = track.itemID.getRawID(),
                       .stack = from == 0 ? 0 : it != owners.end() ? it->second.first : kDangling,
                       .start = pos.getStart().inSeconds(),
                       .length = pos.getLength().inSeconds(),
                       .offset = pos.getOffset().inSeconds(),
                       .take = it != owners.end() ? it->second.second : 0});
    }
    return out;
}

std::vector<model::StackRange> Session::Impl::stackRanges(te::AudioTrack& track)
{
    std::vector<model::StackRange> out;
    for (auto s : track.state.getChildWithName(takesId))
        if (s.hasType(stackId))
            out.push_back({idOf(s), s[fromId], s[toId]});
    return out;
}

void Session::Impl::settle(te::AudioTrack& track, juce::UndoManager* um)
{
    const auto result = model::settleStacks(places(track), stackRanges(track));
    for (const auto id : result.plain)
        if (auto* c = clip(id))
            c->state.removeProperty(fromTakeId, um);
    for (const auto id : result.remove)
        if (auto s = stackTree(track, id); s.isValid())
            s.getParent().removeChild(s, um);
    for (const auto& r : result.ranges)
        if (auto s = stackTree(track, r.id); s.isValid())
        {
            s.setProperty(fromId, r.start, um);
            s.setProperty(toId, r.end, um);
        }
    settleFades(track, um);
    colourNewClips(track, um);
}

void Session::Impl::colourNewClips(te::AudioTrack& track, juce::UndoManager* um)
{
    const auto colour = colourOf(track.itemID.getRawID());
    std::vector<std::pair<juce::ValueTree, juce::var>> stackColours; // a stack's first playing clip's
    for (auto* c : track.getClips())
    {
        if (!c->state.hasProperty(colourPropertyId))
            c->state.setProperty(colourPropertyId, colour, um);
        if (auto s = stackOf(*c); s.isValid() && std::none_of(stackColours.begin(), stackColours.end(),
                                                              [&](const auto& p) { return p.first == s; }))
            stackColours.emplace_back(s, c->state[colourPropertyId]);
    }
    for (auto s : track.state.getChildWithName(takesId))
    {
        const auto it = std::find_if(stackColours.begin(), stackColours.end(),
                                     [&](const auto& p) { return p.first == s; });
        const auto own = it != stackColours.end() ? it->second : juce::var(colour);
        for (auto take : s)
            for (auto c : take)
                if (!c.hasProperty(colourPropertyId))
                    c.setProperty(colourPropertyId, own, um);
    }
}

void Session::Impl::loadTakes(bool convert)
{
    for (auto* t : te::getAudioTracks(*edit))
    {
        auto takes = t->state.getChildWithName(takesId);
        if (convert && takes.isValid())
        {
            // C5b: TAKEs right under FDTAKES (take lanes). Overlapping ones become one clip's takes,
            // its range the clips playing from them; takes nothing plays go (their files stay).
            std::vector<juce::ValueTree> lanes;
            for (auto child : takes)
                if (child.hasType(takeId))
                    lanes.push_back(child);
            std::vector<std::pair<double, double>> spans;
            for (const auto& lane : lanes)
            {
                double from = 1e300, to = -1e300;
                for (auto clip : lane)
                {
                    from = std::min(from, startOf(clip));
                    to = std::max(to, endOf(clip));
                }
                spans.emplace_back(from, to);
            }
            for (const auto& lane : lanes)
                takes.removeChild(lane, nullptr);
            for (const auto& group : model::overlapGroups(spans))
            {
                std::set<std::uint64_t> clips;
                for (const auto i : group)
                    for (auto clip : lanes[i])
                        clips.insert(idOf(clip));
                double from = 1e300, to = -1e300;
                for (auto* c : t->getClips())
                    if (clips.contains(idProperty(c->state[fromTakeId])))
                    {
                        from = std::min(from, c->getPosition().getStart().inSeconds());
                        to = std::max(to, c->getPosition().getEnd().inSeconds());
                    }
                if (!(to > from))
                    continue;
                auto stack = makeStack(*t, from, to, nullptr);
                for (const auto i : group)
                    stack.appendChild(lanes[i], nullptr);
            }
        }
        t->state.removeProperty(showTakesId, nullptr);
        settle(*t, nullptr);
    }
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
        const auto range = c->getPosition().time;
        const double a = range.getStart().inSeconds(), b = range.getEnd().inSeconds();
        // A pass makes its own clip (ADR 0037): the clips it covers entirely give it their takes;
        // what played over its range is kept as a take; placing it cuts the rest (ADR 0019).
        auto others = places(track);
        std::erase_if(others, [&](const auto& p) { return pass.contains(p.clip); });
        const auto cover = model::passCover(others, a, b);
        std::vector<juce::ValueTree> joined, covered;
        for (const auto id : cover.join)
            if (auto s = stackTree(track, id); s.isValid())
                joined.push_back(s);
        for (const auto id : cover.before)
            if (auto* other = clip(id))
                if (auto trimmed = trimmedState(other->state, a, b); trimmed.isValid())
                {
                    // As it played, but for fades at edges the pass cut, and joins it doesn't keep.
                    if (startOf(trimmed) > startOf(other->state) + 1e-9)
                        for (const auto& property : {te::IDs::fadeIn, fadeIds::inBend})
                            trimmed.removeProperty(property, nullptr);
                    if (endOf(trimmed) < endOf(other->state) - 1e-9)
                        for (const auto& property : {te::IDs::fadeOut, fadeIds::outBend})
                            trimmed.removeProperty(property, nullptr);
                    for (const auto& property : {fadeIds::crossLength, fadeIds::crossFrom, fadeIds::crossShape})
                        trimmed.removeProperty(property, nullptr);
                    covered.push_back(trimmed);
                }
        juce::ValueTree stack;
        if (joined.empty())
            stack = makeStack(track, a, b, um);
        else
        {
            std::sort(joined.begin(), joined.end(),
                      [](const auto& x, const auto& y) { return static_cast<double>(x[fromId]) < static_cast<double>(y[fromId]); });
            stack = joined.front();
            double from = std::min(a, static_cast<double>(stack[fromId])), to = std::max(b, static_cast<double>(stack[toId]));
            for (size_t i = 1; i < joined.size(); ++i)
            {
                from = std::min(from, static_cast<double>(joined[i][fromId]));
                to = std::max(to, static_cast<double>(joined[i][toId]));
                while (joined[i].getNumChildren() > 0)
                {
                    auto take = joined[i].getChild(0);
                    joined[i].removeChild(take, um);
                    stack.appendChild(take, um);
                }
                joined[i].getParent().removeChild(joined[i], um);
            }
            stack.setProperty(fromId, from, um);
            stack.setProperty(toId, to, um);
        }
        // Numbered along the track: takes keep their names when they join another clip's.
        std::vector<model::TakeInfo> names;
        for (auto other : track.state.getChildWithName(takesId))
            if (other.hasType(stackId))
                for (const auto& k : namesOf(other))
                    names.push_back(k);
        const int n = model::nextTakeNumber(names);
        if (!covered.empty())
            addTake(stack, "Before take " + juce::String(n), covered, um);
        // A take per lap; the clip plays the newest complete one.
        const auto laps = lapsOf(*edit, *c);
        juce::ValueTree chosen;
        for (size_t i = 0; i < laps.size(); ++i)
        {
            auto take = addTake(stack, "Take " + juce::String(n + static_cast<int>(i)), {laps[i].state}, um);
            if (laps[i].complete || !chosen.isValid())
                chosen = take.getChild(0);
        }
        c->removeFromParent();
        if (chosen.isValid())
            if (auto* placed = useTakeClip(track, chosen, a, b))
            {
                out.push_back(placed->itemID.getRawID());
                // It plays the new take alone: slivers of the clips it took in (within the slack) go.
                for (auto* rest : partsOf(track, stack))
                    if (rest != placed)
                        rest->removeFromParent();
            }
        if (laps.size() > 1) // several takes to choose from: they open under the clip (view state)
            openStack(track, stack, true);
    }
    settle(track, um);
    return out;
}

// ---- Session -------------------------------------------------------------------------------

void Session::setTakesOpen(std::uint64_t stackId, bool open)
{
    if (!open && impl_->audition && impl_->audition->stack == stackId) // its strips go: so does the audition
        impl_->stopAudition();
    if (auto [track, stack] = impl_->findStack(stackId); track != nullptr)
        impl_->openStack(*track, stack, open);
}

bool Session::auditionTake(std::uint64_t trackId, std::uint64_t takeId)
{
    auto* t = impl_->track(trackId);
    const auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid())
        return false;
    impl_->stopAudition();
    // A listening aid: never an undo step, and never an unsaved change (as solo).
    auto& edit = *impl_->edit;
    const bool wasChanged = edit.hasChangedSinceSaved();
    const bool wasDirty = impl_->dirty;
    const auto stack = take.getParent();
    const double from = stack[Impl::fromId], to = stack[Impl::toId];
    Impl::Audition audition{.track = trackId, .stack = idOf(stack), .take = takeId};
    for (auto* c : impl_->partsOf(*t, stack))
        if (!c->isMuted())
        {
            c->state.setProperty(te::IDs::mute, true, nullptr);
            audition.muted.push_back(c->itemID.getRawID());
        }
    for (auto clip : take)
        if (auto copy = trimmedState(clip, from, to); copy.isValid())
        {
            te::EditItemID::remapIDs(copy, nullptr, edit);
            copy.removeProperty(Impl::fromTakeId, nullptr);
            copy.setProperty(Impl::auditionId, true, nullptr);
            t->state.appendChild(copy, nullptr);
            audition.added.push_back(idOf(copy));
        }
    impl_->audition = std::move(audition);
    if (!wasChanged)
        edit.resetChangedStatus();
    impl_->dirty = wasDirty;
    return true;
}

void Session::stopAudition()
{
    impl_->stopAudition();
}

void Session::Impl::stopAudition()
{
    if (!audition || edit == nullptr)
        return;
    const auto a = std::move(*audition);
    audition.reset();
    const bool wasChanged = edit->hasChangedSinceSaved();
    const bool wasDirty = dirty;
    for (const auto id : a.added)
        if (auto* c = clip(id))
            c->state.getParent().removeChild(c->state, nullptr);
    for (const auto id : a.muted)
        if (auto* c = clip(id))
            c->state.setProperty(te::IDs::mute, false, nullptr);
    if (!wasChanged)
        edit->resetChangedStatus();
    dirty = wasDirty;
}

bool Session::useTake(std::uint64_t trackId, std::uint64_t takeId)
{
    auto* t = impl_->track(trackId);
    const auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid())
        return false;
    impl_->beginEdit("Use take");
    impl_->fillFromTake(*t, take);
    return true;
}

bool Session::compTake(std::uint64_t trackId, std::uint64_t takeId, double from, double to, const std::string& undoName)
{
    auto* t = impl_->track(trackId);
    const auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid())
        return false;
    const auto stack = take.getParent();
    model::TakeStackInfo info;
    for (const auto& s : impl_->stackInfos(*t, 0))
        if (s.id == idOf(stack))
            info = s;
    const auto pieces = model::swipePieces(info, takeId, from, to);
    if (pieces.empty() || impl_->partsOf(*t, stack).empty())
        return false;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(juce::String(undoName));
    const auto ends = impl_->clipEnds(*t, stack);
    std::vector<double> edges;
    for (const auto& piece : pieces)
        for (auto clip : take)
            if (idOf(clip) == piece.takeClip && impl_->useTakeClip(*t, clip, piece.from, piece.to) != nullptr)
            {
                edges.push_back(piece.from);
                edges.push_back(piece.to);
            }
    impl_->joinSeamlessParts(*t, stack, um);
    impl_->restoreEnds(*t, stack, ends, um);
    std::set<std::uint64_t> parts;
    for (auto* c : impl_->partsOf(*t, stack))
        parts.insert(c->itemID.getRawID());
    impl_->crossfadeJoinsAt(*t, parts, edges, model::kCrossfadeSeconds, um);
    impl_->settle(*t, um);
    return true;
}

bool Session::stepTake(std::uint64_t clipId, int direction, std::optional<double> at)
{
    auto* c = impl_->clip(clipId);
    auto* t = c != nullptr ? dynamic_cast<te::AudioTrack*>(c->getClipTrack()) : nullptr;
    const auto stack = c != nullptr ? impl_->stackOf(*c) : juce::ValueTree();
    if (t == nullptr || !stack.isValid())
        return false;
    model::TakeStackInfo info;
    for (const auto& s : impl_->stackInfos(*t, 0))
        if (s.id == idOf(stack))
            info = s;
    std::vector<model::ClipInfo> main;
    for (auto* clip : t->getClips())
    {
        const auto id = clip->itemID.getRawID();
        main.push_back({.id = id,
                        .start = clip->getPosition().getStart().inSeconds(),
                        .length = clip->getPosition().getLength().inSeconds(),
                        .fromTake = idProperty(clip->state[Impl::fromTakeId]),
                        .stack = std::find(info.parts.begin(), info.parts.end(), id) != info.parts.end() ? info.id : 0});
    }
    // The section at `at` (the first when it's outside the clip; ADR 0038); a clip that isn't a
    // comp is one section, and plays the take alone.
    const auto all = model::sections(info, main);
    if (all.empty())
        return false;
    const auto section = at ? model::sectionAt(info, main, *at).value_or(all.front()) : all.front();
    const auto next = model::stepTake(info.takes, model::sectionTake(info, main, section), section.first, section.second, direction);
    if (!next)
        return false;
    if (all.size() == 1)
    {
        impl_->beginEdit("Use take");
        impl_->fillFromTake(*t, stack.getChild(static_cast<int>(*next)));
        return true;
    }
    return compTake(t->itemID.getRawID(), info.takes[*next].id, section.first, section.second, "Use take");
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

void Session::deleteTakes(std::uint64_t trackId, const std::vector<std::uint64_t>& takeIds)
{
    auto* t = impl_->track(trackId);
    if (t == nullptr)
        return;
    std::set<std::uint64_t> takes;
    for (auto id : takeIds)
        if (impl_->takeTree(*t, id).isValid())
            takes.insert(id);
    if (takes.empty())
        return;
    impl_->beginEdit(takes.size() == 1 ? "Delete take" : "Delete takes");
    impl_->removeTakes(*t, takes, &impl_->edit->getUndoManager());
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
    if (unused.empty())
        return;
    impl_->beginEdit("Delete unused takes");
    impl_->removeTakes(*t, {unused.begin(), unused.end()}, &impl_->edit->getUndoManager());
}

bool Session::keepOnlyTake(std::uint64_t trackId, std::uint64_t takeId)
{
    auto* t = impl_->track(trackId);
    const auto take = t != nullptr ? impl_->takeTree(*t, takeId) : juce::ValueTree();
    if (!take.isValid())
        return false;
    impl_->beginEdit("Keep only this take");
    if (!impl_->fillFromTake(*t, take)) // nothing of it in the clip's range: the others stay
        return false;
    std::set<std::uint64_t> others;
    for (auto other : take.getParent())
        if (other != take)
            others.insert(idOf(other));
    impl_->removeTakes(*t, others, &impl_->edit->getUndoManager());
    return true;
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
    std::set<std::uint64_t> moved;
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
        // Curves are cleared in place: removing their trees would leave the copied parameters
        // writing to curves no longer in the project, so lanes assigned later wouldn't play.
        for (auto* p : track->pluginList)
            for (auto* param : p->getAutomatableParameters())
                if (param->getCurve().getNumPoints() > 0)
                    param->getCurve().clear(um);
        track->state.setProperty(Impl::armedId, false, nullptr);
        track->setName(juce::String::fromUTF8(
            model::uniqueName((t->getName() + " " + take[te::IDs::name].toString()).toStdString(), impl_->trackNames()).c_str()));
        for (auto clip : take) // plain clips: the new track has no takes
            impl_->useTakeClip(*track, clip, -1e300, 1e300, false);
        if (move)
            moved.insert(id);
        out.push_back(track->itemID.getRawID());
        after = copy;
    }
    if (!moved.empty())
        impl_->removeTakes(*t, moved, um);
    return out;
}

// ---- Capture MIDI (recording.md §8) --------------------------------------------------------

void Session::Impl::drainTap()
{
    if (edit == nullptr)
        return;
    auto& transport = edit->getTransport();
    const bool playing = transport.isPlaying();
    const double now = tap.now();
    if (playing && !wasPlaying)
        playStartedAt = now; // started elsewhere than play() (assistants, the engine)
    wasPlaying = playing;
    // The playhead now, from the audio (the transport's own position follows it on a timer).
    const auto* context = edit->getCurrentPlaybackContext();
    const double position = (context != nullptr ? context->getPosition() : transport.getPosition()).inSeconds();
    tap.drain([&](double time) -> std::optional<double> {
        if (!playing || time < playStartedAt)
            return std::nullopt;
        return position - (now - time); // the song ran with the input clock since
    });
}

std::vector<model::MidiEvent> Session::Impl::phrase()
{
    drainTap();
    const auto& tempo = owner.snapshot()->tempo;
    // A pause of two bars ends a phrase; while playing, what came before starting doesn't count.
    const double pause = tempo.secondsForBeats(2.0 * tempo.beatsPerBar());
    const double notBefore = std::max(consumedUntil, edit->getTransport().isPlaying() ? playStartedAt : -1e300);
    return model::lastPhrase(tap.events(), pause, notBefore, tempo.secondsForBeats(1.0));
}

bool Session::canCaptureMidi() const
{
    return !isRecording() && !impl_->phrase().empty();
}

Session::CaptureTarget Session::captureTarget(std::optional<std::uint64_t> selected) const
{
    const auto d = model::captureDestinations(*snapshot(), selected);
    if (!d.armed.empty())
        return {.tracks = d.armed};
    if (d.track)
        return {.tracks = {*d.track}};
    return {.newTrack = true};
}

std::uint64_t Session::captureMidi(std::optional<std::uint64_t> selected)
{
    if (isRecording())
        return 0;
    const auto phrase = impl_->phrase();
    if (phrase.empty())
        return 0;
    flushSnapshot();
    const auto snap = snapshot();
    const auto destinations = model::captureDestinations(*snap, selected);
    // What each track gets: an armed track what its input took; otherwise all of it.
    std::vector<std::pair<std::uint64_t, std::vector<model::MidiEvent>>> work;
    for (const auto id : destinations.armed)
        if (const auto* t = snap->channel(id))
        {
            std::vector<model::MidiEvent> taken;
            for (const auto& e : phrase)
                if (model::inputTakes(t->input, impl_->tap.sourceName(e.source), e.channel()))
                    taken.push_back(e);
            if (std::any_of(taken.begin(), taken.end(), [](const auto& e) { return e.isNoteOn(); }))
                work.emplace_back(id, std::move(taken));
        }
    if (destinations.armed.empty())
        work.emplace_back(destinations.track.value_or(0), phrase);
    if (work.empty())
        return 0;

    EditGroup group(*this, "Capture MIDI");
    const auto& tempo = snap->tempo;
    const double now = impl_->tap.now();
    std::uint64_t made = 0;
    for (auto& [trackId, events] : work)
    {
        if (trackId == 0)
            trackId = addTrack(); // nothing selected: a new track (no instrument, ADR 0033)
        auto* track = impl_->track(trackId);
        if (track == nullptr)
            continue;
        const auto placed = model::placeCapture(events, tempo, position(), now);
        const auto start = te::TimePosition::fromSeconds(placed.start), end = te::TimePosition::fromSeconds(placed.end);
        auto clip = track->insertMIDIClip(track->getName(), {start, end}, nullptr);
        if (clip == nullptr)
            continue;
        // Notes, pedal, bends and controllers at their places, on the clip's channel (tracktion
        // imports only that one); notes still held end with the clip.
        juce::MidiMessageSequence sequence;
        const int channel = clip->getMidiChannel().getChannelNumber();
        std::set<int> held;
        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto& e = events[i];
            juce::MidiMessage m(e.bytes, e.size, placed.times[i]);
            if (m.getChannel() > 0)
                m.setChannel(std::max(1, channel));
            if (e.isNoteOn())
                held.insert(e.bytes[1]);
            else if (e.isNoteOff())
                held.erase(e.bytes[1]);
            sequence.addEvent(m);
        }
        for (const int note : held)
            sequence.addEvent(juce::MidiMessage::noteOff(std::max(1, channel), note), placed.end);
        sequence.updateMatchedPairs();
        clip->getSequence().importMidiSequence(sequence, impl_->edit.get(), start, &impl_->edit->getUndoManager());
        impl_->clearUnder(*track, placed.start, placed.end, {clip->itemID.getRawID()});
        if (made == 0)
            made = clip->itemID.getRawID();
    }
    impl_->consumedUntil = now; // never captured twice
    return made;
}
} // namespace fd::engine
