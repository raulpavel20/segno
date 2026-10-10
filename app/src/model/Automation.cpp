#include "model/Automation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fd::model
{
namespace
{
// tracktion_core's Bezier helpers (utilities/tracktion_Bezier.h), reproduced so that lanes draw
// what plays; the integration tests compare this curve with the engine's.

std::pair<double, double> bezierPoint(double x1, double y1, double x2, double y2, double c)
{
    const double run = x2 - x1;
    const double xc = x1 + run / 2;
    if (y2 > y1)
    {
        const double rise = y2 - y1;
        const double yc = y1 + rise / 2;
        return {xc - run / 2 * -c, yc + rise / 2 * -c};
    }
    const double rise = y1 - y2;
    const double yc = y2 + rise / 2;
    return {xc - run / 2 * -c, yc - rise / 2 * -c};
}

struct Ends
{
    double x1, x2;
    float y1, y2;
};

Ends bezierEnds(double x1, double y1, double x2, double y2, double c)
{
    const double minic = (std::abs(c) - 0.5f) * 2.0f;
    const double run = minic * (x2 - x1);
    const double rise = minic * ((y2 > y1) ? (y2 - y1) : (y1 - y2));
    if (c > 0)
        return {x1 + run, x2, static_cast<float>(y1), static_cast<float>(y1 < y2 ? (y2 - rise) : (y2 + rise))};
    return {x1, x2 - run, static_cast<float>(y1 < y2 ? (y1 + rise) : (y1 - rise)), static_cast<float>(y2)};
}

double bezierYFromX(double x, double x1, double y1, double xb, double yb, double x2, double y2)
{
    if (x1 == x2 || y1 == y2)
        return y1;
    if (x <= x1)
        return y1;
    if (x >= x2)
        return y2;
    const double a = x1 - 2 * xb + x2;
    const double b = -2 * x1 + 2 * xb;
    const double c = x1 - x;
    double t;
    if (a == 0)
        t = -c / b;
    else
    {
        t = (-b + std::sqrt(b * b - 4 * a * c)) / (2 * a);
        if (t < 0.0f || t > 1.0f)
            t = (-b - std::sqrt(b * b - 4 * a * c)) / (2 * a);
    }
    return std::pow(1 - t, 2) * y1 + 2 * t * (1 - t) * yb + std::pow(t, 2) * y2;
}

float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

void sortKeepingSelection(PointEdit& edit)
{
    // Stable by time; the selection follows its points.
    std::vector<size_t> order(edit.points.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return edit.points[a].time < edit.points[b].time; });
    AutomationPoints sorted;
    std::set<size_t> selection;
    for (size_t i = 0; i < order.size(); ++i)
    {
        sorted.push_back(edit.points[order[i]]);
        if (edit.selection.contains(order[i]))
            selection.insert(i);
    }
    edit.points = std::move(sorted);
    edit.selection = std::move(selection);
}

float curveFor(const LaneRules& rules)
{
    return rules.stepped() ? kStepCurve : 0.0f;
}
} // namespace

float AssignmentRange::toTravel(float lane) const noexcept
{
    return from + clamp01(lane) * (to - from);
}

std::optional<float> AssignmentRange::toLane(float travel) const noexcept
{
    if (std::abs(to - from) < 1e-6f)
        return std::nullopt;
    return clamp01((travel - from) / (to - from));
}

bool AssignmentRange::full() const noexcept
{
    return std::abs(from) < 1e-6f && std::abs(to - 1.0f) < 1e-6f;
}

AutomationPoints mapValues(const AutomationPoints& points, const std::function<float(float)>& f)
{
    AutomationPoints out = points;
    for (auto& p : out)
        p.value = f(p.value);
    return out;
}

AutomationPoints replaceRange(const AutomationPoints& points, double t0, double t1, AutomationPoints with)
{
    AutomationPoints out;
    for (const auto& p : points)
        if (p.time < t0 || p.time > t1)
            out.push_back(p);
    out.insert(out.end(), with.begin(), with.end());
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return out;
}

AutomationPoints spliceRecorded(const AutomationPoints& lane, const AutomationPoints& derived,
                                const AutomationPoints& recorded, const std::function<float(float)>& toLane)
{
    constexpr double eps = 1e-6;
    auto same = [](const AutomationPoint& a, const AutomationPoint& b) {
        return std::abs(a.time - b.time) < eps && std::abs(a.value - b.value) < 1e-6f && a.curve == b.curve;
    };
    const size_t n = std::min(lane.size(), derived.size());
    size_t prefix = 0;
    while (prefix < n && prefix < recorded.size() && same(recorded[prefix], derived[prefix]))
        ++prefix;
    size_t suffix = 0;
    while (suffix < n - prefix && suffix < recorded.size() - prefix &&
           same(recorded[recorded.size() - 1 - suffix], derived[n - 1 - suffix]))
        ++suffix;

    AutomationPoints out(lane.begin(), lane.begin() + static_cast<std::ptrdiff_t>(prefix));
    for (size_t i = prefix; i < recorded.size() - suffix; ++i)
    {
        const auto& r = recorded[i];
        // A point the pass didn't touch (between a loop's laps) stays the lane's own.
        const auto it = std::find_if(derived.begin() + static_cast<std::ptrdiff_t>(prefix),
                                     derived.begin() + static_cast<std::ptrdiff_t>(n - suffix),
                                     [&](const auto& d) { return same(d, r); });
        if (it != derived.begin() + static_cast<std::ptrdiff_t>(n - suffix))
            out.push_back(lane[static_cast<size_t>(it - derived.begin())]);
        else
            out.push_back({r.time, toLane(r.value), r.curve});
    }
    out.insert(out.end(), lane.begin() + static_cast<std::ptrdiff_t>(n - suffix), lane.begin() + static_cast<std::ptrdiff_t>(n));
    return out;
}

float automationValueAt(const AutomationPoints& points, double seconds, float fallback)
{
    if (points.empty())
        return fallback;
    const auto it = std::upper_bound(points.begin(), points.end(), seconds,
                                     [](double t, const AutomationPoint& p) { return t < p.time; });
    const auto index = static_cast<size_t>(it - points.begin());
    if (index == 0)
        return points.front().value;
    if (index >= points.size())
        return points.back().value;

    const auto& p1 = points[index - 1];
    const auto& p2 = points[index];
    return segmentValueAt(p1.time, p1.value, p2.time, p2.value, p1.curve, seconds);
}

float segmentValueAt(double t1, float v1, double t2, float v2, float curve, double seconds) noexcept
{
    if (curve == 0.0f)
    {
        const auto alpha = static_cast<float>((seconds - t1) / (t2 - t1));
        return v1 + alpha * (v2 - v1);
    }
    const auto [bx, by] = bezierPoint(t1, v1, t2, v2, std::clamp(curve * 2.0f, -1.0f, 1.0f));
    const auto yb = static_cast<float>(by); // the engine keeps the control point's value as a float
    if (curve >= -0.5f && curve <= 0.5f)
        return static_cast<float>(bezierYFromX(seconds, t1, v1, bx, yb, t2, v2));

    const auto ends = bezierEnds(t1, v1, t2, v2, curve);
    if (seconds >= t1 && seconds <= ends.x1)
        return v1;
    if (seconds >= ends.x2 && seconds <= t2)
        return v2;
    return static_cast<float>(bezierYFromX(seconds, ends.x1, ends.y1, bx, yb, ends.x2, ends.y2));
}

float bendOf(const AutomationPoint& point) noexcept
{
    return point.curve >= -0.5f && point.curve <= 0.5f ? point.curve * 2.0f : 0.0f;
}

float curveForBend(float bend) noexcept
{
    return std::clamp(bend, -1.0f, 1.0f) * 0.5f;
}

float snapToSteps(float value, int steps) noexcept
{
    value = clamp01(value);
    if (steps < 2)
        return value;
    const float last = static_cast<float>(steps - 1);
    return std::round(value * last) / last;
}

PointEdit addPoint(const AutomationPoints& points, double seconds, float value, const LaneRules& rules)
{
    PointEdit edit{points, {}};
    const auto it = std::upper_bound(edit.points.begin(), edit.points.end(), seconds,
                                     [](double t, const AutomationPoint& p) { return t < p.time; });
    const auto index = static_cast<size_t>(it - edit.points.begin());
    edit.points.insert(it, {std::max(0.0, seconds), snapToSteps(value, rules.steps), curveFor(rules)});
    edit.selection = {index};
    return edit;
}

PointEdit movePoints(const AutomationPoints& points, const std::set<size_t>& selection, double seconds, float value,
                     const LaneRules& rules)
{
    PointEdit edit{points, selection};
    if (selection.empty())
        return edit;
    // Limit the move in time so that no selected point passes an unselected neighbour, or 0.
    double lo = -1e300, hi = 1e300;
    for (const auto i : selection)
    {
        if (i >= points.size())
            continue;
        lo = std::max(lo, -points[i].time);
        for (size_t j = i; j-- > 0;)
            if (!selection.contains(j))
            {
                lo = std::max(lo, points[j].time - points[i].time);
                break;
            }
        for (size_t j = i + 1; j < points.size(); ++j)
            if (!selection.contains(j))
            {
                hi = std::min(hi, points[j].time - points[i].time);
                break;
            }
    }
    const double dt = std::clamp(seconds, std::min(lo, 0.0), std::max(hi, 0.0));
    for (const auto i : selection)
        if (i < edit.points.size())
        {
            edit.points[i].time += dt;
            edit.points[i].value = snapToSteps(edit.points[i].value + value, rules.steps);
        }
    sortKeepingSelection(edit);
    return edit;
}

PointEdit moveSegment(const AutomationPoints& points, size_t index, float value, const LaneRules& rules)
{
    if (index + 1 >= points.size())
        return {points, {}};
    return movePoints(points, {index, index + 1}, 0.0, value, rules);
}

PointEdit bendSegment(const AutomationPoints& points, size_t index, float bend, const LaneRules& rules)
{
    PointEdit edit{points, {}};
    if (index + 1 < points.size() && !rules.stepped())
        edit.points[index].curve = curveForBend(bend);
    return edit;
}

PointEdit deletePoints(const AutomationPoints& points, const std::set<size_t>& selection)
{
    PointEdit edit;
    for (size_t i = 0; i < points.size(); ++i)
        if (!selection.contains(i))
            edit.points.push_back(points[i]);
    return edit;
}

PointEdit duplicatePoints(const AutomationPoints& points, const std::set<size_t>& selection, double stepSeconds,
                          const LaneRules& rules)
{
    if (selection.empty() || points.empty())
        return {points, selection};
    double first = 1e300, last = -1e300;
    for (const auto i : selection)
        if (i < points.size())
        {
            first = std::min(first, points[i].time);
            last = std::max(last, points[i].time);
        }
    const double step = stepSeconds > 0.0 ? stepSeconds : 1.0;
    const double by = std::max(1.0, std::ceil((last - first) / step - 1e-9)) * step;
    AutomationPoints copies;
    for (const auto i : selection)
        if (i < points.size())
            copies.push_back({points[i].time + by, points[i].value, points[i].curve});
    // The copies replace whatever lay in their range.
    PointEdit edit{{}, {}};
    for (const auto& p : points)
        if (p.time < first + by || p.time > last + by)
            edit.points.push_back(p);
    const size_t start = edit.points.size();
    for (auto& c : copies)
    {
        c.value = snapToSteps(c.value, rules.steps);
        edit.points.push_back(c);
    }
    for (size_t i = start; i < edit.points.size(); ++i)
        edit.selection.insert(i);
    sortKeepingSelection(edit);
    return edit;
}

PointEdit drawPoints(const AutomationPoints& points, AutomationPoints drawn, const LaneRules& rules)
{
    if (drawn.empty())
        return {points, {}};
    std::stable_sort(drawn.begin(), drawn.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    const double t0 = drawn.front().time, t1 = drawn.back().time;
    PointEdit edit;
    for (const auto& p : points)
        if (p.time < t0 || p.time > t1)
            edit.points.push_back(p);
    const size_t start = edit.points.size();
    for (auto p : drawn)
    {
        p.time = std::max(0.0, p.time);
        p.value = snapToSteps(p.value, rules.steps);
        p.curve = curveFor(rules);
        edit.points.push_back(p);
    }
    for (size_t i = start; i < edit.points.size(); ++i)
        edit.selection.insert(i);
    sortKeepingSelection(edit);
    return edit;
}

std::set<size_t> pointsInBox(const AutomationPoints& points, double t0, double t1, float v0, float v1)
{
    if (t1 < t0)
        std::swap(t0, t1);
    if (v1 < v0)
        std::swap(v0, v1);
    std::set<size_t> inside;
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i].time >= t0 && points[i].time <= t1 && points[i].value >= v0 && points[i].value <= v1)
            inside.insert(i);
    return inside;
}

std::optional<size_t> hitPoint(const AutomationPoints& points, const LaneMapping& map, double px, double py,
                               double radius)
{
    std::optional<size_t> best;
    double bestDistance = radius * radius;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const double dx = map.x(points[i].time) - px, dy = map.y(points[i].value) - py;
        if (const double d = dx * dx + dy * dy; d <= bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

std::optional<size_t> hitSegment(const AutomationPoints& points, const LaneMapping& map, double px, double py,
                                 double tolerance)
{
    if (points.size() < 2)
        return std::nullopt;
    const double t = map.time(px);
    const auto it = std::upper_bound(points.begin(), points.end(), t,
                                     [](double time, const AutomationPoint& p) { return time < p.time; });
    const auto index = static_cast<size_t>(it - points.begin());
    if (index == 0 || index >= points.size())
        return std::nullopt; // outside the points: the flat runs before and after aren't segments
    if (std::abs(map.y(automationValueAt(points, t)) - py) > tolerance)
        return std::nullopt;
    return index - 1;
}
} // namespace fd::model
