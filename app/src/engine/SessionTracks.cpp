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
    if (Impl::isReturn(*t)) // the sends' bus is named after it (ADR 0026)
        impl_->edit->setAuxBusName(busOf(*t), trimmed);
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
    updateSnapshotNow();
    const auto plan = model::deletionPlan(*impl_->snapshot, channelId);
    const bool isReturn = Impl::isReturn(*t);
    impl_->beginEdit(isReturn ? "Delete return track" : "Delete track");
    auto* um = &impl_->edit->getUndoManager();
    // Its automation (B41): the lanes that drive only it, its assignments in the others, and
    // the lanes after it sit after the track above.
    for (const auto lane : plan.lanes)
        if (auto tree = impl_->laneTree(lane); tree.isValid())
            tree.getParent().removeChild(tree, um);
    for (const auto& a : plan.assignments)
        if (auto tree = impl_->laneTree(a.lane); tree.isValid())
            for (int i = tree.getNumChildren(); --i >= 0;)
                if (const auto assign = tree.getChild(i);
                    assign.hasType(Impl::assignId) && Impl::targetIn(assign) == ParamTarget{a.channel, a.key})
                    tree.removeChild(i, um);
    for (const auto& [lane, after] : plan.reanchor)
        if (auto tree = impl_->laneTree(lane); tree.isValid())
            tree.setProperty(Impl::afterId, idVar(after), um);
    // The sidechains it feeds go back to Off (ADR 0025); the sends to a return go.
    for (const auto device : plan.sidechains)
        if (auto* p = impl_->plugin(device))
        {
            if (auto* native = dynamic_cast<DevicePlugin*>(p); native != nullptr && native->isMidiEffect())
                native->setListenTo({}, um); // a MIDI effect's Listen (midi-effects.md §5.3)
            else
                p->setSidechainSourceID({});
        }
    if (isReturn)
        for (auto* other : impl_->tracks())
            if (auto* send = sendIn(other->pluginList, busOf(*t)))
                send->removeFromParent();
    if (impl_->midiTarget == channelId)
        impl_->midiTarget.reset();
    impl_->edit->deleteTrack(t);
    return true;
}

std::vector<model::RowRef> Session::Impl::rowOrder() const
{
    const auto all = tracks();
    std::set<std::uint64_t> shown;
    for (auto* t : all)
        shown.insert(t->itemID.getRawID());
    std::vector<model::RowRef> out;
    auto lanesAfter = [&](auto anchored)
    {
        for (const auto& lane : lanesTree())
            if (anchored(idProperty(lane[afterId])))
                out.push_back({.id = idOf(lane), .lane = true});
    };
    lanesAfter([](std::uint64_t after) { return after == 0; });
    for (auto* t : all)
    {
        const auto id = t->itemID.getRawID();
        out.push_back({.id = id});
        lanesAfter([id](std::uint64_t after) { return after == id; });
    }
    lanesAfter([&shown](std::uint64_t after) { return after != 0 && !shown.contains(after); });
    return out;
}

void Session::Impl::placeRows(const model::RowOrder& order, te::Track* moved, juce::UndoManager* um)
{
    if (moved != nullptr)
    {
        // After the track before it in the new order; first, after what comes before the first
        // track now (another kind of track).
        const auto id = moved->itemID.getRawID();
        const auto at = std::find(order.tracks.begin(), order.tracks.end(), id);
        te::Track* preceding = nullptr;
        if (at != order.tracks.begin() && at != order.tracks.end())
            preceding = track(*std::prev(at));
        else if (const auto all = tracks(); !all.empty())
            preceding = trackBefore(*edit, all.front() == moved && all.size() > 1 ? *all[1] : *all.front(), moved);
        edit->moveTrack(moved, te::TrackInsertPoint(nullptr, preceding));
    }
    // Each lane after its track, the lanes in row order.
    auto lanes = lanesTree();
    for (int i = 0; i < static_cast<int>(order.lanes.size()); ++i)
    {
        const auto& [id, after] = order.lanes[static_cast<size_t>(i)];
        auto lane = laneTree(id);
        if (!lane.isValid())
            continue;
        if (idProperty(lane[afterId]) != after)
            lane.setProperty(afterId, idVar(after), um);
        if (const int now = lanes.indexOf(lane); now != i)
            lanes.moveChild(now, i, um);
    }
}

bool Session::moveTrack(std::uint64_t channelId, std::uint64_t before)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr || before == channelId)
        return false;
    if (!Impl::isReturn(*t))
    {
        // Right above a track or a lane; everything else stays where it is (ADR 0042).
        const auto order = model::moveRow(impl_->rowOrder(), channelId, before);
        if (!order)
            return false;
        impl_->beginEdit("Move track");
        impl_->placeRows(*order, t, &impl_->edit->getUndoManager());
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
    const auto now = static_cast<std::size_t>(std::find(kind.begin(), kind.end(), t) - kind.begin());
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

bool Session::moveLane(std::uint64_t lane, std::uint64_t before)
{
    if (!impl_->laneTree(lane).isValid())
        return false;
    // Right above a track or a lane; everything else stays where it is (ADR 0042).
    const auto order = model::moveRow(impl_->rowOrder(), lane, before);
    if (!order)
        return false;
    impl_->beginEdit("Move automation lane");
    impl_->placeRows(*order, nullptr, &impl_->edit->getUndoManager());
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

    // A copy of its state with new ids; a clip's take points at its copy's (ADR 0032).
    t->flushStateToValueTree();
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
    // "Bass 2"; not armed (two tracks would record one input twice), not soloed.
    std::vector<std::string> names;
    for (const auto* list : {&snapshot->tracks, &snapshot->returns})
        for (const auto& c : *list)
            names.push_back(c.name);
    const auto name = juce::String(model::nextCopyName(t->getName().toStdString(), names));
    copy.setProperty(te::IDs::name, name, nullptr);
    copy.removeProperty(Impl::armedId, nullptr);
    copy.setProperty(te::IDs::solo, false, nullptr);

    auto inserted = impl_->edit->insertTrack(te::TrackInsertPoint(nullptr, t), copy, nullptr);
    auto* track = dynamic_cast<te::AudioTrack*>(inserted.get());
    if (track == nullptr)
        return 0;
    const auto id = track->itemID.getRawID();
    if (isReturn) // its own bus: the sends to the original stay its
    {
        if (auto* in = findIn<te::AuxReturnPlugin>(track->pluginList))
            in->busNumber = bus;
        impl_->edit->setAuxBusName(bus, name);
    }
    // The lanes that drive only the original: copies after the copy, driving it (modules by
    // their copies' ids).
    for (const auto laneId : model::lanesOnlyFor(*snapshot, channelId))
    {
        const auto lane = impl_->laneTree(laneId);
        if (!lane.isValid())
            continue;
        auto laneCopy = lane.createCopy();
        impl_->edit->createNewItemID().writeID(laneCopy, nullptr);
        laneCopy.setProperty(Impl::nameId,
                             juce::String(model::laneCopyName(lane[Impl::nameId].toString().toStdString(), impl_->laneNames())),
                             nullptr); // names never repeat (tracks-and-clips.md §18)
        laneCopy.setProperty(Impl::afterId, idVar(id), nullptr);
        for (auto assign : laneCopy)
            if (assign.hasType(Impl::assignId))
            {
                assign.setProperty(Impl::assignChannelId, idVar(id), nullptr);
                auto key = assign[Impl::keyId].toString();
                if (key.startsWith("m")) // "m<module>/<parameter>"
                    if (const auto it =
                            map.find(static_cast<std::uint64_t>(key.fromFirstOccurrenceOf("m", false, false)
                                                                    .upToFirstOccurrenceOf("/", false, false)
                                                                    .getLargeIntValue()));
                        it != map.end())
                        assign.setProperty(Impl::keyId,
                                           "m" + juce::String(static_cast<juce::int64>(it->second)) + "/" +
                                               key.fromFirstOccurrenceOf("/", false, false),
                                           nullptr);
            }
        impl_->lanesTree().addChild(laneCopy, -1, um);
        impl_->derive(laneCopy, um);
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
