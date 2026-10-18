#pragma once

#include "model/ArrangementRows.h"
#include "model/Snapshot.h"

#include <cstdint>
#include <optional>
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

/// The names the app gives new rows never repeat (§18), each the one after the highest in use of
/// its kind in `names`, so a deleted one's never comes back while a later one stands:
/// - a track: "N Track" ("1 Track" of three deleted: "4 Track");
/// - a return track: "L Return", A to Z, then AA, AB...;
/// - an automation lane: "Automation N".
/// Names the user gave don't count; the first is 1, or A.
[[nodiscard]] std::string nextTrackName(const std::vector<std::string>& names);
[[nodiscard]] std::string nextReturnName(const std::vector<std::string>& names);
[[nodiscard]] std::string nextLaneName(const std::vector<std::string>& names);
/// `name`, or with the next number free ("Bass bounce 2") when another row has it: the other
/// names the app gives (a bounce's track, a take on a track of its own).
[[nodiscard]] std::string uniqueName(const std::string& name, const std::vector<std::string>& taken);
/// A copied lane's name (a duplicated track's lanes): the next number for a numbered one, as a
/// new lane gets; a copy's ("Filter 2") for a name the user gave.
[[nodiscard]] std::string laneCopyName(const std::string& name, const std::vector<std::string>& taken);

/// Where a dragged row lands (§6, §19), for the boundary nearest the pointer: boundary b is above
/// rows[b] (rows.size(): below the last row).
/// - A track or a lane lands at any boundary among the tracks and lanes (ADR 0042): right above
///   the row there, or after the last of them.
/// - A return lands among the returns.
struct RowDrop
{
    std::size_t boundary = 0; // where the line is drawn
    bool moves = false;       // false: it would land where it is
    std::uint64_t before = 0; // the track, lane or return it lands right above (0: the last of its part)
    int depth = 0;            // how many groups it lands in: the line starts that far in (groups.md §5.3)
    std::uint64_t group = 0;  // the group it lands in (0: none): its row lit as a drop target
    bool onHeader = false;    // dropped on that group's header: at its end
};
[[nodiscard]] RowDrop dropRow(const std::vector<ArrangementRow>& rows, std::size_t dragged, std::size_t boundary);

/// A track or a lane, in the arrangement's order above the returns.
struct RowRef
{
    std::uint64_t id = 0;
    bool lane = false;

    friend bool operator==(const RowRef&, const RowRef&) = default;
};

/// The tracks and lanes as the project keeps them: the tracks in their order, and the lanes in
/// theirs, each after a track (0: at the top).
struct RowOrder
{
    std::vector<std::uint64_t> tracks;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> lanes; // the lane, the track it sits after
};

/// The order after moving `row` right above `before` (0: after the last row), every other row
/// keeping its place (§19, ADR 0042): each lane then sits after the track now above it. Nullopt
/// when either isn't there, or it wouldn't move.
[[nodiscard]] std::optional<RowOrder> moveRow(const std::vector<RowRef>& rows, std::uint64_t row,
                                              std::uint64_t before);

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
