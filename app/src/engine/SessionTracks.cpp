// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md): renaming,
// colours, deleting with what only made sense with it, moving, duplicating, the clip clipboard.

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

void Session::setClipColour(const std::vector<std::uint64_t>& clips, std::optional<int> colour)
{
    if (colour && (*colour < 0 || *colour >= Impl::paletteSize))
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
        if (colour)
            c->state.setProperty(Impl::colourPropertyId, *colour, um);
        else
            c->state.removeProperty(Impl::colourPropertyId, um); // its track's again
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
            p->setSidechainSourceID({});
    if (isReturn)
        for (auto* other : impl_->tracks())
            if (auto* send = sendIn(other->pluginList, busOf(*t)))
                send->removeFromParent();
    if (impl_->midiTarget == channelId)
        impl_->midiTarget.reset();
    impl_->edit->deleteTrack(t);
    return true;
}

bool Session::moveTrack(std::uint64_t channelId, std::uint64_t before)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr || before == channelId)
        return false;
    const bool isReturn = Impl::isReturn(*t);
    const auto kind = isReturn ? impl_->returns() : impl_->tracks();
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
    // After the one before its place among its kind; at the front, after what comes before the
    // first of its kind now (another kind of track, or for returns the last track).
    te::Track* preceding =
        at > 0 ? others[at - 1] : (others.empty() ? nullptr : trackBefore(*impl_->edit, *others.front(), t));
    impl_->beginEdit("Move track");
    impl_->edit->moveTrack(t, te::TrackInsertPoint(nullptr, preceding));
    return true;
}

bool Session::moveLane(std::uint64_t lane, std::uint64_t after, std::uint64_t beforeLane)
{
    auto tree = impl_->laneTree(lane);
    if (!tree.isValid() || beforeLane == lane || (after != 0 && impl_->track(after) == nullptr))
        return false;
    auto lanes = impl_->lanesTree();
    // Its index among the lanes: before `beforeLane`, or after the last lane sitting after `after`.
    int index = -1;
    if (beforeLane != 0)
    {
        const auto target = impl_->laneTree(beforeLane);
        if (!target.isValid())
            return false;
        index = lanes.indexOf(target);
    }
    else
        for (int i = 0; i < lanes.getNumChildren(); ++i)
            if (lanes.getChild(i) != tree && idProperty(lanes.getChild(i)[Impl::afterId]) == after)
                index = i + 1;
    const int now = lanes.indexOf(tree);
    if (index < 0)
        index = lanes.getNumChildren();
    if (index > now)
        --index; // moving it out first shifts the ones after it
    if (idProperty(tree[Impl::afterId]) == after && index == now)
        return false;
    impl_->beginEdit("Move automation lane");
    auto* um = &impl_->edit->getUndoManager();
    tree.setProperty(Impl::afterId, idVar(after), um);
    lanes.moveChild(now, std::clamp(index, 0, lanes.getNumChildren() - 1), um);
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

Session::PasteResult Session::pasteClips(const ClipClipboard& clipboard, std::uint64_t trackId, double at)
{
    PasteResult result;
    if (clipboard.empty())
        return result;
    const auto& data = *clipboard.data_;
    const auto tracks = impl_->tracks();
    const auto from = std::find_if(tracks.begin(), tracks.end(),
                                   [trackId](auto* t) { return t->itemID.getRawID() == trackId; });
    if (from == tracks.end())
        return result;
    std::vector<model::PasteItem> items;
    for (const auto& c : data.clips)
        items.push_back({c.row, c.offset});
    const auto [placed, leftOut] = model::pastePlacement(
        items, tracks.size(), static_cast<std::size_t>(from - tracks.begin()), std::max(0.0, at));
    result.rowsLeftOut = leftOut;
    if (placed.empty())
        return result;

    impl_->beginEdit("Paste");
    auto* um = &impl_->edit->getUndoManager();
    const double delta = std::max(0.0, at) - data.start;
    const auto first = static_cast<std::size_t>(from - tracks.begin());
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
        impl_->clearUnder(*track, start, end, pasted);
        involved.insert(track);
    }
    for (auto* track : involved)
        impl_->settle(*track, um);
    return result;
}
} // namespace fd::engine
