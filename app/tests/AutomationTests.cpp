// Automation lanes' curve and point editing (M2 step C4, docs/design/automation.md).

#include "model/Automation.h"
#include "model/Snapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

TEST_CASE("automation: the curve holds outside its points and is straight between them", "[fast]")
{
    CHECK(automationValueAt({}, 1.0, 0.25f) == 0.25f);
    const AutomationPoints one{{2.0, 0.6f, 0.0f}};
    CHECK(automationValueAt(one, 0.0) == 0.6f); // a single point holds everywhere
    CHECK(automationValueAt(one, 9.0) == 0.6f);

    const AutomationPoints line{{1.0, 0.2f, 0.0f}, {3.0, 0.6f, 0.0f}};
    CHECK(automationValueAt(line, 0.0) == 0.2f);
    CHECK(automationValueAt(line, 1.0) == 0.2f);
    CHECK(automationValueAt(line, 2.0) == Approx(0.4f));
    CHECK(automationValueAt(line, 2.5) == Approx(0.5f));
    CHECK(automationValueAt(line, 3.0) == 0.6f);
    CHECK(automationValueAt(line, 5.0) == 0.6f);
}

TEST_CASE("automation: bends curve a segment between its ends; steps hold until the next point", "[fast]")
{
    const AutomationPoints up{{0.0, 0.0f, curveForBend(1.0f)}, {1.0, 1.0f, 0.0f}};
    const AutomationPoints down{{0.0, 0.0f, curveForBend(-1.0f)}, {1.0, 1.0f, 0.0f}};
    // A positive bend moves late (below the line at the middle), a negative one early (above).
    CHECK(automationValueAt(up, 0.5) < 0.4f);
    CHECK(automationValueAt(down, 0.5) > 0.6f);
    for (double t = 0.0; t <= 1.0; t += 0.05) // monotonic and inside the ends
    {
        CHECK(automationValueAt(up, t) >= 0.0f);
        CHECK(automationValueAt(up, t) <= 1.0f);
        CHECK(automationValueAt(up, t + 0.05) >= automationValueAt(up, t) - 1e-6f);
    }
    CHECK(bendOf(up[0]) == Approx(1.0f));
    CHECK(bendOf(down[0]) == Approx(-1.0f));

    const AutomationPoints steps{{0.0, 0.0f, kStepCurve}, {1.0, 1.0f, kStepCurve}, {2.0, 0.5f, kStepCurve}};
    CHECK(automationValueAt(steps, 0.5) == 0.0f);
    CHECK(automationValueAt(steps, 0.999) == 0.0f);
    CHECK(automationValueAt(steps, 1.0) == 1.0f);
    CHECK(automationValueAt(steps, 1.5) == 1.0f);
    CHECK(automationValueAt(steps, 2.5) == 0.5f);
}

TEST_CASE("automation: points are added in order and stepped lanes snap", "[fast]")
{
    const AutomationPoints line{{1.0, 0.2f, 0.0f}, {3.0, 0.6f, 0.0f}};
    auto e = addPoint(line, 2.0, 0.9f, {});
    REQUIRE(e.points.size() == 3);
    CHECK(e.points[1] == AutomationPoint{2.0, 0.9f, 0.0f});
    CHECK(e.selection == std::set<size_t>{1});

    const LaneRules toggle{.steps = 2};
    e = addPoint({}, 0.5, 0.7f, toggle);
    CHECK(e.points[0] == AutomationPoint{0.5, 1.0f, kStepCurve});
    CHECK(snapToSteps(0.3f, 3) == Approx(0.5f));
    CHECK(snapToSteps(1.4f, 0) == 1.0f);
}

TEST_CASE("automation: moved points keep their order and stay in the lane", "[fast]")
{
    const AutomationPoints pts{{0.0, 0.5f, 0.0f}, {1.0, 0.5f, 0.0f}, {2.0, 0.5f, 0.0f}, {3.0, 0.5f, 0.0f}};
    // The middle point can't pass its neighbours.
    auto e = movePoints(pts, {1}, 5.0, 0.2f, {});
    CHECK(e.points[1].time == Approx(2.0));
    CHECK(e.points[1].value == Approx(0.7f));
    e = movePoints(pts, {1}, -5.0, 0.0f, {});
    CHECK(e.points[1].time == Approx(0.0));
    // A selected group moves together until its outer neighbour.
    e = movePoints(pts, {1, 2}, 4.0, 0.9f, {});
    CHECK(e.points[1].time == Approx(2.0));
    CHECK(e.points[2].time == Approx(3.0));
    CHECK(e.points[1].value == 1.0f); // clamped
    CHECK(e.selection == std::set<size_t>{1, 2});
    // Nothing goes before zero.
    e = movePoints(pts, {0, 1, 2, 3}, -2.0, 0.0f, {});
    CHECK(e.points[0].time == Approx(0.0));
    CHECK(e.points[3].time == Approx(3.0));
}

TEST_CASE("automation: segments move, bend and points delete", "[fast]")
{
    const AutomationPoints pts{{0.0, 0.2f, 0.0f}, {1.0, 0.4f, 0.0f}, {2.0, 0.6f, 0.0f}};
    auto e = moveSegment(pts, 1, 0.1f, {});
    CHECK(e.points[0].value == Approx(0.2f));
    CHECK(e.points[1].value == Approx(0.5f));
    CHECK(e.points[2].value == Approx(0.7f));
    CHECK(moveSegment(pts, 2, 0.1f, {}).points == pts); // the last point starts no segment

    e = bendSegment(pts, 0, 0.6f, {});
    CHECK(bendOf(e.points[0]) == Approx(0.6f));
    CHECK(bendSegment(pts, 0, 0.6f, {.steps = 4}).points == pts); // step lanes don't bend

    e = deletePoints(pts, {0, 2});
    REQUIRE(e.points.size() == 1);
    CHECK(e.points[0].time == 1.0);
}

TEST_CASE("automation: copied points paste from a time, replacing what lies there; steps on a stepped lane",
          "[fast]")
{
    const AutomationPoints lane{{1.0, 0.2f, 0.0f}, {2.0, 0.6f, 0.0f}, {3.0, 0.9f, 0.0f}, {6.0, 0.1f, 0.0f}};
    const auto held = copyPoints(lane, {2, 0}); // any order
    REQUIRE(held.size() == 2);
    CHECK(held[0].time == 0.0); // from the first
    CHECK(held[1].time == 2.0);
    CHECK(held[1].value == 0.9f);
    CHECK(copyPoints(lane, {}).empty());

    // Pasted at 5: the point at 6 lies in their range (5 to 7) and goes; the pasted are selected.
    const auto pasted = pastePoints(lane, held, 5.0, {});
    REQUIRE(pasted.points.size() == 5);
    CHECK(pasted.points[3].time == 5.0);
    CHECK(pasted.points[4].time == 7.0);
    CHECK(pasted.selection == std::set<size_t>{3, 4});
    // Never before 0.
    CHECK(pastePoints(lane, held, -3.0, {}).points.front().time == 0.0);
    // A stepped lane takes them as its steps.
    const auto stepped = pastePoints({}, held, 0.0, {.steps = 2});
    REQUIRE(stepped.points.size() == 2);
    CHECK(stepped.points[0].value == 0.0f);
    CHECK(stepped.points[1].value == 1.0f);
    CHECK(stepped.points[0].curve == kStepCurve);
    // Nothing held: nothing changes.
    CHECK(pastePoints(lane, {}, 1.0, {}).points == lane);
}

TEST_CASE("automation: duplicating and drawing replace what lies in their range", "[fast]")
{
    const AutomationPoints pts{{0.0, 0.1f, 0.0f}, {0.5, 0.9f, 0.0f}, {1.5, 0.3f, 0.0f}};
    // Points at 0 and 0.5 span half a second: one step of 1 s; the copy replaces the point at 1.5.
    auto e = duplicatePoints(pts, {0, 1}, 1.0, {});
    REQUIRE(e.points.size() == 4);
    CHECK(e.points[2] == AutomationPoint{1.0, 0.1f, 0.0f});
    CHECK(e.points[3] == AutomationPoint{1.5, 0.9f, 0.0f});
    CHECK(e.selection == std::set<size_t>{2, 3});

    // A span of whole steps: the first copy lands on the last point, and both stay (a jump).
    const AutomationPoints whole{{1.0, 0.25f, 0.0f}, {2.0, 0.75f, 0.0f}};
    e = duplicatePoints(whole, {0, 1}, 1.0, {});
    REQUIRE(e.points.size() == 4);
    CHECK(e.points[1] == AutomationPoint{2.0, 0.75f, 0.0f}); // the original, then its copy's start
    CHECK(e.points[2] == AutomationPoint{2.0, 0.25f, 0.0f});
    CHECK(e.points[3] == AutomationPoint{3.0, 0.75f, 0.0f});
    CHECK(e.selection == std::set<size_t>{2, 3});
    CHECK(automationValueAt(e.points, 1.999) == Approx(0.75f).margin(0.01f));
    CHECK(automationValueAt(e.points, 2.5) == Approx(0.5f));

    e = drawPoints(pts, {{1.0, 0.4f, 0.0f}, {0.25, 0.2f, 0.0f}}, {});
    REQUIRE(e.points.size() == 4); // 0.5 was inside the drawn range
    CHECK(e.points[1].time == 0.25);
    CHECK(e.points[2].time == 1.0);
    CHECK(e.points[3].time == 1.5);
    CHECK(e.selection == std::set<size_t>{1, 2});
}

TEST_CASE("automation: hit testing in pixels", "[fast]")
{
    const AutomationPoints pts{{1.0, 0.0f, 0.0f}, {2.0, 1.0f, 0.0f}};
    const LaneMapping map{.x = [](double t) { return t * 100.0; },
                          .time = [](double x) { return x / 100.0; },
                          .y = [](float v) { return 100.0 - v * 100.0; }};
    CHECK(hitPoint(pts, map, 102.0, 98.0, 6.0) == size_t{0});
    CHECK_FALSE(hitPoint(pts, map, 150.0, 50.0, 6.0));
    CHECK(hitSegment(pts, map, 150.0, 52.0, 4.0) == size_t{0});
    CHECK_FALSE(hitSegment(pts, map, 150.0, 70.0, 4.0));
    CHECK_FALSE(hitSegment(pts, map, 50.0, 100.0, 4.0)); // before the first point
    CHECK(pointsInBox(pts, 0.5, 1.5, -0.1f, 0.5f) == std::set<size_t>{0});
}

TEST_CASE("automation: an assignment's range maps the lane onto part of a control's travel", "[fast]")
{
    const AssignmentRange full;
    CHECK(full.full());
    CHECK(full.toTravel(0.3f) == Approx(0.3f));

    const AssignmentRange part{0.2f, 0.6f};
    CHECK_FALSE(part.full());
    CHECK(part.toTravel(0.0f) == Approx(0.2f));
    CHECK(part.toTravel(0.5f) == Approx(0.4f));
    CHECK(part.toTravel(2.0f) == Approx(0.6f)); // lane values are clamped
    CHECK(*part.toLane(0.4f) == Approx(0.5f));
    CHECK(*part.toLane(0.9f) == Approx(1.0f)); // outside the range: its nearest end

    const AssignmentRange inverted{1.0f, 0.0f};
    CHECK(inverted.toTravel(0.25f) == Approx(0.75f));
    CHECK(*inverted.toLane(0.75f) == Approx(0.25f));

    CHECK_FALSE((AssignmentRange{0.5f, 0.5f}.toLane(0.5f)));
}

TEST_CASE("automation: a curve mapped through a range plays the range of the curve, bends included", "[fast]")
{
    // The engine's bends are linear in values, so a + k·v of the curve is the curve of a + k·v,
    // also inverted (k < 0) and with bends past ±0.5 (the engine's sharper shape).
    AutomationPoints pts;
    unsigned seed = 12345;
    auto random = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
    };
    for (int i = 0; i < 24; ++i)
        pts.push_back({i * 0.5, random(), curveForBend(random() * 2.0f - 1.0f)});
    pts[7].curve = kStepCurve;

    for (const AssignmentRange range : {AssignmentRange{0.1f, 0.7f}, AssignmentRange{0.9f, 0.2f}, AssignmentRange{1.0f, 0.0f}})
    {
        const auto mapped = mapValues(pts, [&](float v) { return range.toTravel(v); });
        REQUIRE(mapped.size() == pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
        {
            CHECK(mapped[i].time == pts[i].time);
            CHECK(mapped[i].curve == pts[i].curve);
        }
        for (double t = -0.5; t < 12.5; t += 0.013)
            CHECK(automationValueAt(mapped, t) == Approx(range.toTravel(automationValueAt(pts, t))).margin(1e-5));
    }
}

TEST_CASE("automation: replacing a time range keeps what lies outside it", "[fast]")
{
    const AutomationPoints pts{{0.0, 0.1f, 0.0f}, {1.0, 0.2f, 0.0f}, {2.0, 0.3f, 0.0f}, {3.0, 0.4f, 0.0f}};
    const auto out = replaceRange(pts, 1.0, 2.0, {{1.5, 0.9f, 0.0f}, {1.2, 0.8f, 0.0f}});
    REQUIRE(out.size() == 4);
    CHECK(out[0] == pts[0]);
    CHECK(out[1] == AutomationPoint{1.2, 0.8f, 0.0f});
    CHECK(out[2] == AutomationPoint{1.5, 0.9f, 0.0f});
    CHECK(out[3] == pts[3]);
}

TEST_CASE("automation: a recorded pass splices into its lane, mapped back through the range", "[fast]")
{
    const AssignmentRange range{0.2f, 0.6f};
    auto forward = [&](float v) { return range.toTravel(v); };
    auto back = [&](float v) { return *range.toLane(v); };
    // Lane values whose round trip through the range isn't exact in floats: kept points must be
    // the lane's own, not mapped back.
    const AutomationPoints lane{{0.0, 0.1f, 0.0f}, {1.0, 0.333f, curveForBend(0.4f)}, {2.0, 0.7f, 0.0f},
                                {3.0, 0.9f, 0.0f}, {4.0, 0.111f, 0.0f}, {5.0, 0.77f, 0.0f}};
    const auto derived = mapValues(lane, forward);

    SECTION("one pass in the middle")
    {
        auto recorded = replaceRange(derived, 1.5, 3.5, {{1.5, 0.3f, 0.0f}, {2.5, 0.5f, 0.0f}, {3.5, 0.4f, 0.0f}});
        const auto out = spliceRecorded(lane, derived, recorded, back);
        REQUIRE(out.size() == 7);
        CHECK(out[0] == lane[0]);
        CHECK(out[1] == lane[1]);
        CHECK(out[2].time == 1.5);
        CHECK(out[2].value == Approx(0.25f));
        CHECK(out[3].value == Approx(0.75f));
        CHECK(out[4].value == Approx(0.5f));
        CHECK(out[5] == lane[4]);
        CHECK(out[6] == lane[5]);
    }
    SECTION("a loop's two laps keep the points between them")
    {
        auto recorded = replaceRange(derived, 0.0, 1.5, {{0.0, 0.6f, 0.0f}, {1.5, 0.6f, 0.0f}});
        recorded = replaceRange(recorded, 3.5, 5.0, {{3.5, 0.2f, 0.0f}, {5.0, 0.2f, 0.0f}});
        const auto out = spliceRecorded(lane, derived, recorded, back);
        REQUIRE(out.size() == 6);
        CHECK(out[0].value == Approx(1.0f));
        CHECK(out[1].value == Approx(1.0f));
        CHECK(out[2] == lane[2]);
        CHECK(out[3] == lane[3]);
        CHECK(out[4].value == Approx(0.0f));
        CHECK(out[5].value == Approx(0.0f));
    }
    SECTION("an empty lane takes the whole pass")
    {
        const auto out = spliceRecorded({}, {}, {{1.0, 0.4f, 0.0f}, {2.0, 0.6f, 0.0f}}, back);
        REQUIRE(out.size() == 2);
        CHECK(out[0].value == Approx(0.5f));
        CHECK(out[1].value == Approx(1.0f));
    }
    SECTION("an untouched curve is the lane exactly")
    {
        CHECK(spliceRecorded(lane, derived, derived, back) == lane);
    }
}

TEST_CASE("automation: a lane's name is its own, and it edits as steps for one stepped one", "[fast]")
{
    // Lanes are numbered when made (tracks-and-clips.md §18), not named after what they drive.
    AutomationLaneInfo lane{.id = 1, .name = "Automation 1"};
    CHECK(lane.displayName() == "Automation 1");
    CHECK(lane.allOn());
    lane.assignments.push_back({.channel = 2, .key = "m3/mode", .name = "Mode", .steps = 3});
    CHECK(lane.displayName() == "Automation 1");
    CHECK(lane.rules().steps == 3);
    lane.assignments[0].range = {0.0f, 0.5f}; // part of a stepped parameter: it curves
    CHECK_FALSE(lane.rules().stepped());
    lane.assignments[0].range = {};
    lane.assignments.push_back({.channel = 2, .key = "volume", .on = false, .name = "Volume"});
    CHECK(lane.displayName() == "Automation 1");
    CHECK_FALSE(lane.rules().stepped());
    CHECK(lane.anyOff());
    CHECK(lane.assignment(2, "volume")->name == "Volume");
    CHECK(lane.assignment(3, "volume") == nullptr);
    lane.name = "Sweep";
    CHECK(lane.displayName() == "Sweep");
}
