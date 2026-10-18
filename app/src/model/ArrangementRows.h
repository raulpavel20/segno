#pragma once

#include "model/Snapshot.h"

#include <set>
#include <string>
#include <vector>

namespace fd::model
{
/// One row of the arrangement (docs/design/automation.md §3, §12): a track (it holds clips, and
/// grows by its open takes, ADR 0032), a group (ADR 0072: its members in miniature), an automation
/// lane among the tracks, or a return track. The master isn't one: its row is the song row, pinned
/// above them (ADR 0049).
struct ArrangementRow
{
    enum class Kind
    {
        Track,
        Lane,
        Return,
        Group
    };
    Kind kind = Kind::Track;
    std::uint64_t channel = 0; // a track, group, return or master row: its channel
    std::uint64_t lane = 0;    // a lane row: its lane (AutomationLaneInfo::id)
    /// The index in Snapshot::tracks of a track or group row; for a lane row, of the track above it
    /// (the first track for lanes at the top); -1 for return tracks.
    int track = -1;
    /// How many groups the row is in (groups.md §6.1): its header shows that many colour bars.
    int depth = 0;

    [[nodiscard]] bool holdsClips() const noexcept { return kind == Kind::Track; }
    [[nodiscard]] bool isLane() const noexcept { return kind == Kind::Lane; }
    [[nodiscard]] bool isGroup() const noexcept { return kind == Kind::Group; }
    /// Stable while the row exists (row heights are kept by it): "c12", "a40".
    [[nodiscard]] std::string key() const;

    friend bool operator==(const ArrangementRow&, const ArrangementRow&) = default;
};

/// A lane's default height (name and subtitle).
inline constexpr double kLaneDefaultHeight = 56.0;

/// The rows, top to bottom: the lanes at the top, then each track or group followed by the lanes
/// after it (`lanes`: shown), lanes whose track is gone, then (when `returns`) the return tracks.
/// A group's row comes before the rows in it; a lane after a group comes after them. The rows in a
/// `folded` group are left out (groups.md §6.4).
[[nodiscard]] std::vector<ArrangementRow> arrangementRows(const Snapshot& snapshot, bool returns,
                                                          bool lanes = true,
                                                          const std::set<std::uint64_t>& folded = {});

/// The row index of the first return row (rows.size() when there is none).
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
