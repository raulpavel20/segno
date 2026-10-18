#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace fd::model
{
// The arrangement's rows as the project keeps them (tracks-and-clips.md §19, ADR 0042; groups,
// ADR 0072, groups.md §5.3, §20): the tracks and groups in the edit's order, each in a group or in
// none, and the automation lanes, each after a track or group (0: first) in a group (0: none). A
// row moved keeps every other row where it was.

struct RowTree
{
    struct Channel
    {
        std::uint64_t id = 0, group = 0; // the group it's in (0: none)
        bool isGroup = false;
        friend bool operator==(const Channel&, const Channel&) = default;
    };
    struct Lane
    {
        std::uint64_t id = 0;
        std::uint64_t after = 0; // a track or group in `group` it comes after (0: first there)
        std::uint64_t group = 0; // the group it's in (0: none)
        friend bool operator==(const Lane&, const Lane&) = default;
    };
    std::vector<Channel> channels; // a group before what it holds
    std::vector<Lane> lanes;       // in their order
    friend bool operator==(const RowTree&, const RowTree&) = default;
};

/// A row, top to bottom: a track, group or lane, the group it's in, and how deep that is.
struct TreeRow
{
    std::uint64_t id = 0, group = 0;
    bool lane = false, isGroup = false;
    int depth = 0;
    friend bool operator==(const TreeRow&, const TreeRow&) = default;
};

/// The rows top to bottom. In each group, and at the top: the lanes first there, then each of its
/// tracks and groups (a group's own rows right after it), each followed by the lanes after it. A
/// lane whose row is gone comes last in its group, and last of all when its group is gone too.
[[nodiscard]] std::vector<TreeRow> treeRows(const RowTree& tree);

/// The tree with a track or group gone, a group with everything in it: the lanes left keep their
/// places, those that were in it then in its group, each after the row now above it (ADR 0042).
[[nodiscard]] RowTree withoutRow(const RowTree& tree, std::uint64_t row);

/// The tree with `chosen` tracks and groups grouped into a new group `group` (groups.md §5.1): it
/// goes at the topmost one's place, in its group, and they move into it in their order, a group
/// with its rows; one inside another chosen goes with that one. A lane between two of them, with
/// nothing else between, goes in with them; every other row stays where it was. As it was when
/// nothing chosen is there.
[[nodiscard]] RowTree groupInTree(const RowTree& tree, const std::vector<std::uint64_t>& chosen,
                                  std::uint64_t group);

/// A row moved right above `before` (0: at the end) in `into`, a group (0: none): a group moves
/// with its rows, every other row stays where it was, and each lane is then after the track or
/// group above it in its group (ADR 0042). Without `into`, the group `before` is in; with `before`
/// outside `into`, at the end of `into`. The moved channel's place (its group, and the channel it
/// comes after there, 0: first) and the tree after. None when a row isn't there, a group would go
/// inside itself, or nothing would move.
struct RowMove
{
    std::uint64_t group = 0, after = 0;
    RowTree tree;
};
[[nodiscard]] std::optional<RowMove> moveInTree(const RowTree& tree, std::uint64_t row, std::uint64_t before,
                                                std::optional<std::uint64_t> into = std::nullopt);
} // namespace fd::model
