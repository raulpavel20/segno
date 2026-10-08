#pragma once

#include "model/TempoMap.h"

#include <vector>

namespace fd::model
{
/// Horizontal mapping of the arrangement: seconds <-> pixels, zoom and scroll, the adaptive
/// grid and snapping (docs/design/daw-shell.md, "Timeline"). Pure and unit-tested; views own one
/// and draw from it.
class Timeline
{
public:
    static constexpr double minPixelsPerSecond = 0.5;
    static constexpr double maxPixelsPerSecond = 20000.0;
    static constexpr double minLineSpacing = 10.0;  // px between the finest grid lines
    static constexpr double minLabelSpacing = 56.0; // px between bar labels
    static constexpr double zoomTimeConstant = 0.08; // s, animated zoom

    void setWidth(double pixels) noexcept { width_ = pixels > 1.0 ? pixels : 1.0; }
    [[nodiscard]] double width() const noexcept { return width_; }

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

    /// The current grid step in quarter notes: the finest step of the ladder whose lines are
    /// at least minLineSpacing apart.
    [[nodiscard]] double gridStepBeats(const TempoMap& tempo) const noexcept;

    /// Grid lines in [start, end] (cleared and refilled; reuse the vector across frames).
    void gridLines(const TempoMap& tempo, std::vector<GridLine>& out) const;

    /// The nearest line of the current grid.
    [[nodiscard]] double snap(double seconds, const TempoMap& tempo) const noexcept;

private:
    void clampView() noexcept;

    double width_ = 1000.0;
    double start_ = 0.0;
    double pps_ = 50.0;

    bool animating_ = false;
    double targetPps_ = 50.0;
    double anchorX_ = 0.0, anchorTime_ = 0.0;
};
} // namespace fd::model
