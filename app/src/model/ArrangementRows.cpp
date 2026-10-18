#include "model/ArrangementRows.h"

#include "model/Groups.h"

#include <algorithm>

namespace fd::model
{
std::string ArrangementRow::key() const
{
    return isLane() ? "a" + std::to_string(lane) : "c" + std::to_string(channel);
}

std::vector<ArrangementRow> arrangementRows(const Snapshot& snapshot, bool returns, bool lanes,
                                            const std::set<std::uint64_t>& folded)
{
    std::vector<ArrangementRow> rows;
    const int last = static_cast<int>(snapshot.tracks.size()) - 1;
    auto lanesAfter = [&](std::uint64_t after, int track, int depth)
    {
        if (lanes)
            for (const auto& l : snapshot.automationLanes)
                if (l.after == after)
                    rows.push_back(
                        {.kind = ArrangementRow::Kind::Lane, .lane = l.id, .track = track, .depth = depth});
    };
    lanesAfter(0, last >= 0 ? 0 : -1, 0);
    // The groups open above the next row, innermost last: a lane after a group comes once its rows
    // have.
    std::vector<const TrackInfo*> open;
    const auto close = [&](const TrackInfo* until, int track)
    {
        while (!open.empty() && open.back() != until)
        {
            const auto* group = open.back();
            open.pop_back();
            if (!hiddenByFolding(snapshot, *group, folded))
                lanesAfter(group->id, track, groupDepth(snapshot, *group));
        }
    };
    for (int i = 0; i <= last; ++i)
    {
        const auto& t = snapshot.tracks[static_cast<size_t>(i)];
        close(parentGroup(snapshot, t), i - 1);
        if (t.isGroup())
            open.push_back(&t);
        if (hiddenByFolding(snapshot, t, folded))
            continue;
        const int depth = groupDepth(snapshot, t);
        rows.push_back({.kind = t.isGroup() ? ArrangementRow::Kind::Group : ArrangementRow::Kind::Track,
                        .channel = t.id,
                        .track = i,
                        .depth = depth});
        if (!t.isGroup())
            lanesAfter(t.id, i, depth);
    }
    close(nullptr, last);
    // A lane whose track is gone sits after the last track.
    if (lanes)
        for (const auto& l : snapshot.automationLanes)
            if (l.after != 0 && std::none_of(snapshot.tracks.begin(), snapshot.tracks.end(),
                                             [&](const auto& t) { return t.id == l.after; }))
                rows.push_back({.kind = ArrangementRow::Kind::Lane, .lane = l.id, .track = last});
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
