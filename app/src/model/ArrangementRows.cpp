#include "model/ArrangementRows.h"

#include "model/Groups.h"
#include "model/RowTree.h"

#include <algorithm>
#include <map>

namespace fd::model
{
std::string ArrangementRow::key() const
{
    return isLane() ? "a" + std::to_string(lane) : "c" + std::to_string(channel);
}

RowTree rowTreeOf(const Snapshot& snapshot)
{
    RowTree tree;
    std::map<std::uint64_t, std::uint64_t> groupOf;
    for (const auto& t : snapshot.tracks)
    {
        tree.channels.push_back({.id = t.id, .group = t.group, .isGroup = t.isGroup()});
        groupOf[t.id] = t.group;
    }
    // A lane is in the group of the row it's after, unless it says another (first in a group).
    for (const auto& l : snapshot.automationLanes)
    {
        const auto anchor = groupOf.find(l.after);
        tree.lanes.push_back({.id = l.id,
                              .after = l.after,
                              .group = l.group != 0              ? l.group
                                       : anchor != groupOf.end() ? anchor->second
                                                                 : 0});
    }
    return tree;
}

std::vector<ArrangementRow> arrangementRows(const Snapshot& snapshot, bool returns, bool lanes,
                                            const std::set<std::uint64_t>& folded)
{
    std::vector<ArrangementRow> rows;
    std::map<std::uint64_t, int> index;
    for (int i = 0; i < static_cast<int>(snapshot.tracks.size()); ++i)
        index[snapshot.tracks[static_cast<size_t>(i)].id] = i;
    // A folded group hides its rows: those whose group is it or inside it.
    const auto hidden = [&](std::uint64_t group)
    {
        for (std::size_t guard = 0; group != 0 && guard <= snapshot.tracks.size(); ++guard)
        {
            if (folded.contains(group))
                return true;
            const auto it = index.find(group);
            group = it != index.end() ? snapshot.tracks[static_cast<size_t>(it->second)].group : 0;
        }
        return false;
    };
    int above = snapshot.tracks.empty() ? -1 : 0; // a lane's track: the one above it (the first at the top)
    for (const auto& r : treeRows(rowTreeOf(snapshot)))
    {
        if (!r.lane)
            above = index.at(r.id);
        if (hidden(r.group) || (r.lane && !lanes))
            continue;
        if (r.lane)
            rows.push_back(
                {.kind = ArrangementRow::Kind::Lane, .lane = r.id, .track = above, .depth = r.depth});
        else
            rows.push_back({.kind = r.isGroup ? ArrangementRow::Kind::Group : ArrangementRow::Kind::Track,
                            .channel = r.id,
                            .track = above,
                            .depth = r.depth});
    }
    if (returns)
        for (const auto& r : snapshot.returns)
            rows.push_back({.kind = ArrangementRow::Kind::Return, .channel = r.id});
    return rows;
}

int firstMixRow(const std::vector<ArrangementRow>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].kind == ArrangementRow::Kind::Return)
            return static_cast<int>(i);
    return static_cast<int>(rows.size());
}

int clipTrackOf(const std::vector<ArrangementRow>& rows, int row, int trackCount)
{
    if (trackCount <= 0)
        return -1;
    if (row < 0)
        return 0;
    if (row < static_cast<int>(rows.size()) && rows[static_cast<size_t>(row)].track >= 0)
        return rows[static_cast<size_t>(row)].track;
    return trackCount - 1;
}

int rowOfTrack(const std::vector<ArrangementRow>& rows, int track)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].holdsClips() && rows[i].track == track)
            return static_cast<int>(i);
    return -1;
}

int rowOfLane(const std::vector<ArrangementRow>& rows, std::uint64_t lane)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].isLane() && rows[i].lane == lane)
            return static_cast<int>(i);
    return -1;
}
} // namespace fd::model
