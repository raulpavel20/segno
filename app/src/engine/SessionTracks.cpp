// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md): renaming,
// colours, deleting with what only made sense with it, moving, duplicating, the clip clipboard.

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/TrackEdits.h"

namespace fd::engine
{
namespace
{
std::uint64_t idProperty(const juce::var& v)
{
    return static_cast<std::uint64_t>(static_cast<juce::int64>(v));
}

juce::var idVar(std::uint64_t id)
{
    return static_cast<juce::int64>(id);
}

/// The track right before `track` in the edit, of any kind, leaving `skip` out.
te::Track* trackBefore(te::Edit& edit, const te::Track& track, const te::Track* skip)
{
    te::Track* before = nullptr;
    for (auto* t : te::getAllTracks(edit))
    {
        if (t == &track)
            return before;
        if (t != skip)
            before = t;
    }
    return before;
}
} // namespace

int Session::Impl::freeBus() const
{
    std::set<int> used;
    for (auto* r : returns())
        used.insert(busOf(*r));
    int bus = 0;
    while (used.contains(bus))
        ++bus;
    return bus;
}

// ---- names and colours ----------------------------------------------------------------------

bool Session::renameTrack(std::uint64_t channelId, const std::string& name)
{
    auto* t = impl_->channelTrack(channelId);
    const auto trimmed = juce::String(name).trim();
    if (t == nullptr || trimmed.isEmpty() || t->getName() == trimmed)
        return false;
    impl_->beginEdit("Rename track");
    t->setName(trimmed);
    if (auto* back = dynamic_cast<te::AudioTrack*>(t); back != nullptr && Impl::isReturn(*back))
        impl_->edit->setAuxBusName(busOf(*back), trimmed); // the sends' bus is named after it (ADR 0026)
    return true;
}

bool Session::setTrackColour(std::uint64_t channelId, int colour)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr || colour < 0 || colour >= Impl::paletteSize)
        return false;
    if (t->state.hasProperty(Impl::colourPropertyId) &&
        static_cast<int>(t->state[Impl::colourPropertyId]) == colour)
        return false;
    impl_->beginEdit("Track colour");
    t->state.setProperty(Impl::colourPropertyId, colour, &impl_->edit->getUndoManager());
    return true;
}

bool Session::setLaneColour(std::uint64_t lane, int colour)
{
    auto tree = impl_->laneTree(lane);
    if (!tree.isValid() || colour < 0 || colour >= Impl::paletteSize ||
        static_cast<int>(tree[Impl::colourId]) == colour)
        return false;
    impl_->beginEdit("Lane colour");
    tree.setProperty(Impl::colourId, colour, &impl_->edit->getUndoManager());
    return true;
}

void Session::renameClips(const std::vector<std::uint64_t>& clips, const std::string& name)
{
    const auto trimmed = juce::String(name).trim();
    if (trimmed.isEmpty())
        return;
    std::vector<te::Clip*> found;
    for (const auto id : clips)
        if (auto* c = impl_->clip(id); c != nullptr && c->getName() != trimmed)
            found.push_back(c);
    if (found.empty())
        return;
    impl_->beginEdit(found.size() > 1 ? "Rename clips" : "Rename clip");
    for (auto* c : found)
        c->setName(trimmed);
}

void Session::setClipColour(const std::vector<std::uint64_t>& clips, int colour)
{
    if (colour < 0 || colour >= Impl::paletteSize)
        return;
    std::vector<te::Clip*> found;
    for (const auto id : clips)
        if (auto* c = impl_->clip(id))
            found.push_back(c);
    if (found.empty())
        return;
    impl_->beginEdit("Clip colour");
    auto* um = &impl_->edit->getUndoManager();
    for (auto* c : found)
    {
        c->state.setProperty(Impl::colourPropertyId, colour, um);
        // Its takes too: switching takes plays copies of them (ADR 0043).
        for (auto take : impl_->stackOf(*c))
            for (auto takeClip : take)
                takeClip.setProperty(Impl::colourPropertyId, colour, um);
    }
}

// ---- deleting, moving, duplicating -------------------------------------------------------------

bool Session::deleteTrack(std::uint64_t channelId, std::string* why)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr)
        return false;
    if (isRecording())
    {
        if (why != nullptr)
            *why = "Stop recording first: tracks can't be deleted while a pass records";
        return false;
    }
    const bool isReturn = Impl::isReturn(*t);
    const EditGroup step(*this, isReturn ? "Delete return track" : "Delete track");
    impl_->deleteChannel(channelId); // a group with everything in it (groups.md §5.4)
    impl_->removeEmptyGroups();      // its group, if it was the last in it (§4)
    return true;
}

void Session::Impl::deleteChannel(std::uint64_t channelId)
{
    auto* t = channelTrack(channelId);
    if (t == nullptr)
        return;
    if (auto* g = group(channelId)) // what's in it first
    {
        std::vector<std::uint64_t> inside;
        for (auto* child : g->getAllSubTracks(false))
            inside.push_back(child->itemID.getRawID());
        for (const auto id : inside)
            deleteChannel(id);
        t = channelTrack(channelId);
    }
    owner.updateSnapshotNow();
    const auto plan = model::deletionPlan(*snapshot, channelId);
    const bool isReturnTrack = isReturn(*t);
    auto* um = &edit->getUndoManager();
    // Its automation (B41): the lanes that drive only it, its assignments in the others; the
    // lanes left keep their places, after the row now above them (ADR 0042).
    for (const auto lane : plan.lanes)
        if (auto tree = laneTree(lane); tree.isValid())
            tree.getParent().removeChild(tree, um);
    for (const auto& a : plan.assignments)
        if (auto tree = laneTree(a.lane); tree.isValid())
            for (int i = tree.getNumChildren(); --i >= 0;)
                if (const auto assign = tree.getChild(i);
                    assign.hasType(assignId) && targetIn(assign) == ParamTarget{a.channel, a.key})
                    tree.removeChild(i, um);
    if (!isReturnTrack)
        placeRows(model::withoutRow(rowTree(), channelId), nullptr, nullptr, um);
    // The sidechains it feeds go back to Off (ADR 0025); the sends to a return go.
    for (const auto device : plan.sidechains)
        if (auto* p = plugin(device))
        {
            if (auto* native = dynamic_cast<DevicePlugin*>(p); native != nullptr && native->isMidiEffect())
                native->setListenTo({}, um); // a MIDI effect's Listen (midi-effects.md §5.3)
            else
                p->setSidechainSourceID({});
        }
    if (auto* back = dynamic_cast<te::AudioTrack*>(t); back != nullptr && isReturnTrack)
        for (auto* other : rowTracks())
            if (auto* send = sendIn(other->pluginList, busOf(*back)))
                send->removeFromParent();
    if (midiTarget == channelId)
        midiTarget.reset();
    edit->deleteTrack(t);
}

void Session::Impl::removeEmptyGroups()
{
    // A group is never empty (groups.md §4): one left with nothing in it goes, as a track goes;
    // its own group may be left empty in turn. The views are told, for the toast that says so.
    std::vector<std::string> names;
    for (bool again = true; again;)
    {
        again = false;
        for (auto* t : rowTracks())
            if (auto* g = dynamic_cast<te::FolderTrack*>(t);
                g != nullptr && isGroup(*g) && g->getAllSubTracks(false).isEmpty())
            {
                names.push_back(g->getName().toStdString());
                deleteChannel(g->itemID.getRawID());
                again = true;
                break;
            }
    }
    if (!names.empty() && owner.onGroupsEmptied)
        owner.onGroupsEmptied(names);
}

std::uint64_t Session::Impl::groupOf(const te::Track& track)
{
    const auto* parent = track.getParentFolderTrack();
    return parent != nullptr ? parent->itemID.getRawID() : 0;
}

model::RowTree Session::Impl::rowTree() const
{
    model::RowTree tree;
    std::map<std::uint64_t, std::uint64_t> groups;
    for (auto* t : rowTracks())
    {
        const auto id = t->itemID.getRawID();
        tree.channels.push_back({.id = id, .group = groupOf(*t), .isGroup = isGroup(*t)});
        groups[id] = groupOf(*t);
    }
    // A lane's group: its own where it says one (first in a group), else its row's.
    for (const auto& lane : lanesTree())
    {
        const auto after = idProperty(lane[afterId]);
        const auto anchor = groups.find(after);
        tree.lanes.push_back({.id = idOf(lane),
                              .after = after,
                              .group = lane.hasProperty(laneGroupId) ? idProperty(lane[laneGroupId])
                                       : anchor != groups.end()      ? anchor->second
                                                                     : 0});
    }
    return tree;
}

void Session::Impl::placeRows(const model::RowTree& tree, const model::RowMove* move, te::Track* moved,
                              juce::UndoManager* um)
{
    if (moved != nullptr && move != nullptr)
    {
        // After the channel it now follows in its group; first in a group, first there; first of
        // all, after what comes before the first track now (another kind of track).
        auto* parent = move->group != 0 ? group(move->group) : nullptr;
        te::Track* preceding = move->after != 0 ? channelTrack(move->after) : nullptr;
        if (parent == nullptr && preceding == nullptr)
            if (const auto all = rowTracks(); !all.empty())
                preceding = trackBefore(
                    *edit, all.front() == moved && all.size() > 1 ? *all[1] : *all.front(), moved);
        edit->moveTrack(moved, te::TrackInsertPoint(parent, preceding));
    }
    // Each lane after its row in its group, the lanes in row order; its group kept only where it
    // isn't the group of the row it's after.
    std::map<std::uint64_t, std::uint64_t> groups;
    for (const auto& c : tree.channels)
        groups[c.id] = c.group;
    auto lanes = lanesTree();
    for (int i = 0; i < static_cast<int>(tree.lanes.size()); ++i)
    {
        const auto& l = tree.lanes[static_cast<size_t>(i)];
        auto lane = laneTree(l.id);
        if (!lane.isValid())
            continue;
        if (idProperty(lane[afterId]) != l.after)
            lane.setProperty(afterId, idVar(l.after), um);
        const auto anchor = groups.find(l.after);
        const auto implied = l.after != 0 && anchor != groups.end() ? anchor->second : 0;
        if (l.group != implied)
        {
            if (idProperty(lane[laneGroupId]) != l.group || !lane.hasProperty(laneGroupId))
                lane.setProperty(laneGroupId, idVar(l.group), um);
        }
        else if (lane.hasProperty(laneGroupId))
            lane.removeProperty(laneGroupId, um);
        if (const int now = lanes.indexOf(lane); now != i)
            lanes.moveChild(now, i, um);
    }
}

bool Session::moveTrack(std::uint64_t channelId, std::uint64_t before, std::optional<std::uint64_t> into)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr || before == channelId)
        return false;
    if (!Impl::isReturn(*t))
    {
        // Right above a row, in a group or in none; everything else stays where it is (ADR 0042,
        // groups.md §5.3). A group it leaves empty goes, in the same step.
        const auto move = model::moveInTree(impl_->rowTree(), channelId, before, into);
        if (!move)
            return false;
        const EditGroup step(*this, "Move track");
        impl_->placeRows(move->tree, &*move, t, &impl_->edit->getUndoManager());
        impl_->removeEmptyGroups();
        return true;
    }
    // A return among the returns.
    const auto kind = impl_->returns();
    std::vector<te::AudioTrack*> others;
    for (auto* k : kind)
        if (k != t)
            others.push_back(k);
    auto at = others.size();
    if (before != 0)
    {
        const auto it = std::find_if(others.begin(), others.end(),
                                     [before](auto* k) { return k->itemID.getRawID() == before; });
        if (it == others.end())
            return false;
        at = static_cast<std::size_t>(it - others.begin());
    }
    // Where it is now: no move.
    const auto now = static_cast<std::size_t>(
        std::find_if(kind.begin(), kind.end(), [t](auto* k) { return k == t; }) - kind.begin());
    if (now == at)
        return false;
    // After the one before its place among the returns; first, after what comes before the first
    // return now (the last track).
    te::Track* preceding =
        at > 0 ? others[at - 1] : (others.empty() ? nullptr : trackBefore(*impl_->edit, *others.front(), t));
    impl_->beginEdit("Move track");
    impl_->edit->moveTrack(t, te::TrackInsertPoint(nullptr, preceding));
    return true;
}

bool Session::moveLane(std::uint64_t lane, std::uint64_t before, std::optional<std::uint64_t> into)
{
    if (!impl_->laneTree(lane).isValid())
        return false;
    // Right above a row, in a group or in none; everything else stays where it is (ADR 0042).
    const auto move = model::moveInTree(impl_->rowTree(), lane, before, into);
    if (!move)
        return false;
    impl_->beginEdit("Move automation lane");
    impl_->placeRows(move->tree, nullptr, nullptr, &impl_->edit->getUndoManager());
    return true;
}

std::uint64_t Session::duplicateTrack(std::uint64_t channelId)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr || isRecording())
        return 0;
    const bool isReturn = Impl::isReturn(*t);
    const int bus = isReturn ? impl_->freeBus() : -1;
    if (isReturn && bus >= Impl::maxReturns)
        return 0;
    updateSnapshotNow();
    const auto snapshot = impl_->snapshot;
    impl_->beginEdit(isReturn ? "Duplicate return track" : "Duplicate track");
    auto* um = &impl_->edit->getUndoManager();

    // A copy of its state with new ids (a group's with everything in it); a clip's take points at
    // its copy's (ADR 0032).
    t->flushStateToValueTree();
    std::vector<std::uint64_t> originals{channelId}; // it, and what's in it
    if (auto* g = impl_->group(channelId))
        for (auto* inside : g->getAllSubTracks(true))
        {
            inside->flushStateToValueTree();
            originals.push_back(inside->itemID.getRawID());
        }
    auto copy = t->state.createCopy();
    te::EditItemID::IDMap ids;
    te::EditItemID::remapIDs(copy, nullptr, *impl_->edit, &ids);
    impl_->copyRacksIn(copy, ids); // a rack of its own each, not the original's (racks.md)
    std::map<std::uint64_t, std::uint64_t> map;
    for (const auto& [from, to] : ids)
        map[from.getRawID()] = to.getRawID();
    std::function<void(juce::ValueTree)> repoint = [&](juce::ValueTree v)
    {
        if (v.hasProperty(Impl::fromTakeId))
        {
            if (const auto it = map.find(idProperty(v[Impl::fromTakeId])); it != map.end())
                v.setProperty(Impl::fromTakeId, idVar(it->second), nullptr);
            else
                v.removeProperty(Impl::fromTakeId, nullptr);
        }
        for (auto child : v)
            repoint(child);
    };
    repoint(copy);
    // "Bass 2", and in a group's copy "Kick 2"...; not armed (two tracks would record one input
    // twice), not soloed.
    std::vector<std::string> names;
    for (const auto* list : {&snapshot->tracks, &snapshot->returns})
        for (const auto& c : *list)
            names.push_back(c.name);
    std::function<void(juce::ValueTree)> rename = [&](juce::ValueTree v)
    {
        if (v.hasType(te::IDs::TRACK) || v.hasType(te::IDs::FOLDERTRACK))
        {
            const auto name = model::nextCopyName(v[te::IDs::name].toString().toStdString(), names);
            names.push_back(name);
            v.setProperty(te::IDs::name, juce::String(name), nullptr);
            v.removeProperty(Impl::armedId, nullptr);
            v.setProperty(te::IDs::solo, false, nullptr);
        }
        for (auto child : v)
            rename(child);
    };
    rename(copy);
    const auto name = copy[te::IDs::name].toString();

    // Right after it, in its group (groups.md §5.4).
    auto inserted =
        impl_->edit->insertTrack(te::TrackInsertPoint(t->getParentFolderTrack(), t), copy, nullptr);
    if (inserted == nullptr)
        return 0;
    const auto id = inserted->itemID.getRawID();
    if (auto* back = dynamic_cast<te::AudioTrack*>(inserted.get()); back != nullptr && isReturn)
    {
        // Its own bus: the sends to the original stay its.
        if (auto* in = findIn<te::AuxReturnPlugin>(back->pluginList))
            in->busNumber = bus;
        impl_->edit->setAuxBusName(bus, name);
    }
    // The lanes that drive only the original (or only one of a group's tracks): copies after the
    // copy, driving it (modules by their copies' ids).
    for (const auto original : originals)
    {
        const auto copied = map.find(original);
        if (copied == map.end())
            continue;
        for (const auto laneId : model::lanesOnlyFor(*snapshot, original))
        {
            const auto lane = impl_->laneTree(laneId);
            if (!lane.isValid())
                continue;
            auto laneCopy = lane.createCopy();
            impl_->edit->createNewItemID().writeID(laneCopy, nullptr);
            laneCopy.setProperty(Impl::nameId,
                                 juce::String(model::laneCopyName(lane[Impl::nameId].toString().toStdString(),
                                                                  impl_->laneNames())),
                                 nullptr); // names never repeat (tracks-and-clips.md §18)
            laneCopy.setProperty(Impl::afterId, idVar(copied->second), nullptr);
            laneCopy.removeProperty(Impl::laneGroupId, nullptr);
            for (auto assign : laneCopy)
                if (assign.hasType(Impl::assignId))
                {
                    assign.setProperty(Impl::assignChannelId, idVar(copied->second), nullptr);
                    auto key = assign[Impl::keyId].toString();
                    if (key.startsWith("m")) // "m<module>/<parameter>"
                        if (const auto it = map.find(
                                static_cast<std::uint64_t>(key.fromFirstOccurrenceOf("m", false, false)
                                                               .upToFirstOccurrenceOf("/", false, false)
                                                               .getLargeIntValue()));
                            it != map.end())
                            assign.setProperty(Impl::keyId,
                                               "m" + juce::String(static_cast<juce::int64>(it->second)) +
                                                   "/" + key.fromFirstOccurrenceOf("/", false, false),
                                               nullptr);
                }
            impl_->lanesTree().addChild(laneCopy, -1, um);
            impl_->derive(laneCopy, um);
        }
    }
    return id;
}

// ---- the clip clipboard ------------------------------------------------------------------------

struct Session::ClipClipboard::Data
{
    struct Clip
    {
        juce::ValueTree state;
        int row = 0;
        double offset = 0.0; // after the earliest
    };
    struct Stack
    {
        juce::ValueTree tree;
        int row = 0;
    };
    std::vector<Clip> clips;
    std::vector<Stack> stacks; // the takes they play from (ADR 0032)
    double start = 0.0;        // the earliest clip's start
    int rows = 0;
};

bool Session::ClipClipboard::empty() const noexcept
{
    return data_ == nullptr || data_->clips.empty();
}

int Session::ClipClipboard::rows() const noexcept
{
    return data_ == nullptr ? 0 : data_->rows;
}

Session::ClipClipboard Session::copyClips(const std::vector<std::uint64_t>& asked) const
{
    ClipClipboard out;
    const auto tracks = impl_->tracks();
    struct Found
    {
        te::Clip* clip;
        int row;
    };
    std::vector<Found> found;
    std::set<std::uint64_t> seen;
    auto add = [&](te::Clip* c)
    {
        auto* track = dynamic_cast<te::AudioTrack*>(c->getClipTrack());
        const auto it = std::find(tracks.begin(), tracks.end(), track);
        if (it != tracks.end() && seen.insert(c->itemID.getRawID()).second)
            found.push_back({c, static_cast<int>(it - tracks.begin())});
    };
    for (const auto id : asked)
        if (auto* c = impl_->clip(id))
        {
            add(c);
            // Every part of a clip with takes: it's copied whole.
            if (auto* track = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                if (const auto stack = impl_->stackOf(*c); stack.isValid())
                    for (auto* part : impl_->partsOf(*track, stack))
                        add(part);
        }
    if (found.empty())
        return out;
    auto data = std::make_shared<ClipClipboard::Data>();
    int top = found.front().row, bottom = top;
    data->start = found.front().clip->getPosition().getStart().inSeconds();
    for (const auto& f : found)
    {
        top = std::min(top, f.row);
        bottom = std::max(bottom, f.row);
        data->start = std::min(data->start, f.clip->getPosition().getStart().inSeconds());
    }
    data->rows = bottom - top + 1;
    std::set<std::uint64_t> stacks;
    for (const auto& f : found)
    {
        f.clip->flushStateToValueTree();
        data->clips.push_back({f.clip->state.createCopy(), f.row - top,
                               f.clip->getPosition().getStart().inSeconds() - data->start});
        if (const auto stack = impl_->stackOf(*f.clip);
            stack.isValid() && stacks.insert(Impl::idOf(stack)).second)
            data->stacks.push_back({stack.createCopy(), f.row - top});
    }
    out.data_ = std::move(data);
    return out;
}

Session::ClipClipboard Session::copyClipsAcross(double start, double end, double from,
                                                const std::set<std::uint64_t>* only) const
{
    ClipClipboard out;
    auto data = std::make_shared<ClipClipboard::Data>();
    data->start = from;
    const auto tracks = impl_->tracks();
    // Rows from the first track, or from the topmost of `only`.
    std::size_t top = 0;
    if (only != nullptr)
        while (top < tracks.size() && !only->contains(tracks[top]->itemID.getRawID()))
            ++top;
    std::set<std::uint64_t> stacks;
    for (std::size_t index = top; index < tracks.size(); ++index)
    {
        if (only != nullptr && !only->contains(tracks[index]->itemID.getRawID()))
            continue;
        const auto row = static_cast<int>(index - top);
        for (auto* c : tracks[index]->getClips())
        {
            const auto range = c->getPosition().time;
            if (range.getStart().inSeconds() >= end - 1e-6 || range.getEnd().inSeconds() <= start + 1e-6)
                continue;
            c->flushStateToValueTree();
            data->clips.push_back({c->state.createCopy(), row, range.getStart().inSeconds() - from});
            data->rows = row + 1;
            if (const auto stack = impl_->stackOf(*c);
                stack.isValid() && stacks.insert(Impl::idOf(stack)).second)
                data->stacks.push_back({stack.createCopy(), row});
        }
    }
    if (!data->clips.empty())
        out.data_ = std::move(data);
    return out;
}

Session::PasteResult Session::pasteClips(const ClipClipboard& clipboard, std::uint64_t trackId, double at)
{
    const auto tracks = impl_->tracks();
    const auto from = std::find_if(tracks.begin(), tracks.end(),
                                   [trackId](auto* t) { return t->itemID.getRawID() == trackId; });
    if (clipboard.empty() || from == tracks.end())
        return {};
    return placeClips(clipboard, static_cast<std::size_t>(from - tracks.begin()), std::max(0.0, at), true);
}

Session::PasteResult Session::placeClips(const ClipClipboard& clipboard, std::size_t first, double at,
                                         bool cutUnder)
{
    PasteResult result;
    if (clipboard.empty())
        return result;
    const auto& data = *clipboard.data_;
    const auto tracks = impl_->tracks();
    std::vector<model::PasteItem> items;
    for (const auto& c : data.clips)
        items.push_back({c.row, c.offset});
    const auto [placed, leftOut] = model::pastePlacement(items, tracks.size(), first, at);
    result.rowsLeftOut = leftOut;
    if (placed.empty())
        return result;

    impl_->beginEdit("Paste");
    auto* um = &impl_->edit->getUndoManager();
    const double delta = at - data.start;
    // The takes first, each on its row's track; then the clips, playing from their copies.
    std::map<std::uint64_t, std::uint64_t> takeClips;
    for (const auto& s : data.stacks)
        if (const auto row = first + static_cast<std::size_t>(s.row); row < tracks.size())
            impl_->copyStack(s.tree, *tracks[row], delta, takeClips, um);
    std::vector<std::tuple<te::AudioTrack*, double, double>> spans;
    for (const auto& p : placed)
    {
        auto copy = data.clips[p.item].state.createCopy();
        te::EditItemID::remapIDs(copy, nullptr, *impl_->edit);
        if (copy.hasProperty(Impl::fromTakeId))
        {
            if (const auto it = takeClips.find(idProperty(copy[Impl::fromTakeId])); it != takeClips.end())
                copy.setProperty(Impl::fromTakeId, idVar(it->second), nullptr);
            else
                copy.removeProperty(Impl::fromTakeId, nullptr);
        }
        copy.setProperty(te::IDs::start, p.start, nullptr);
        if (auto* clip = tracks[p.track]->insertClipWithState(copy))
        {
            result.clips.push_back(clip->itemID.getRawID());
            spans.emplace_back(tracks[p.track], p.start, clip->getPosition().getEnd().inSeconds());
        }
    }
    // They cut what they land on (ADR 0019).
    const std::set<std::uint64_t> pasted(result.clips.begin(), result.clips.end());
    std::set<te::AudioTrack*> involved;
    for (const auto& [track, start, end] : spans)
    {
        if (cutUnder)
            impl_->clearUnder(*track, start, end, pasted);
        involved.insert(track);
    }
    for (auto* track : involved)
        impl_->settle(*track, um);
    return result;
}
} // namespace fd::engine
