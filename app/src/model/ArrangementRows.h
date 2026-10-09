#pragma once

#include "model/Snapshot.h"

#include <string>
#include <vector>

namespace fd::model
{
/// One row of the arrangement (docs/design/automation.md §3, §12): a track (it holds clips), an
/// automation lane under its channel, a return track or the master.
struct ArrangementRow
{
    enum class Kind { Track, Lane, Return, Master };
    Kind kind = Kind::Track;
    std::uint64_t channel = 0;
    std::string lane; // a lane row: its key (LaneInfo::key)
    /// The index in Snapshot::tracks of a track row, or of the track a lane row belongs to; -1
    /// for return tracks, the master and their lanes.
    int track = -1;

    [[nodiscard]] bool holdsClips() const noexcept { return kind == Kind::Track; }
    [[nodiscard]] bool isLane() const noexcept { return kind == Kind::Lane; }
    /// Stable while the row exists (row heights are kept by it): "c12", "c12/volume".
    [[nodiscard]] std::string key() const;

    friend bool operator==(const ArrangementRow&, const ArrangementRow&) = default;
};

/// Default heights: a track's, and a lane's (name and subtitle).
inline constexpr double kLaneDefaultHeight = 56.0;

/// The rows, top to bottom: each track with its unfolded lanes, then (when `mixRows`) each
/// return track with its lanes and the master with its lanes.
[[nodiscard]] std::vector<ArrangementRow> arrangementRows(const Snapshot& snapshot, bool mixRows);

/// The row index of the first return or master row (rows.size() when there is none).
[[nodiscard]] int firstMixRow(const std::vector<ArrangementRow>& rows);

/// The track index (Snapshot::tracks) a row stands for when dragging clips across rows: a track
/// row's own, a lane's track, or for rows below the tracks the last track. -1 with no tracks.
[[nodiscard]] int clipTrackOf(const std::vector<ArrangementRow>& rows, int row, int trackCount);

/// The row of a track (by index in Snapshot::tracks); -1 if it isn't shown.
[[nodiscard]] int rowOfTrack(const std::vector<ArrangementRow>& rows, int track);
} // namespace fd::model
