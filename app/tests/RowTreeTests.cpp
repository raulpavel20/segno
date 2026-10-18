// The arrangement's rows as the project keeps them (tracks-and-clips.md §19, ADR 0042; groups.md
// §5.3, §20): rows in order from the tracks, groups and lanes; a moved row, a group with its rows,
// leaving every other row where it was.

#include "model/RowTree.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;

namespace
{
using Lanes = std::vector<RowTree::Lane>;

/// Ids of the rows, top to bottom.
std::vector<std::uint64_t> ids(const RowTree& tree)
{
    std::vector<std::uint64_t> out;
    for (const auto& r : treeRows(tree))
        out.push_back(r.id);
    return out;
}

/// Lane 50 (top), T1, T2, lane 60, lane 61, T3.
RowTree flat()
{
    return {.channels = {{1}, {2}, {3}}, .lanes = {{50, 0}, {60, 2}, {61, 2}}};
}

/// T1; Drums (10): Kick 2, lane 60 after it, Snare 3; lane 61 after Drums; Bass (11): Sub 4; T5.
RowTree grouped()
{
    return {.channels = {{1}, {10, 0, true}, {2, 10}, {3, 10}, {11, 0, true}, {4, 11}, {5}},
            .lanes = {{60, 2, 10}, {61, 10, 0}}};
}
} // namespace

TEST_CASE("row tree: rows in order, a group's after it, a lane after its row in its group, at their depth",
          "[fast]")
{
    CHECK(ids(flat()) == std::vector<std::uint64_t>{50, 1, 2, 60, 61, 3});
    const auto rows = treeRows(grouped());
    CHECK(ids(grouped()) == std::vector<std::uint64_t>{1, 10, 2, 60, 3, 61, 11, 4, 5});
    CHECK(rows[2].depth == 1);
    CHECK(rows[3].lane);
    CHECK(rows[3].group == 10);
    CHECK(rows[5].group == 0); // after Drums, outside it
    // First in a group, and after a row that's gone (last in its group).
    auto t = grouped();
    t.lanes.push_back({62, 0, 11});
    t.lanes.push_back({63, 99, 10});
    CHECK(ids(t) == std::vector<std::uint64_t>{1, 10, 2, 60, 3, 63, 61, 11, 62, 4, 5});
}

TEST_CASE("row tree: a moved track or lane leaves every other row where it was, each lane after the row now "
          "above it (ADR 0042)",
          "[fast]")
{
    const auto rows = flat();
    SECTION("a track moves alone: its lanes stay, after the track now above them")
    {
        const auto o = moveInTree(rows, 2, 0);
        REQUIRE(o);
        CHECK(ids(o->tree) == std::vector<std::uint64_t>{50, 1, 60, 61, 3, 2});
        CHECK(o->tree.lanes == Lanes{{50, 0}, {60, 1}, {61, 1}});
        CHECK(o->after == 3);
        const auto top = moveInTree(rows, 2, 50);
        REQUIRE(top);
        CHECK(top->tree.lanes == Lanes{{50, 2}, {60, 1}, {61, 1}});
        CHECK(top->after == 0);
    }
    SECTION("a track between two lanes: the lane below it is now after it")
    {
        const auto o = moveInTree(rows, 3, 61);
        REQUIRE(o);
        CHECK(o->tree.lanes == Lanes{{50, 0}, {60, 2}, {61, 3}});
    }
    SECTION("a lane moves alone")
    {
        const auto o = moveInTree(rows, 61, 1);
        REQUIRE(o);
        CHECK(o->tree.lanes == Lanes{{50, 0}, {61, 0}, {60, 2}});
        const auto last = moveInTree(rows, 50, 0);
        REQUIRE(last);
        CHECK(last->tree.lanes == Lanes{{60, 2}, {61, 2}, {50, 3}});
    }
    SECTION("where it is, or nowhere: no move")
    {
        CHECK_FALSE(moveInTree(rows, 2, 2));
        CHECK_FALSE(moveInTree(rows, 2, 60)); // right above the row after it
        CHECK_FALSE(moveInTree(rows, 3, 0));  // the last, to the end
        CHECK_FALSE(moveInTree(rows, 7, 0));
        CHECK_FALSE(moveInTree(rows, 2, 7));
    }
}

TEST_CASE("row tree: into and out of groups; a group moves with its rows; never inside itself", "[fast]")
{
    const auto t = grouped();
    // T5 between Kick's lane and Snare: into Drums, after Kick.
    auto o = moveInTree(t, 5, 3);
    REQUIRE(o);
    CHECK(o->group == 10);
    CHECK(o->after == 2);
    CHECK(ids(o->tree) == std::vector<std::uint64_t>{1, 10, 2, 60, 5, 3, 61, 11, 4});
    // T1 at Drums' end, inside it (dropped above lane 61, into Drums): after Snare.
    o = moveInTree(t, 1, 61, 10);
    REQUIRE(o);
    CHECK(o->group == 10);
    CHECK(o->after == 3);
    CHECK(ids(o->tree) == std::vector<std::uint64_t>{10, 2, 60, 3, 1, 61, 11, 4, 5});
    // The same place, outside Drums: right above lane 61, which then comes after T1.
    o = moveInTree(t, 1, 61, 0);
    REQUIRE(o);
    CHECK(o->group == 0);
    CHECK(o->after == 10);
    CHECK(o->tree.lanes == Lanes{{60, 2, 10}, {61, 1, 0}});
    // Kick out of Drums, to the top: its lane stays in Drums, first there.
    o = moveInTree(t, 2, 1);
    REQUIRE(o);
    CHECK(o->group == 0);
    CHECK(ids(o->tree) == std::vector<std::uint64_t>{2, 1, 10, 60, 3, 61, 11, 4, 5});
    CHECK(o->tree.lanes == Lanes{{60, 0, 10}, {61, 10, 0}});
    // Drums moved below Bass, with its rows; lane 61 stays, after T1.
    o = moveInTree(t, 10, 5);
    REQUIRE(o);
    CHECK(ids(o->tree) == std::vector<std::uint64_t>{1, 61, 11, 4, 10, 2, 60, 3, 5});
    CHECK(o->tree.lanes == Lanes{{61, 1, 0}, {60, 2, 10}});
    // Drums into Bass, at its end: a group in a group.
    o = moveInTree(t, 10, 0, 11);
    REQUIRE(o);
    CHECK(o->group == 11);
    CHECK(o->after == 4);
    // Never inside itself, nor onto its own rows.
    CHECK_FALSE(moveInTree(t, 10, 0, 10));
    CHECK_FALSE(moveInTree(t, 10, 3));
    auto nested = t;
    nested.channels[4].group = 10; // Bass inside Drums
    CHECK_FALSE(moveInTree(nested, 10, 0, 11));
}

TEST_CASE("row tree: a track or group gone, the lanes left keep their places, after the row now above them",
          "[fast]")
{
    // T2 gone: lanes 60 and 61 after T1; T1 gone: lane 50 stays first.
    auto o = withoutRow(flat(), 2);
    CHECK(ids(o) == std::vector<std::uint64_t>{50, 1, 60, 61, 3});
    CHECK(o.lanes == Lanes{{50, 0}, {60, 1}, {61, 1}});
    o = withoutRow(flat(), 1);
    CHECK(o.lanes == Lanes{{50, 0}, {60, 2}, {61, 2}});
    // Kick gone: its lane is first in Drums.
    o = withoutRow(grouped(), 2);
    CHECK(ids(o) == std::vector<std::uint64_t>{1, 10, 60, 3, 61, 11, 4, 5});
    CHECK(o.lanes == Lanes{{60, 0, 10}, {61, 10, 0}});
    // Drums gone with everything in it: the lane that was in it stays where it was, after T1.
    o = withoutRow(grouped(), 10);
    CHECK(ids(o) == std::vector<std::uint64_t>{1, 60, 61, 11, 4, 5});
    CHECK(o.lanes == Lanes{{60, 1, 0}, {61, 1, 0}});
    // Nothing there: as it was.
    CHECK(withoutRow(grouped(), 99) == grouped());
}

TEST_CASE("row tree: grouping gathers the chosen rows at the topmost's place; lanes between them go in, the "
          "others stay",
          "[fast]")
{
    // T1, lane 50 (after T1), T2, lane 60 (after T2), T3, lane 61 (after T3), T4.
    const RowTree t{.channels = {{1}, {2}, {3}, {4}}, .lanes = {{50, 1}, {60, 2}, {61, 3}}};
    // T1 and T2: lane 50 between them goes in; lane 60 after T2 stays, now after the group.
    auto o = groupInTree(t, {2, 1}, 90);
    CHECK(ids(o) == std::vector<std::uint64_t>{90, 1, 50, 2, 60, 3, 61, 4});
    CHECK(o.channels == std::vector<RowTree::Channel>{{90, 0, true}, {1, 90}, {2, 90}, {3}, {4}});
    CHECK(o.lanes == Lanes{{50, 1, 90}, {60, 90, 0}, {61, 3, 0}});
    // T1 and T3, apart: T2 between them, so lanes 50 and 60 stay with it, after the group.
    o = groupInTree(t, {1, 3}, 90);
    CHECK(ids(o) == std::vector<std::uint64_t>{90, 1, 3, 50, 2, 60, 61, 4});
    CHECK(o.lanes == Lanes{{50, 90, 0}, {60, 2, 0}, {61, 2, 0}});
    // Nothing chosen there: as it was.
    CHECK(groupInTree(t, {7}, 90) == t);

    // In a group: a group chosen goes with its rows, a track inside it with it; the new group in
    // the topmost's group.
    const auto g = grouped(); // T1; Drums 10 (2, lane 60, 3); lane 61; Bass 11 (4); T5
    o = groupInTree(g, {10, 2, 5}, 90);
    CHECK(ids(o) == std::vector<std::uint64_t>{1, 90, 10, 2, 60, 3, 5, 61, 11, 4});
    CHECK(o.channels[1] == RowTree::Channel{90, 0, true});
    CHECK(o.channels[2] == RowTree::Channel{10, 90, true});
    CHECK(o.channels[3] == RowTree::Channel{2, 10});
    o = groupInTree(g, {3}, 90); // Snare alone, inside Drums
    CHECK(ids(o) == std::vector<std::uint64_t>{1, 10, 2, 60, 90, 3, 61, 11, 4, 5});
    CHECK(o.channels[3] == RowTree::Channel{90, 10, true});
}
