#pragma once

#include "model/Snapshot.h"

#include <string>
#include <vector>

namespace fd::model
{
/// One row of the arrangement (docs/design/automation.md §3, §12; recording.md §6): a track (it
/// holds clips), one of a track's take lanes under it, an automation lane among the tracks, a
/// return track or the master.
struct ArrangementRow
{
    enum class Kind { Track, Take, Lane, Return, Master };
    Kind kind = Kind::Track;
    std::uint64_t channel = 0; // a track, take, return or master row: its channel
    std::uint64_t lane = 0;    // a lane row: its lane (AutomationLaneInfo::id)
    std::uint64_t take = 0;    // a take row: its take (TakeInfo::id)
    /// The index in Snapshot::tracks of a track row; for a lane row, of the track above it (the
    /// first track for lanes at the top); -1 for return tracks and the master.
    int track = -1;

    [[nodiscard]] bool holdsClips() const noexcept { return kind == Kind::Track; }
    [[nodiscard]] bool isLane() const noexcept { return kind == Kind::Lane; }
    [[nodiscard]] bool isTake() const noexcept { return kind == Kind::Take; }
    /// Stable while the row exists (row heights are kept by it): "c12", "a40", "t12/51".
    [[nodiscard]] std::string key() const;

    friend bool operator==(const ArrangementRow&, const ArrangementRow&) = default;
};

/// Default heights: a track's, a lane's (name and subtitle) and a take lane's.
inline constexpr double kLaneDefaultHeight = 56.0;
inline constexpr double kTakeDefaultHeight = 40.0;

/// The rows, top to bottom: the lanes at the top, then each track followed by its take lanes
/// (when it shows them) and the lanes after it (`lanes`: shown), lanes whose track is gone, then
/// (when `mixRows`) the return tracks and the master.
[[nodiscard]] std::vector<ArrangementRow> arrangementRows(const Snapshot& snapshot, bool mixRows, bool lanes = true);

/// The row index of the first return or master row (rows.size() when there is none).
[[nodiscard]] int firstMixRow(const std::vector<ArrangementRow>& rows);

/// The track index (Snapshot::tracks) a row stands for when dragging clips across rows: a track
/// row's own, a lane's track above it, or for rows below the tracks the last track. -1 with no
/// tracks.
[[nodiscard]] int clipTrackOf(const std::vector<ArrangementRow>& rows, int row, int trackCount);

/// The row of a track (by index in Snapshot::tracks); -1 if it isn't shown.
[[nodiscard]] int rowOfTrack(const std::vector<ArrangementRow>& rows, int track);

/// The row of an automation lane; -1 if it isn't shown.
[[nodiscard]] int rowOfLane(const std::vector<ArrangementRow>& rows, std::uint64_t lane);
} // namespace fd::model
