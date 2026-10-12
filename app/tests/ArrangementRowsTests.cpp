// The arrangement's rows (M2 step C4, ADR 0029, docs/design/automation.md §3): automation lanes
// among the tracks, then returns and the master.

#include "model/ArrangementRows.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Kind = ArrangementRow::Kind;

namespace
{
/// Lane 40 at the top; Keys (1) with lanes 41 and 42; Drums (2); lane 43 after a deleted track.
Snapshot project()
{
    Snapshot s;
    s.tracks.push_back({.id = 1, .name = "Keys"});
    s.tracks.push_back({.id = 2, .name = "Drums"});
    s.returns.push_back({.id = 5, .role = TrackInfo::Role::Return});
    s.master.id = 9;
    s.automationLanes = {{.id = 40}, {.id = 41, .after = 1}, {.id = 42, .after = 1}, {.id = 43, .after = 3}};
    return s;
}
} // namespace

TEST_CASE("arrangement rows: lanes sit among the tracks, after the track they follow", "[fast]")
{
    const auto s = project();
    const auto rows = arrangementRows(s, true);
    REQUIRE(rows.size() == 7); // the master's row is the song row, above them (ADR 0049)
    CHECK(rows[0] == ArrangementRow{.kind = Kind::Lane, .lane = 40, .track = 0});
    CHECK(rows[1] == ArrangementRow{.kind = Kind::Track, .channel = 1, .track = 0});
    CHECK(rows[2] == ArrangementRow{.kind = Kind::Lane, .lane = 41, .track = 0});
    CHECK(rows[3].lane == 42);
    CHECK(rows[4] == ArrangementRow{.kind = Kind::Track, .channel = 2, .track = 1});
    CHECK(rows[5] == ArrangementRow{.kind = Kind::Lane, .lane = 43, .track = 1}); // its track is gone
    CHECK(rows[6] == ArrangementRow{.kind = Kind::Return, .channel = 5});
    CHECK(firstMixRow(rows) == 6);
    CHECK(rows[2].key() == "a41");
    CHECK(rows[1].key() == "c1");
    CHECK(rowOfLane(rows, 42) == 3);
    CHECK(rowOfLane(rows, 7) == -1);

    // Without the returns: tracks and lanes; without lanes: the tracks alone.
    CHECK(arrangementRows(s, false).size() == 6);
    const auto tracksOnly = arrangementRows(s, false, false);
    CHECK(tracksOnly.size() == 2);
    CHECK(firstMixRow(tracksOnly) == 2);
}

TEST_CASE("arrangement rows: clips move between tracks across lane rows", "[fast]")
{
    const auto rows = arrangementRows(project(), true);
    CHECK(clipTrackOf(rows, 0, 2) == 0); // a lane at the top: the first track
    CHECK(clipTrackOf(rows, 3, 2) == 0); // a lane of the first track
    CHECK(clipTrackOf(rows, 4, 2) == 1);
    CHECK(clipTrackOf(rows, 6, 2) == 1); // below the tracks: the last
    CHECK(clipTrackOf(rows, -1, 2) == 0);
    CHECK(clipTrackOf(rows, 0, 0) == -1);
    CHECK(rowOfTrack(rows, 1) == 4);
    CHECK(rowOfTrack(rows, 4) == -1);

    Snapshot empty;
    empty.automationLanes = {{.id = 40}};
    CHECK(arrangementRows(empty, false) == std::vector{ArrangementRow{.kind = Kind::Lane, .lane = 40}});
}
