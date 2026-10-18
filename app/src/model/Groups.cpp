#include "model/Groups.h"

#include <algorithm>

namespace fd::model
{
namespace
{
const TrackInfo* trackWithId(const Snapshot& snapshot, std::uint64_t id)
{
    for (const auto& t : snapshot.tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}
} // namespace

const TrackInfo* parentGroup(const Snapshot& snapshot, const TrackInfo& channel)
{
    if (channel.group == 0)
        return nullptr;
    const auto* g = trackWithId(snapshot, channel.group);
    return g != nullptr && g->isGroup() ? g : nullptr;
}

std::vector<const TrackInfo*> groupsAbove(const Snapshot& snapshot, const TrackInfo& channel)
{
    std::vector<const TrackInfo*> out;
    // Bounded by the number of tracks, so a malformed loop can't hang it.
    for (const auto* g = parentGroup(snapshot, channel); g != nullptr && out.size() < snapshot.tracks.size();
         g = parentGroup(snapshot, *g))
        out.insert(out.begin(), g);
    return out;
}

int groupDepth(const Snapshot& snapshot, const TrackInfo& channel)
{
    return static_cast<int>(groupsAbove(snapshot, channel).size());
}

bool isInGroup(const Snapshot& snapshot, const TrackInfo& channel, std::uint64_t group)
{
    const auto above = groupsAbove(snapshot, channel);
    return std::any_of(above.begin(), above.end(), [group](const auto* g) { return g->id == group; });
}

std::vector<const TrackInfo*> membersOf(const Snapshot& snapshot, std::uint64_t group, bool nested)
{
    std::vector<const TrackInfo*> out;
    for (const auto& t : snapshot.tracks)
        if (nested ? isInGroup(snapshot, t, group) : t.group == group)
            out.push_back(&t);
    return out;
}

int tracksIn(const Snapshot& snapshot, std::uint64_t group)
{
    const auto all = membersOf(snapshot, group, true);
    return static_cast<int>(
        std::count_if(all.begin(), all.end(), [](const auto* t) { return t->holdsClips(); }));
}

std::string tracksText(int count)
{
    return std::to_string(count) + (count == 1 ? " track" : " tracks");
}

bool hiddenByFolding(const Snapshot& snapshot, const TrackInfo& channel,
                     const std::set<std::uint64_t>& folded)
{
    if (folded.empty())
        return false;
    const auto above = groupsAbove(snapshot, channel);
    return std::any_of(above.begin(), above.end(), [&](const auto* g) { return folded.contains(g->id); });
}

std::vector<std::uint64_t> outermost(const Snapshot& snapshot, const std::vector<std::uint64_t>& channels)
{
    std::vector<std::uint64_t> out;
    for (const auto id : channels)
    {
        const auto* t = snapshot.channel(id);
        if (t == nullptr || std::none_of(channels.begin(), channels.end(), [&](std::uint64_t g)
                                         { return g != id && isInGroup(snapshot, *t, g); }))
            out.push_back(id);
    }
    return out;
}

std::set<std::uint64_t> toggledFold(const Snapshot& snapshot, const std::set<std::uint64_t>& folded,
                                    std::uint64_t group, bool all)
{
    auto out = folded;
    const bool fold = !folded.contains(group);
    for (const auto& t : snapshot.tracks)
        if (t.isGroup() && (all || t.id == group))
        {
            if (fold)
                out.insert(t.id);
            else
                out.erase(t.id);
        }
    return out;
}

std::vector<int> clipTracks(const Snapshot& snapshot)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(snapshot.tracks.size()); ++i)
        if (!snapshot.tracks[static_cast<size_t>(i)].isGroup())
            out.push_back(i);
    return out;
}

int clipTrackRank(const Snapshot& snapshot, int track)
{
    if (track < 0 || track >= static_cast<int>(snapshot.tracks.size()) ||
        snapshot.tracks[static_cast<size_t>(track)].isGroup())
        return -1;
    int rank = 0;
    for (int i = 0; i < track; ++i)
        rank += snapshot.tracks[static_cast<size_t>(i)].isGroup() ? 0 : 1;
    return rank;
}

bool holdsSolo(const Snapshot& snapshot, const TrackInfo& group)
{
    if (!group.isGroup() || group.mix.solo)
        return false;
    const auto inside = membersOf(snapshot, group.id, true);
    return std::any_of(inside.begin(), inside.end(), [](const auto* t) { return t->mix.solo; });
}

bool inMutedGroup(const Snapshot& snapshot, const TrackInfo& channel)
{
    const auto above = groupsAbove(snapshot, channel);
    return std::any_of(above.begin(), above.end(), [](const auto* g) { return g->mix.mute; });
}
} // namespace fd::model
