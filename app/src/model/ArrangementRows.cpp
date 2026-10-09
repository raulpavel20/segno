#include "model/ArrangementRows.h"

namespace fd::model
{
std::string ArrangementRow::key() const
{
    auto k = "c" + std::to_string(channel);
    return lane.empty() ? k : k + "/" + lane;
}

std::vector<ArrangementRow> arrangementRows(const Snapshot& snapshot, bool mixRows)
{
    std::vector<ArrangementRow> rows;
    auto add = [&rows](const TrackInfo& channel, ArrangementRow::Kind kind, int track) {
        rows.push_back({.kind = kind, .channel = channel.id, .track = track});
        if (!channel.lanesFolded)
            for (const auto& lane : channel.lanes)
                rows.push_back({.kind = ArrangementRow::Kind::Lane, .channel = channel.id, .lane = lane.key, .track = track});
    };
    for (size_t i = 0; i < snapshot.tracks.size(); ++i)
        add(snapshot.tracks[i], ArrangementRow::Kind::Track, static_cast<int>(i));
    if (mixRows)
    {
        for (const auto& r : snapshot.returns)
            add(r, ArrangementRow::Kind::Return, -1);
        add(snapshot.master, ArrangementRow::Kind::Master, -1);
    }
    return rows;
}

int firstMixRow(const std::vector<ArrangementRow>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].kind == ArrangementRow::Kind::Return || rows[i].kind == ArrangementRow::Kind::Master)
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
} // namespace fd::model
