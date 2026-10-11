#pragma once

#include "model/ArrangementRows.h"
#include "model/Snapshot.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fd::model
{
// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md): the rules,
// JUCE-free. The Session applies them; the views draw what they say.

/// A copy's name (§7): "Bass" -> "Bass 2", "Bass 2" -> "Bass 3", the next number no name in
/// `taken` has.
[[nodiscard]] std::string nextCopyName(const std::string& name, const std::vector<std::string>& taken);

/// Where a dragged row lands (§6), for the boundary nearest the pointer: boundary b is above
/// rows[b] (rows.size(): below the last row).
/// - A track lands before a track (with the lanes right after it moving along): a boundary among
///   another track's lanes snaps to after them. It stays among the tracks.
/// - A lane lands at the boundary, after the track above it (0: at the top), before the lane
///   below it if that one sits after the same track.
/// - A return lands among the returns.
struct RowDrop
{
    std::size_t boundary = 0;       // where the line is drawn, snapped
    bool moves = false;             // false: it would land where it is
    std::uint64_t beforeTrack = 0;  // a track: before this one (0: after the last track)
    std::uint64_t laneAfter = 0;    // a lane: the track it sits after (0: at the top)
    std::uint64_t beforeLane = 0;   // a lane: before this lane (0: after the others there)
    std::uint64_t beforeReturn = 0; // a return: before this one (0: the last)
};
[[nodiscard]] RowDrop dropRow(const std::vector<ArrangementRow>& rows, const Snapshot& snapshot,
                              std::size_t dragged, std::size_t boundary);

/// Clips being pasted (§9): each on the row `row` below the target track, `offset` seconds after
/// the paste point.
struct PasteItem
{
    int row = 0;
    double offset = 0.0;
};
struct PastePlace
{
    std::size_t item = 0;
    std::size_t track = 0; // index in the tracks
    double start = 0.0;
};
/// Where they land at `at` on track `from` of `trackCount`: rows past the last track are left out
/// (how many rows, the second).
[[nodiscard]] std::pair<std::vector<PastePlace>, int>
pastePlacement(const std::vector<PasteItem>& items, std::size_t trackCount, std::size_t from, double at);

/// What deleting a track or return takes with it (§5), in the same undo step.
struct DeletionPlan
{
    std::vector<std::uint64_t> lanes; // lanes that drive only it: deleted
    /// Assignments to remove from lanes that stay: (lane, channel, key). Its own, and on other
    /// channels, their sends to it (a return).
    struct Assignment
    {
        std::uint64_t lane = 0, channel = 0;
        std::string key;
        bool operator==(const Assignment&) const = default;
    };
    std::vector<Assignment> assignments;
    /// Lanes right after it that stay: (lane, the track they sit after now: the one above, or 0).
    std::vector<std::pair<std::uint64_t, std::uint64_t>> reanchor;
    std::vector<std::uint64_t> sidechains; // modules whose sidechain it feeds: back to Off
    std::vector<std::uint64_t> sends;      // channels with a send to it (a return)
};
[[nodiscard]] DeletionPlan deletionPlan(const Snapshot& snapshot, std::uint64_t channel);

/// The lanes a duplicated track takes copies of (§7): those that drive only it.
[[nodiscard]] std::vector<std::uint64_t> lanesOnlyFor(const Snapshot& snapshot, std::uint64_t channel);
} // namespace fd::model
