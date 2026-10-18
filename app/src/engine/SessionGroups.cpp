// Groups (ADR 0072, docs/design/groups.md §4, §5): tracks and groups summed into one channel, a
// submix folder track with the channel plugins every Segno channel has. Made around the tracks
// chosen, ungrouped back into their place, left by tracks; never empty.

#include "engine/SessionImpl.h"
#include "model/TrackEdits.h"

#include <algorithm>

namespace fd::engine
{
namespace
{
/// Whether `track` is inside one of `tracks` (at any depth).
bool insideAny(const te::Track& track, const std::vector<te::Track*>& tracks)
{
    for (auto* g = track.getParentFolderTrack(); g != nullptr; g = g->getParentFolderTrack())
        if (std::find(tracks.begin(), tracks.end(), g) != tracks.end())
            return true;
    return false;
}
} // namespace

std::uint64_t Session::groupTracks(const std::vector<std::uint64_t>& channels, const std::string& name)
{
    if (isRecording())
        return 0;
    // The tracks and groups asked for, in their order; one inside another asked for goes with it.
    std::vector<te::Track*> chosen;
    for (auto* t : impl_->rowTracks())
        if (std::find(channels.begin(), channels.end(), t->itemID.getRawID()) != channels.end())
            chosen.push_back(t);
    const auto all = chosen;
    std::erase_if(chosen, [&all](auto* t) { return insideAny(*t, all); });
    if (chosen.empty())
        return 0;
    auto& edit = *impl_->edit;
    const EditGroup step(*this, "Group tracks");
    auto* um = &edit.getUndoManager();
    const auto rows = impl_->rowTree(); // the lanes' places, before
    // At the topmost one's place, in its group (§5.1).
    auto* top = chosen.front();
    const auto names = impl_->trackNames();
    auto folder = edit.insertNewFolderTrack(te::TrackInsertPoint(*top, true), nullptr, true);
    if (folder == nullptr)
        return 0;
    folder->setName(juce::String::fromUTF8((name.empty() ? model::nextGroupName(names) : name).c_str()));
    folder->state.setProperty(Impl::colourPropertyId, top->state.getProperty(Impl::colourPropertyId, 0), um);
    if (auto* v = folder->getVolumePlugin())
        v->setPanLaw(te::PanLawBalance);
    te::Track* last = nullptr;
    std::vector<std::uint64_t> ids;
    for (auto* t : chosen)
    {
        edit.moveTrack(t, te::TrackInsertPoint(folder.get(), last));
        last = t;
        ids.push_back(t->itemID.getRawID());
    }
    // The lanes between them go in with them; the others stay where they were.
    impl_->placeRows(model::groupInTree(rows, ids, folder->itemID.getRawID()), nullptr, nullptr, um);
    impl_->removeEmptyGroups(); // a group they all left
    return folder->itemID.getRawID();
}

std::vector<std::uint64_t> Session::ungroupTracks(std::uint64_t groupId)
{
    auto* g = impl_->group(groupId);
    if (g == nullptr || isRecording())
        return {};
    auto& edit = *impl_->edit;
    const EditGroup step(*this, "Ungroup tracks");
    // What it holds takes its place, in order, in its group (§5.2); then it goes, as a track goes.
    std::vector<std::uint64_t> out;
    auto* parent = g->getParentFolderTrack();
    te::Track* last = nullptr;
    for (auto* t : g->getAllSubTracks(false))
    {
        edit.moveTrack(t,
                       last != nullptr ? te::TrackInsertPoint(parent, last) : te::TrackInsertPoint(*g, true));
        last = t;
        out.push_back(t->itemID.getRawID());
    }
    impl_->deleteChannel(groupId);
    return out;
}

bool Session::removeFromGroup(const std::vector<std::uint64_t>& channels)
{
    if (isRecording())
        return false;
    std::vector<te::Track*> leaving;
    for (auto* t : impl_->rowTracks())
        if (std::find(channels.begin(), channels.end(), t->itemID.getRawID()) != channels.end() &&
            t->getParentFolderTrack() != nullptr)
            leaving.push_back(t);
    const auto all = leaving;
    std::erase_if(leaving, [&all](auto* t) { return insideAny(*t, all); });
    if (leaving.empty())
        return false;
    auto& edit = *impl_->edit;
    const EditGroup step(*this, "Remove from group");
    // Each right after its group, in the group above it; several from one group in their order.
    std::map<te::Track*, te::Track*> lastAfter; // by group: the last placed after it
    for (auto* t : leaving)
    {
        auto* group = t->getParentFolderTrack();
        auto& after = lastAfter[group];
        edit.moveTrack(t,
                       te::TrackInsertPoint(group->getParentFolderTrack(), after != nullptr ? after : group));
        after = t;
    }
    impl_->removeEmptyGroups();
    return true;
}

bool Session::colourMembers(std::uint64_t groupId)
{
    auto* g = impl_->group(groupId);
    if (g == nullptr)
        return false;
    const auto colour = g->state.getProperty(Impl::colourPropertyId, 0);
    std::vector<te::Track*> members;
    for (auto* t : g->getAllSubTracks(true))
        if (dynamic_cast<te::AudioTrack*>(t) != nullptr || Impl::isGroup(*t))
            members.push_back(t);
    const auto differs = [&](const juce::ValueTree& v)
    { return v.getProperty(Impl::colourPropertyId) != colour; };
    bool any = false;
    for (auto* t : members)
    {
        any = any || differs(t->state);
        if (auto* track = dynamic_cast<te::AudioTrack*>(t))
            for (auto* c : track->getClips())
                any = any || differs(c->state);
    }
    if (!any)
        return false;
    impl_->beginEdit("Track colour"); // groups.md §12
    auto* um = &impl_->edit->getUndoManager();
    for (auto* t : members)
    {
        t->state.setProperty(Impl::colourPropertyId, colour, um);
        if (auto* track = dynamic_cast<te::AudioTrack*>(t))
            for (auto* c : track->getClips())
            {
                c->state.setProperty(Impl::colourPropertyId, colour, um);
                // Its takes too, as a clip's colour does (ADR 0043).
                for (auto take : impl_->stackOf(*c))
                    for (auto takeClip : take)
                        takeClip.setProperty(Impl::colourPropertyId, colour, um);
            }
    }
    return true;
}
} // namespace fd::engine
