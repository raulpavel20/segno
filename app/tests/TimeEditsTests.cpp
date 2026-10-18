// A time selection's edits (docs/design/time-selection.md §8–§11, ADR 0073): a lane's points
// cleared, copied and laid over a stretch with the curve outside kept; Fade to selection by where
// the stretch lies; section marks following time taken out and put in (§9).

#include "model/Sections.h"
#include "model/TimeEdits.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
/// A ramp up from 0 at 0 s to 1 at 4 s, then down to 0 at 8 s.
AutomationPoints tent()
{
    return {{0.0, 0.0f, 0.0f}, {4.0, 1.0f, 0.0f}, {8.0, 0.0f, 0.0f}};
}

/// The value at `t` approaching from both sides is the same everywhere outside [a, b].
void sameOutside(const AutomationPoints& before, const AutomationPoints& after, double a, double b)
{
    for (double t = -1.0; t <= 10.0; t += 0.25)
        if (t < a - 1e-9 || t > b + 1e-9)
        {
            INFO("t = " << t);
            CHECK(automationValueAt(after, t) == Approx(automationValueAt(before, t)).margin(1e-5));
        }
}

ClipInfo audio(std::uint64_t id, double start, double length)
{
    return {.id = id, .kind = ClipInfo::Kind::Audio, .start = start, .length = length};
}
} // namespace

TEST_CASE("time edits: a time selection's stretch and rows, from the selection", "[fast]")
{
    Selection s{.channel = 1, .rowTracks = {1, 2}, .rowLanes = {40}, .time = TimeSpan{1.0, 3.0}};
    const auto t = timeSelectionOf(s);
    CHECK(t.span == TimeSpan{1.0, 3.0});
    CHECK(t.tracks == std::set<std::uint64_t>{1, 2});
    CHECK(t.lanes == std::set<std::uint64_t>{40});
    CHECK_FALSE(t.empty());
    CHECK(timeSelectionOf(Selection{.channel = 1, .clips = {5}}).empty());
}

TEST_CASE("time edits: clearing a stretch of a lane keeps the curve outside it, a line inside (§11)",
          "[fast]")
{
    const auto before = tent();
    const auto after = clearPoints(before, 2.0, 6.0);
    sameOutside(before, after, 2.0, 6.0);
    // The peak inside is gone; a straight line joins the edges' values (0.5 and 0.5).
    CHECK(automationValueAt(after, 4.0) == Approx(0.5f));
    CHECK(std::none_of(after.begin(), after.end(),
                       [](const auto& p) { return std::abs(p.time - 4.0) < 1e-9; }));
    // A stretch with no point inside changes nothing.
    CHECK(clearPoints(before, 4.5, 7.5) == before);
    CHECK(clearPoints({}, 1.0, 2.0).empty());
}

TEST_CASE(
    "time edits: a lane's piece, laid over another place, carries its curve and keeps the rest (§8, §10)",
    "[fast]")
{
    const auto lane = tent();
    const auto piece = piecePoints(lane, 2.0, 6.0); // 0.5 up to 1 and back to 0.5
    REQUIRE(!piece.empty());
    CHECK(piece.front().time == Approx(0.0));
    CHECK(piece.front().value == Approx(0.5f));
    CHECK(piece.back().time == Approx(4.0));
    CHECK(piece.back().value == Approx(0.5f));

    // Laid over 8 .. 12 of a flat lane at 0.2: outside it, 0.2; inside, the piece's shape.
    const AutomationPoints flat{{0.0, 0.2f, 0.0f}, {16.0, 0.2f, 0.0f}};
    const auto placed = placePoints(flat, piece, 8.0, 4.0);
    for (double t : {0.0, 4.0, 7.75, 12.25, 15.0})
        CHECK(automationValueAt(placed, t) == Approx(0.2f).margin(1e-5));
    CHECK(automationValueAt(placed, 10.0) == Approx(1.0f));
    CHECK(automationValueAt(placed, 9.0) == Approx(0.75f));
    // An empty piece leaves a lane as it is; on an empty lane, the piece alone.
    CHECK(placePoints(flat, {}, 8.0, 4.0) == flat);
    CHECK(placePoints({}, piece, 8.0, 4.0).size() == piece.size());
    // Its points in a stretch, by index.
    CHECK(pointsIn(lane, 2.0, 6.0) == std::set<size_t>{1});
    CHECK(pointsIn(lane, 0.0, 8.0) == std::set<size_t>{0, 1});
}

TEST_CASE("time edits: Fade to selection fades in, out or crosses a join by where the stretch lies (§8)",
          "[fast]")
{
    // A 0 .. 4 and B 4 .. 8 meet; C 10 .. 14 stands alone; M a MIDI clip.
    const std::vector<ClipInfo> clips{audio(1, 0.0, 4.0),
                                      audio(2, 4.0, 4.0),
                                      audio(3, 10.0, 4.0),
                                      {.id = 4, .kind = ClipInfo::Kind::Midi, .start = 16.0, .length = 2.0}};
    SECTION("from a gap into C: C fades in to the stretch's end")
    {
        const auto f = stretchFades(clips, {9.0, 11.0});
        CHECK(f.fadeIns == std::vector<StretchFades::Edge>{{3, 1.0}});
        CHECK(f.fadeOuts.empty());
        CHECK(f.crossfades.empty());
    }
    SECTION("from inside C past its end: C fades out from the stretch's start")
    {
        const auto f = stretchFades(clips, {13.0, 15.0});
        CHECK(f.fadeOuts == std::vector<StretchFades::Edge>{{3, 1.0}});
    }
    SECTION("across A and B's join: a crossfade as long as the stretch, its start against the join")
    {
        const auto f = stretchFades(clips, {3.5, 4.5});
        REQUIRE(f.crossfades.size() == 1);
        CHECK(f.crossfades[0].out == 1);
        CHECK(f.crossfades[0].length == Approx(1.0));
        CHECK(f.crossfades[0].from == Approx(-0.5));
        CHECK(f.fadeIns.empty());
        CHECK(f.fadeOuts.empty());
    }
    SECTION("from A's start: a fade-in; inside a clip, over a whole clip, or on MIDI: nothing")
    {
        CHECK(stretchFades(clips, {0.0, 1.0}).fadeIns == std::vector<StretchFades::Edge>{{1, 1.0}});
        CHECK(stretchFades(clips, {11.0, 12.0}).empty());
        CHECK(stretchFades(clips, {9.0, 15.0}).empty());
        CHECK(stretchFades(clips, {15.5, 17.0}).empty());
    }
}

TEST_CASE("time edits: section marks follow time taken out and put in, on any stretch (§9)", "[fast]")
{
    // A 0 .. 4, B 4 .. 8, C 8 .. 12.
    SongContent song;
    song.sections = {{1, "A", 0, 0.0, 4.0}, {2, "B", 1, 4.0, 8.0}, {3, "C", 2, 8.0, 12.0}};
    auto section = [](const PlanResult& r, std::uint64_t id) -> const SectionInfo*
    {
        for (const auto& s : r.sections)
            if (s.id == id)
                return &s;
        return nullptr;
    };

    SECTION("deleting 6 .. 10 shortens B and C; nothing overlaps")
    {
        const auto r = applyPlan(song, planDelete(6.0, 10.0));
        REQUIRE(section(r, 2) != nullptr);
        CHECK(section(r, 2)->start == Approx(4.0));
        CHECK(section(r, 2)->end == Approx(6.0));
        REQUIRE(section(r, 3) != nullptr);
        CHECK(section(r, 3)->start == Approx(6.0));
        CHECK(section(r, 3)->end == Approx(8.0));
        CHECK(section(r, 1)->end == Approx(4.0));
    }
    SECTION("deleting all of B and some of C: B goes, C shortens")
    {
        const auto r = applyPlan(song, planDelete(3.0, 9.0));
        CHECK(section(r, 2) == nullptr);
        CHECK(section(r, 1)->end == Approx(3.0));
        CHECK(section(r, 3)->start == Approx(3.0));
        CHECK(section(r, 3)->end == Approx(6.0));
    }
    SECTION("inserting two seconds inside B lengthens it; at C's start, C moves")
    {
        const auto inside = applyPlan(song, planInsert(6.0, 2.0));
        CHECK(section(inside, 2)->start == Approx(4.0));
        CHECK(section(inside, 2)->end == Approx(10.0));
        CHECK(section(inside, 3)->start == Approx(10.0));
        const auto edge = applyPlan(song, planInsert(8.0, 2.0));
        CHECK(section(edge, 2)->end == Approx(8.0));
        CHECK(section(edge, 3)->start == Approx(10.0));
        CHECK(section(edge, 3)->end == Approx(14.0));
    }
    SECTION("duplicating 4 .. 8 (B exactly) copies B; duplicating 6 .. 10 copies nothing whole, B and C grow "
            "apart")
    {
        const auto whole = applyPlan(song, planCopy(4.0, 8.0, 8.0));
        CHECK(whole.sections.size() == 4);
        const auto part = applyPlan(song, planCopy(6.0, 10.0, 10.0));
        REQUIRE(part.sections.size() == 3);
        CHECK(section(part, 2)->end == Approx(8.0));
        CHECK(section(part, 3)->start == Approx(8.0));
        CHECK(section(part, 3)->end == Approx(16.0)); // the copy went in inside C
    }
}
