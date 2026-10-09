// Automation lanes' curve and point editing (M2 step C4, docs/design/automation.md).

#include "model/Automation.h"

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

TEST_CASE("automation: duplicating and drawing replace what lies in their range", "[fast]")
{
    const AutomationPoints pts{{0.0, 0.1f, 0.0f}, {0.5, 0.9f, 0.0f}, {1.5, 0.3f, 0.0f}};
    // Points at 0 and 0.5 span half a second: one step of 1 s; the copy replaces the point at 1.5.
    auto e = duplicatePoints(pts, {0, 1}, 1.0, {});
    REQUIRE(e.points.size() == 4);
    CHECK(e.points[2] == AutomationPoint{1.0, 0.1f, 0.0f});
    CHECK(e.points[3] == AutomationPoint{1.5, 0.9f, 0.0f});
    CHECK(e.selection == std::set<size_t>{2, 3});

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
