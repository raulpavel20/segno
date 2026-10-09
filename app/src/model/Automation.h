#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace fd::model
{
// Automation lanes (M2 step C4, docs/design/automation.md): the curve the engine plays and pure
// functions that edit points. Lanes work in the control's units: a value is the control's
// travel, 0 at the bottom of the lane and 1 at the top, so a straight segment is a straight move
// of the knob or fader.

/// One breakpoint. `time` is seconds on the timeline. `curve` is as the engine stores it: 0 is a
/// straight segment to the next point, −0.5 … 0.5 a bend (the UI's bend is twice that, −1 … 1),
/// and 1 a step (the value holds until the next point).
struct AutomationPoint
{
    double time = 0.0;
    float value = 0.0f;
    float curve = 0.0f;

    friend bool operator==(const AutomationPoint&, const AutomationPoint&) = default;
};

/// A lane's points in time order. Two points may share a time (a vertical jump).
using AutomationPoints = std::vector<AutomationPoint>;

/// The engine's curve value for a step.
inline constexpr float kStepCurve = 1.0f;

/// The value at `seconds`, exactly as the engine plays it (tracktion's AutomationCurve::getValueAt,
/// with playback patched to match): before the first point its value, after the last its value,
/// `fallback` without points.
[[nodiscard]] float automationValueAt(const AutomationPoints& points, double seconds, float fallback = 0.0f);

/// The UI's bend (−1 … 1) of a point's segment, and the curve value for a bend.
[[nodiscard]] float bendOf(const AutomationPoint& point) noexcept;
[[nodiscard]] float curveForBend(float bend) noexcept;

/// A stepped parameter's value snapped to its nearest step (`steps` values from 0 to 1; fewer
/// than 2: unchanged), clamped to 0 … 1.
[[nodiscard]] float snapToSteps(float value, int steps) noexcept;

/// An assignment's range (ADR 0029): a lane value v drives its parameter at
/// from + v × (to − from) of the control's travel; `to` below `from` inverts it.
struct AssignmentRange
{
    float from = 0.0f, to = 1.0f;

    [[nodiscard]] float toTravel(float lane) const noexcept;
    /// The lane value that drives `travel`, clamped to the range; nullopt for an empty range.
    [[nodiscard]] std::optional<float> toLane(float travel) const noexcept;
    [[nodiscard]] bool full() const noexcept;

    friend bool operator==(const AssignmentRange&, const AssignmentRange&) = default;
};

/// The points with their values mapped by `f` (times and curves kept). For a linear map
/// (a + k·v, k may be negative) the curve maps exactly: the engine's bends are linear in values.
[[nodiscard]] AutomationPoints mapValues(const AutomationPoints& points, const std::function<float(float)>& f);

/// `points` with those in [t0, t1] replaced by `with`.
[[nodiscard]] AutomationPoints replaceRange(const AutomationPoints& points, double t0, double t1, AutomationPoints with);

/// A recorded pass back into its lane (automation.md §8). `derived` is what the lane wrote to the
/// parameter's curve (its points mapped forward), `recorded` the curve after the pass. Points the
/// two share at the start and the end, and any shared point in between (a loop's laps), stay the
/// lane's own exactly; the rest of `recorded` is mapped back with `toLane`.
[[nodiscard]] AutomationPoints spliceRecorded(const AutomationPoints& lane, const AutomationPoints& derived,
                                              const AutomationPoints& recorded,
                                              const std::function<float(float)>& toLane);

/// The result of an edit: the new points, and which of them are selected.
struct PointEdit
{
    AutomationPoints points;
    std::set<size_t> selection;
};

/// What a lane needs to know to edit its points.
struct LaneRules
{
    int steps = 0; // a stepped parameter's number of values (0: continuous)
    [[nodiscard]] bool stepped() const noexcept { return steps >= 2; }
};

/// A new point (after any at the same time); it is selected. Stepped lanes get step points.
[[nodiscard]] PointEdit addPoint(const AutomationPoints& points, double seconds, float value, const LaneRules& rules);

/// Moves the selected points by `seconds` and `value`. They keep their order: the move in time is
/// limited so none passes an unselected neighbour or goes before 0; values are clamped to 0 … 1
/// (and snapped to steps).
[[nodiscard]] PointEdit movePoints(const AutomationPoints& points, const std::set<size_t>& selection, double seconds,
                                   float value, const LaneRules& rules);

/// Moves both ends of the segment starting at `index` up or down by `value`.
[[nodiscard]] PointEdit moveSegment(const AutomationPoints& points, size_t index, float value, const LaneRules& rules);

/// Sets the bend (−1 … 1) of the segment starting at `index` (not on stepped lanes).
[[nodiscard]] PointEdit bendSegment(const AutomationPoints& points, size_t index, float bend, const LaneRules& rules);

[[nodiscard]] PointEdit deletePoints(const AutomationPoints& points, const std::set<size_t>& selection);

/// Copies the selection after itself, by its span rounded up to whole `stepSeconds` (at least one
/// step); the copies are selected.
[[nodiscard]] PointEdit duplicatePoints(const AutomationPoints& points, const std::set<size_t>& selection,
                                        double stepSeconds, const LaneRules& rules);

/// Draws over a time range: the points from the first drawn time to the last are replaced by the
/// drawn ones (`drawn` in any order; stepped lanes get steps). The drawn points are selected.
[[nodiscard]] PointEdit drawPoints(const AutomationPoints& points, AutomationPoints drawn, const LaneRules& rules);

/// The points inside a box of time and value.
[[nodiscard]] std::set<size_t> pointsInBox(const AutomationPoints& points, double t0, double t1, float v0, float v1);

/// Hit testing in pixels: `x(seconds)` and `y(value)` place a point on screen.
struct LaneMapping
{
    std::function<double(double)> x;   // seconds -> pixels
    std::function<double(double)> time; // pixels -> seconds
    std::function<double(float)> y;    // value -> pixels
};
/// The point nearest to (px, py) within `radius` pixels.
[[nodiscard]] std::optional<size_t> hitPoint(const AutomationPoints& points, const LaneMapping& map, double px, double py,
                                             double radius);
/// The segment (its first point's index) whose line passes within `tolerance` pixels of (px, py).
[[nodiscard]] std::optional<size_t> hitSegment(const AutomationPoints& points, const LaneMapping& map, double px, double py,
                                               double tolerance);
} // namespace fd::model
