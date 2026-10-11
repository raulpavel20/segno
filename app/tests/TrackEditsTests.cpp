// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md): copy names,
// where a dragged row lands, where pasted clips land, what a deletion takes. JUCE-free.

#include "model/TrackEdits.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
TrackInfo track(std::uint64_t id, const std::string& name = {})
{
    return {.id = id, .name = name};
}

AutomationLaneInfo lane(std::uint64_t id, std::uint64_t after,
                        std::vector<std::pair<std::uint64_t, std::string>> assigned = {})
{
    AutomationLaneInfo l{.id = id, .after = after};
    for (const auto& [channel, key] : assigned)
        l.assignments.push_back({.channel = channel, .key = key});
    return l;
}
} // namespace

TEST_CASE("tracks: a copy takes the next free number", "[fast]")
{
    CHECK(nextCopyName("Bass", {"Bass"}) == "Bass 2");
    CHECK(nextCopyName("Bass", {"Bass", "Bass 2"}) == "Bass 3");
    CHECK(nextCopyName("Bass 2", {"Bass", "Bass 2"}) == "Bass 3");
    CHECK(nextCopyName("Bass 9", {"Bass 10"}) == "Bass 11");
    CHECK(nextCopyName("1 Track", {"1 Track"}) == "1 Track 2");
    CHECK(nextCopyName("A Return", {}) == "A Return 2");
}

TEST_CASE("tracks: the names the app gives come after the highest in use, so none repeats (C11 review)", "[fast]")
{
    SECTION("tracks")
    {
        CHECK(nextTrackName({}) == "1 Track");
        CHECK(nextTrackName({"1 Track", "2 Track", "3 Track"}) == "4 Track");
        CHECK(nextTrackName({"2 Track", "3 Track"}) == "4 Track"); // "1 Track" deleted: not again
        CHECK(nextTrackName({"1 Track", "3 Track"}) == "4 Track"); // a gap isn't filled either
        CHECK(nextTrackName({"Drums", "Bass", "3 Track 2"}) == "1 Track"); // names given and copies don't count
        CHECK(nextTrackName({"9 Track", "10 Track"}) == "11 Track");
        CHECK(nextTrackName({"0 Track", "07 Track", " Track", "x Track"}) == "1 Track");
    }
    SECTION("returns")
    {
        CHECK(nextReturnName({}) == "A Return");
        CHECK(nextReturnName({"B Return"}) == "C Return"); // "A Return" deleted: not again
        CHECK(nextReturnName({"A Return", "Reverb"}) == "B Return");
        CHECK(nextReturnName({"Z Return"}) == "AA Return");
        CHECK(nextReturnName({"AZ Return", "C Return"}) == "BA Return");
        CHECK(nextReturnName({"a Return", "A1 Return"}) == "A Return");
    }
    SECTION("automation lanes")
    {
        CHECK(nextLaneName({}) == "Automation 1");
        CHECK(nextLaneName({"Automation 1", "Automation 2"}) == "Automation 3");
        CHECK(nextLaneName({"Automation 2"}) == "Automation 3");
        CHECK(nextLaneName({"Filter", "Automation 2 2"}) == "Automation 1");
    }
    SECTION("a duplicated track's lanes")
    {
        CHECK(laneCopyName("Automation 1", {"Automation 1", "Automation 4"}) == "Automation 5");
        CHECK(laneCopyName("Filter", {"Filter", "Automation 1"}) == "Filter 2");
    }
    SECTION("other names the app gives")
    {
        CHECK(uniqueName("Bass bounce", {"Bass"}) == "Bass bounce");
        CHECK(uniqueName("Bass bounce", {"Bass", "Bass bounce"}) == "Bass bounce 2");
        CHECK(uniqueName("Bass bounce", {"Bass bounce", "Bass bounce 2"}) == "Bass bounce 3");
    }
}

TEST_CASE("tracks: a dragged track lands before a track, its lanes with it; lanes and returns alone",
          "[fast]")
{
    // Rows: lane 50 (top), T1, T2, lane 60 (after T2), lane 61 (after T2), T3, return R, master.
    Snapshot s;
    s.tracks = {track(1), track(2), track(3)};
    s.returns = {{.id = 9, .role = TrackInfo::Role::Return}, {.id = 10, .role = TrackInfo::Role::Return}};
    s.master.id = 99;
    s.automationLanes = {lane(50, 0), lane(60, 2), lane(61, 2)};
    const auto rows = arrangementRows(s, true);
    REQUIRE(rows.size() == 9); // 50, T1, T2, 60, 61, T3, R9, R10, master
    REQUIRE(rows[3].lane == 60);

    // T1 (row 1) dropped among T2's lanes: snaps to before T3.
    auto d = dropRow(rows, s, 1, 4);
    CHECK(d.boundary == 5);
    CHECK(d.beforeTrack == 3);
    CHECK(d.moves);
    // ... at the end of the tracks (below T3, or among the returns): after the last.
    d = dropRow(rows, s, 1, 8);
    CHECK(d.boundary == 6);
    CHECK(d.beforeTrack == 0);
    // ... above the top lanes: before the first track (it can't go above lanes at the top).
    d = dropRow(rows, s, 5, 0);
    CHECK(d.boundary == 1);
    CHECK(d.beforeTrack == 1);
    // ... where it is: before itself, or before the track after it.
    CHECK_FALSE(dropRow(rows, s, 1, 1).moves);
    CHECK_FALSE(dropRow(rows, s, 1, 2).moves);
    CHECK_FALSE(dropRow(rows, s, 5, 6).moves);

    // Lane 61 to the top: after nothing, before lane 50? No: 50 sits at the top too.
    d = dropRow(rows, s, 4, 0);
    CHECK(d.laneAfter == 0);
    CHECK(d.beforeLane == 50);
    CHECK(d.moves);
    // ... right after T1: after T1, before nothing there.
    d = dropRow(rows, s, 4, 2);
    CHECK(d.laneAfter == 1);
    CHECK(d.beforeLane == 0);
    // ... before lane 60: after T2, before 60.
    d = dropRow(rows, s, 4, 3);
    CHECK(d.laneAfter == 2);
    CHECK(d.beforeLane == 60);
    CHECK_FALSE(dropRow(rows, s, 4, 4).moves);
    CHECK_FALSE(dropRow(rows, s, 4, 5).moves);

    // Return 10 before return 9; it stays among the returns.
    d = dropRow(rows, s, 7, 1);
    CHECK(d.boundary == 6);
    CHECK(d.beforeReturn == 9);
    CHECK(d.moves);
    d = dropRow(rows, s, 6, 9);
    CHECK(d.boundary == 8);
    CHECK(d.beforeReturn == 0);
    CHECK(d.moves);
    CHECK_FALSE(dropRow(rows, s, 8, 0).moves); // the master stays last
}

TEST_CASE("tracks: pasted clips keep their rows from the selected track; rows past the last are left out",
          "[fast]")
{
    const std::vector<PasteItem> items{{0, 0.0}, {1, 2.0}, {2, 0.5}, {2, 4.0}};
    auto [placed, leftOut] = pastePlacement(items, 3, 0, 10.0);
    REQUIRE(placed.size() == 4);
    CHECK(placed[1].track == 1);
    CHECK(placed[1].start == Approx(12.0));
    CHECK(leftOut == 0);
    std::tie(placed, leftOut) = pastePlacement(items, 3, 2, 1.0);
    CHECK(placed.size() == 1); // row 0 on track 2; rows 1 and 2 have no track
    CHECK(placed[0].track == 2);
    CHECK(leftOut == 2);
}

TEST_CASE("tracks: deleting one takes the lanes only it drives, its assignments, sidechains and sends",
          "[fast]")
{
    Snapshot s;
    s.tracks = {track(1), track(2), track(3)};
    s.tracks[2].devices.push_back({.id = 300, .sidechainSource = 2}); // T3's compressor listens to T2
    s.tracks[0].mix.sends = {{.returnId = 9}};
    s.tracks[2].mix.sends = {{.returnId = 9}};
    s.returns = {{.id = 9, .role = TrackInfo::Role::Return}};
    s.automationLanes = {
        lane(50, 2, {{2, "volume"}}),                // only T2: goes
        lane(51, 2, {{2, "pan"}, {1, "volume"}}),    // shared: loses T2's, re-anchors to T1
        lane(52, 1, {{1, "send/9"}, {3, "send/9"}}), // T1's and T3's sends to return 9
        lane(53, 2),                                 // empty, after T2: stays, re-anchors
    };
    auto plan = deletionPlan(s, 2);
    CHECK(plan.lanes == std::vector<std::uint64_t>{50});
    REQUIRE(plan.assignments.size() == 1);
    CHECK(plan.assignments[0] == DeletionPlan::Assignment{51, 2, "pan"});
    CHECK(plan.reanchor == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{51, 1}, {53, 1}});
    CHECK(plan.sidechains == std::vector<std::uint64_t>{300});
    CHECK(plan.sends.empty());

    // The first track: lanes after it go to the top.
    s.automationLanes = {lane(54, 1)};
    CHECK(deletionPlan(s, 1).reanchor == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{54, 0}});

    // A return: the sends to it, and lanes of only those sends.
    s.automationLanes = {lane(52, 1, {{1, "send/9"}, {3, "send/9"}}),
                         lane(55, 1, {{1, "send/9"}, {1, "volume"}})};
    plan = deletionPlan(s, 9);
    CHECK(plan.lanes == std::vector<std::uint64_t>{52});
    REQUIRE(plan.assignments.size() == 1);
    CHECK(plan.assignments[0] == DeletionPlan::Assignment{55, 1, "send/9"});
    CHECK(plan.sends == std::vector<std::uint64_t>{1, 3});
    CHECK(plan.reanchor.empty());

    // The master can't be deleted; a lane that drives only a track is the one its copy copies.
    s.master.id = 99;
    CHECK(deletionPlan(s, 99).lanes.empty());
    s.automationLanes = {lane(50, 2, {{2, "volume"}}), lane(51, 2, {{2, "pan"}, {1, "volume"}}), lane(53, 2)};
    CHECK(lanesOnlyFor(s, 2) == std::vector<std::uint64_t>{50});
}
