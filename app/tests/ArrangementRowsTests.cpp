// The arrangement's rows (M2 step C4, docs/design/automation.md §3): tracks with their lanes,
// then returns and the master with theirs.

#include "model/ArrangementRows.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Kind = ArrangementRow::Kind;

namespace
{
Snapshot project()
{
    Snapshot s;
    s.tracks.push_back({.id = 1, .name = "Keys", .lanes = {{.key = "volume"}, {.key = "m7/cutoff"}}});
    s.tracks.push_back({.id = 2, .name = "Drums", .lanes = {{.key = "pan"}}, .lanesFolded = true});
    s.returns.push_back({.id = 5, .role = TrackInfo::Role::Return, .lanes = {{.key = "volume"}}});
    s.master.id = 9;
    s.master.lanes = {{.key = "volume"}};
    return s;
}
} // namespace

TEST_CASE("arrangement rows: lanes follow their channel unless folded", "[fast]")
{
    const auto s = project();
    const auto rows = arrangementRows(s, true);
    REQUIRE(rows.size() == 8);
    CHECK(rows[0] == ArrangementRow{.kind = Kind::Track, .channel = 1, .track = 0});
    CHECK(rows[1] == ArrangementRow{.kind = Kind::Lane, .channel = 1, .lane = "volume", .track = 0});
    CHECK(rows[2].lane == "m7/cutoff");
    CHECK(rows[3] == ArrangementRow{.kind = Kind::Track, .channel = 2, .track = 1}); // folded: no lane rows
    CHECK(rows[4] == ArrangementRow{.kind = Kind::Return, .channel = 5});
    CHECK(rows[5] == ArrangementRow{.kind = Kind::Lane, .channel = 5, .lane = "volume"});
    CHECK(rows[6].kind == Kind::Master);
    CHECK(rows[7].isLane());
    CHECK(firstMixRow(rows) == 4);
    CHECK(rows[2].key() == "c1/m7/cutoff");
    CHECK(rows[0].key() == "c1");

    // Without the mix rows: tracks and their lanes only.
    const auto tracksOnly = arrangementRows(s, false);
    CHECK(tracksOnly.size() == 4);
    CHECK(firstMixRow(tracksOnly) == 4);
}

TEST_CASE("arrangement rows: clips move between tracks across lane rows", "[fast]")
{
    const auto rows = arrangementRows(project(), true);
    CHECK(clipTrackOf(rows, 0, 2) == 0);
    CHECK(clipTrackOf(rows, 2, 2) == 0); // a lane of the first track
    CHECK(clipTrackOf(rows, 3, 2) == 1);
    CHECK(clipTrackOf(rows, 5, 2) == 1); // below the tracks: the last
    CHECK(clipTrackOf(rows, -1, 2) == 0);
    CHECK(clipTrackOf(rows, 0, 0) == -1);
    CHECK(rowOfTrack(rows, 1) == 3);
    CHECK(rowOfTrack(rows, 4) == -1);
}
