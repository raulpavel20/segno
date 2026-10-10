// Take lanes and Capture MIDI (M2 step C5b, ADR 0031, docs/design/recording.md §6, §8): every pass
// and loop lap kept under its track; the main lane plays plain clips made from them.

#include "engine/SessionImpl.h"

#include "model/NoteEditing.h"

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

model::TakeInfo Session::Impl::takeInfo(const juce::ValueTree& take, int colour) const
{
    model::TakeInfo info{.id = idOf(take), .name = take[te::IDs::name].toString().toStdString(), .star = take[starId]};
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
    const auto pos = clip->getPosition();
    clearUnder(track, pos.getStart().inSeconds(), pos.getEnd().inSeconds(), {clip->itemID.getRawID()});
    return clip;
}

void Session::Impl::fillFromTake(te::AudioTrack& track, const juce::ValueTree& take)
{
    const auto stack = take.getParent();
    for (auto clip : take)
        useTakeClip(track, clip, stack[fromId], stack[toId]);
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
        rightTake.copyPropertiesFrom(take, nullptr); // its name and star
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
    std::map<std::uint64_t, std::uint64_t> stackOfClip; // take clip -> stack
    for (auto s : track.state.getChildWithName(takesId))
        for (auto take : s)
            for (auto clip : take)
                stackOfClip[idOf(clip)] = idOf(s);
    std::vector<model::ClipPlace> out;
    for (auto* c : track.getClips())
    {
        const auto from = idProperty(c->state[fromTakeId]);
        const auto it = stackOfClip.find(from);
        const auto pos = c->getPosition();
        out.push_back({.clip = c->itemID.getRawID(),
                       .track = track.itemID.getRawID(),
                       .stack = from == 0 ? 0 : it != stackOfClip.end() ? it->second : kDangling,
                       .start = pos.getStart().inSeconds(),
                       .length = pos.getLength().inSeconds(),
                       .offset = pos.getOffset().inSeconds()});
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
        // What the pass recorded over: a clip with takes joins them; a plain clip is kept as a take.
        std::vector<juce::ValueTree> joined, covered;
        for (auto* other : track.getClips())
        {
            if (pass.contains(other->itemID.getRawID()) || !other->getPosition().time.overlaps(range))
                continue;
            if (auto s = stackOf(*other); s.isValid())
            {
                if (std::find(joined.begin(), joined.end(), s) == joined.end())
                    joined.push_back(s);
            }
            else if (auto trimmed = trimmedState(other->state, a, b); trimmed.isValid())
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
        const int n = model::nextTakeNumber(namesOf(stack));
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
                out.push_back(placed->itemID.getRawID());
        if (laps.size() > 1) // several takes to choose from: they open under the clip (view state)
            openStack(track, stack, true);
    }
    settle(track, um);
    return out;
}

// ---- Session -------------------------------------------------------------------------------

void Session::setTakesOpen(std::uint64_t stackId, bool open)
{
    if (auto [track, stack] = impl_->findStack(stackId); track != nullptr)
        impl_->openStack(*track, stack, open);
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

bool Session::stepTake(std::uint64_t clipId, int direction)
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
    const auto next = model::stepTake(info.takes, model::playingTake(info, main), info.start, info.end, direction);
    if (!next)
        return false;
    impl_->beginEdit("Use take");
    impl_->fillFromTake(*t, stack.getChild(static_cast<int>(*next)));
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
    impl_->fillFromTake(*t, take);
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
        te::AutomationCurve::removeAllAutomationCurvesRecursively(track->state);
        track->state.setProperty(Impl::armedId, false, nullptr);
        track->setName(t->getName() + " " + take[te::IDs::name].toString());
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
