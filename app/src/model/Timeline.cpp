#include "model/Timeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fd::model
{
namespace
{
double clampPps(double pps) noexcept
{
    return std::clamp(pps, Timeline::minPixelsPerSecond, Timeline::maxPixelsPerSecond);
}

// Grid arithmetic in ticks of a 1/32 note (1/8 quarter), so bar and beat tests are exact.
constexpr std::int64_t kTicksPerQuarter = 8;

std::int64_t barTicks(const TempoMap& t) noexcept
{
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(t.numerator) * 32 / std::max(1, t.denominator));
}

std::int64_t beatTicks(const TempoMap& t) noexcept
{
    return std::max<std::int64_t>(1, 32 / std::max(1, t.denominator));
}

/// The step ladder in ticks: 1/32, 1/16, 1/8 note, a beat, then 1, 2, 4 ... 256 bars.
std::int64_t stepTicks(const TempoMap& t, double pixelsPerTick, double spacing) noexcept
{
    const std::array<std::int64_t, 3> subdivisions{1, 2, 4};
    for (auto s : subdivisions)
        if (s < beatTicks(t) && s * pixelsPerTick >= spacing)
            return s;
    if (beatTicks(t) * pixelsPerTick >= spacing)
        return beatTicks(t);
    std::int64_t bars = 1;
    while (bars < 256 && bars * barTicks(t) * pixelsPerTick < spacing)
        bars *= 2;
    return bars * barTicks(t);
}

double pixelsPerTick(const Timeline& tl, const TempoMap& t) noexcept
{
    return tl.pixelsPerSecond() * t.secondsForBeats(1.0) / kTicksPerQuarter;
}
} // namespace

void Timeline::setView(double startSeconds, double pixelsPerSecond) noexcept
{
    start_ = startSeconds;
    pps_ = clampPps(pixelsPerSecond);
    animating_ = false;
    clampView();
}

void Timeline::clampView() noexcept
{
    if (start_ < 0.0 || !std::isfinite(start_))
        start_ = 0.0;
}

void Timeline::scrollBy(double dx) noexcept
{
    animating_ = false;
    start_ += dx / pps_;
    clampView();
}

void Timeline::zoomAround(double anchorX, double factor) noexcept
{
    animating_ = false;
    const double anchorTime = timeForX(anchorX);
    pps_ = clampPps(pps_ * factor);
    start_ = anchorTime - anchorX / pps_;
    clampView();
}

void Timeline::animateZoomAround(double anchorX, double factor) noexcept
{
    const double base = animating_ ? targetPps_ : pps_;
    if (!animating_ || std::abs(anchorX - anchorX_) > 0.5)
    {
        anchorX_ = anchorX;
        anchorTime_ = timeForX(anchorX);
    }
    targetPps_ = clampPps(base * factor);
    animating_ = targetPps_ != pps_;
}

void Timeline::animateZoomTo(double from, double to) noexcept
{
    if (!(to > from))
        return;
    const double margin = (to - from) * 0.02;
    const double target = clampPps(width_ / (to - from + 2.0 * margin));
    const double targetStart = std::max(0.0, from - margin);
    // Choose the fixed point between the two views so both edges move smoothly: the point
    // whose screen position is the same before and after.
    const double denominator = target - pps_;
    if (std::abs(denominator) < 1e-12)
    {
        setView(targetStart, target);
        return;
    }
    anchorTime_ = (target * targetStart - pps_ * start_) / denominator;
    anchorX_ = xForTime(anchorTime_);
    targetPps_ = target;
    animating_ = true;
}

bool Timeline::advance(double dt) noexcept
{
    if (!animating_)
        return false;
    const double k = 1.0 - std::exp(-std::max(0.0, dt) / zoomTimeConstant);
    const double logNow = std::log(pps_), logTarget = std::log(targetPps_);
    double next = std::exp(logNow + (logTarget - logNow) * k);
    if (std::abs(std::log(next / targetPps_)) < 1e-3)
    {
        next = targetPps_;
        animating_ = false;
    }
    pps_ = next;
    start_ = anchorTime_ - anchorX_ / pps_;
    clampView();
    return animating_;
}

double Timeline::gridStepBeats(const TempoMap& tempo) const noexcept
{
    return static_cast<double>(stepTicks(tempo, pixelsPerTick(*this, tempo), lineSpacing_)) / kTicksPerQuarter;
}

void Timeline::gridLines(const TempoMap& tempo, std::vector<GridLine>& out) const
{
    out.clear();
    const double ppt = pixelsPerTick(*this, tempo);
    const std::int64_t step = stepTicks(tempo, ppt, lineSpacing_);
    const std::int64_t bar = barTicks(tempo), beat = beatTicks(tempo);

    // Bar labels every 1, 2, 4 ... bars so they stay readable.
    std::int64_t labelBars = 1;
    while (labelBars * bar * ppt < minLabelSpacing && labelBars < (1 << 20))
        labelBars *= 2;

    const double secondsPerTick = tempo.secondsForBeats(1.0) / kTicksPerQuarter;
    auto first = static_cast<std::int64_t>(std::floor(start_ / secondsPerTick / static_cast<double>(step)));
    const auto last = static_cast<std::int64_t>(std::ceil(end() / secondsPerTick / static_cast<double>(step)));
    first = std::max<std::int64_t>(0, first);
    for (std::int64_t i = first; i <= last; ++i)
    {
        const std::int64_t tick = i * step;
        GridLine line;
        line.seconds = static_cast<double>(tick) * secondsPerTick;
        if (tick % bar == 0)
        {
            line.kind = LineKind::Bar;
            line.bar = static_cast<int>(tick / bar) + 1;
            line.label = (tick / bar) % labelBars == 0;
        }
        else
            line.kind = tick % beat == 0 ? LineKind::Beat : LineKind::Subdivision;
        out.push_back(line);
    }
}

double Timeline::snap(double seconds, const TempoMap& tempo) const noexcept
{
    const double stepSeconds = tempo.secondsForBeats(gridStepBeats(tempo));
    return std::max(0.0, std::round(seconds / stepSeconds) * stepSeconds);
}
} // namespace fd::model
