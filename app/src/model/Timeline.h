#pragma once

#include "model/Grid.h"
#include "model/TempoMap.h"

#include <string>
#include <vector>

namespace fd::model
{
/// Horizontal mapping of a time view: seconds <-> pixels, zoom and scroll, the grid and snapping
/// (docs/design/daw-shell.md, "Timeline"). Pure and unit-tested; views own one and draw from it.
class Timeline
{
public:
    static constexpr double minPixelsPerSecond = 0.5;
    static constexpr double maxPixelsPerSecond = 20000.0;
    static constexpr double minDrawSpacing = 7.0;   // px: closer lines (a fixed grid) are thinned on screen
    static constexpr double minLabelSpacing = 56.0; // px between bar labels
    static constexpr double zoomTimeConstant = 0.08; // s, animated zoom

    void setWidth(double pixels) noexcept { width_ = pixels > 1.0 ? pixels : 1.0; }
    [[nodiscard]] double width() const noexcept { return width_; }

    /// The view's grid (ADR 0047): adaptive at a width, or fixed; triplets; snap.
    void setGrid(const GridSettings& grid) noexcept { grid_ = grid; }
    [[nodiscard]] const GridSettings& grid() const noexcept { return grid_; }

    /// Sets the view directly (clamped); stops any animation.
    void setView(double startSeconds, double pixelsPerSecond) noexcept;
    [[nodiscard]] double start() const noexcept { return start_; }
    [[nodiscard]] double end() const noexcept { return start_ + width_ / pps_; }
    [[nodiscard]] double pixelsPerSecond() const noexcept { return pps_; }

    [[nodiscard]] double xForTime(double seconds) const noexcept { return (seconds - start_) * pps_; }
    [[nodiscard]] double timeForX(double x) const noexcept { return start_ + x / pps_; }

    /// Scrolls by `dx` pixels (positive = later). Stops any animation.
    void scrollBy(double dx) noexcept;

    /// Zooms by `factor` (> 1 = in) keeping the time under `anchorX` in place. Immediate:
    /// for wheel and pinch, which are continuous already. Stops any animation.
    void zoomAround(double anchorX, double factor) noexcept;

    /// The same, eased over a few frames (buttons and keys). Repeated calls compound.
    void animateZoomAround(double anchorX, double factor) noexcept;

    /// Shows [from, to] with a small margin, eased.
    void animateZoomTo(double from, double to) noexcept;

    /// Advances an animation by `dt` seconds. Returns true while it is still moving.
    bool advance(double dt) noexcept;
    [[nodiscard]] bool isAnimating() const noexcept { return animating_; }

    // ---- grid -----------------------------------------------------------------------------

    enum class LineKind { Bar, Beat, Subdivision };
    struct GridLine
    {
        double seconds = 0.0;
        LineKind kind = LineKind::Bar;
        int bar = 0;        // 1-based bar number for Bar lines
        bool label = false; // bar lines that get a number
    };

    /// The step the grid snaps to now, in quarter notes: adaptive, the finest step of the ladder
    /// whose lines are at least the width's spacing apart; fixed, its value. Triplets make a step
    /// shorter than a bar two thirds as long.
    [[nodiscard]] double gridStepBeats(const TempoMap& tempo) const noexcept;
    /// That step's name: "1/16", "1/8T", "1 bar", "2 bars".
    [[nodiscard]] std::string gridStepName(const TempoMap& tempo) const;
    /// The fixed value nearest the step showing now (Fixed grid starts there, so nothing moves).
    [[nodiscard]] GridSettings::Value fixedValueNearStep(const TempoMap& tempo) const noexcept;

    /// Grid lines in [start, end] (cleared and refilled; reuse the vector across frames). Lines
    /// below a bar restart at every bar; lines closer than minDrawSpacing are thinned.
    void gridLines(const TempoMap& tempo, std::vector<GridLine>& out) const;

    /// The nearest line of the grid (its step, though thinned lines aren't drawn).
    [[nodiscard]] double snap(double seconds, const TempoMap& tempo) const noexcept;
    /// The line at or before `seconds`.
    [[nodiscard]] double snapFloor(double seconds, const TempoMap& tempo) const noexcept;

private:
    void clampView() noexcept;

    double width_ = 1000.0;
    GridSettings grid_;
    double start_ = 0.0;
    double pps_ = 50.0;

    bool animating_ = false;
    double targetPps_ = 50.0;
    double anchorX_ = 0.0, anchorTime_ = 0.0;
};
} // namespace fd::model
