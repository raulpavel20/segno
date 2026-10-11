#include "model/Timeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace fd::model
{
namespace
{
double clampPps(double pps) noexcept
{
    return std::clamp(pps, Timeline::minPixelsPerSecond, Timeline::maxPixelsPerSecond);
}

// Grid arithmetic in ticks of 1/24 quarter note, so bars, beats and triplets are exact: a 1/32
// note is 3 ticks, its triplet 2.
constexpr std::int64_t kTicksPerQuarter = 24, kTicksPerWhole = 4 * kTicksPerQuarter;

std::int64_t barTicks(const TempoMap& t) noexcept
{
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(t.numerator) * kTicksPerWhole /
                                         std::max(1, t.denominator));
}

std::int64_t beatTicks(const TempoMap& t) noexcept
{
    return std::max<std::int64_t>(1, kTicksPerWhole / std::max(1, t.denominator));
}

/// A step's triplet, where triplets are on and it's shorter than a bar.
std::int64_t tripletOf(std::int64_t step, const GridSettings& g, const TempoMap& t) noexcept
{
    return g.triplets && step < barTicks(t) && step % 3 == 0 ? step * 2 / 3 : step;
}

std::int64_t fixedTicks(GridSettings::Value v, const TempoMap& t) noexcept
{
    using V = GridSettings::Value;
    const std::int64_t bar = barTicks(t);
    switch (v)
    {
    case V::Bars8:
        return 8 * bar;
    case V::Bars4:
        return 4 * bar;
    case V::Bars2:
        return 2 * bar;
    case V::Bar:
        return bar;
    case V::Half:
        return kTicksPerWhole / 2;
    case V::Quarter:
        return kTicksPerWhole / 4;
    case V::Eighth:
        return kTicksPerWhole / 8;
    case V::Sixteenth:
        return kTicksPerWhole / 16;
    case V::ThirtySecond:
        return kTicksPerWhole / 32;
    }
    return bar;
}

/// The step in ticks. Adaptive: from the ladder 1/32, 1/16, 1/8 note (those shorter than a beat),
/// a beat, then 1, 2, 4 ... 256 bars, the first whose lines are the width's spacing apart; with
/// triplets, the steps below a bar as triplets. Fixed: its value; one longer than a bar that
/// isn't whole bars (a half note in 3/8) is a bar. Every step is below a bar or whole bars.
std::int64_t stepTicks(const TempoMap& t, double pixelsPerTick, const GridSettings& g) noexcept
{
    const std::int64_t bar = barTicks(t), beat = beatTicks(t);
    if (g.mode == GridSettings::Mode::Fixed)
    {
        const std::int64_t step = tripletOf(fixedTicks(g.value, t), g, t);
        return step > bar && step % bar != 0 ? bar : step;
    }
    const double spacing = g.spacing();
    for (const std::int64_t s : {kTicksPerWhole / 32, kTicksPerWhole / 16, kTicksPerWhole / 8, beat})
        if (s <= beat && s < bar && tripletOf(s, g, t) * pixelsPerTick >= spacing)
            return tripletOf(s, g, t);
    std::int64_t bars = 1;
    while (bars < 256 && bars * bar * pixelsPerTick < spacing)
        bars *= 2;
    return bars * bar;
}

/// The step drawn: the step, doubled (up to a bar, then in bars) while its lines would be closer
/// than minDrawSpacing. Its lines are always among the step's.
std::int64_t drawTicks(std::int64_t step, std::int64_t bar, double pixelsPerTick) noexcept
{
    while (step * pixelsPerTick < Timeline::minDrawSpacing && step < (bar << 20))
        step = step < bar && step * 2 > bar ? bar : step * 2;
    return step;
}

double pixelsPerTick(const Timeline& tl, const TempoMap& t) noexcept
{
    return tl.pixelsPerSecond() * t.secondsForBeats(1.0) / kTicksPerQuarter;
}

/// "1/16", "1/8T", "1 bar", "2 bars"; a step that is none of these, in beats.
std::string stepName(std::int64_t step, const TempoMap& t)
{
    const std::int64_t bar = barTicks(t);
    if (step >= bar && step % bar == 0)
        return step == bar ? "1 bar" : std::to_string(step / bar) + " bars";
    const auto plain = [](std::int64_t ticks) -> std::int64_t
    {
        // 1/n of a whole note, n a power of two: the n, else 0.
        if (ticks <= 0 || kTicksPerWhole % ticks != 0)
            return 0;
        const std::int64_t n = kTicksPerWhole / ticks;
        return (n & (n - 1)) == 0 ? n : 0;
    };
    if (const auto n = plain(step))
        return "1/" + std::to_string(n);
    if (step % 2 == 0)
        if (const auto n = plain(step * 3 / 2))
            return "1/" + std::to_string(n) + "T";
    const double beats = static_cast<double>(step) / kTicksPerQuarter;
    char text[32];
    std::snprintf(text, sizeof(text), "%g beats", beats);
    return text;
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
    return static_cast<double>(stepTicks(tempo, pixelsPerTick(*this, tempo), grid_)) / kTicksPerQuarter;
}

std::string Timeline::gridStepName(const TempoMap& tempo) const
{
    return stepName(stepTicks(tempo, pixelsPerTick(*this, tempo), grid_), tempo);
}

GridSettings::Value Timeline::fixedValueNearStep(const TempoMap& tempo) const noexcept
{
    std::int64_t step = stepTicks(tempo, pixelsPerTick(*this, tempo), grid_);
    if (grid_.triplets && step < barTicks(tempo) && step % 2 == 0)
        step = step * 3 / 2; // the value its triplet came from
    auto best = GridSettings::Value::Sixteenth;
    double bestError = 1e9;
    for (int i = 0; i < GridSettings::numValues; ++i)
    {
        const auto v = static_cast<GridSettings::Value>(i);
        const double error =
            std::abs(std::log(static_cast<double>(fixedTicks(v, tempo)) / static_cast<double>(step)));
        if (error < bestError - 1e-9)
        {
            bestError = error;
            best = v;
        }
    }
    return best;
}

void Timeline::gridLines(const TempoMap& tempo, std::vector<GridLine>& out) const
{
    out.clear();
    const double ppt = pixelsPerTick(*this, tempo);
    const std::int64_t bar = barTicks(tempo), beat = beatTicks(tempo);
    const std::int64_t step = drawTicks(stepTicks(tempo, ppt, grid_), bar, ppt);

    // Bar labels every 1, 2, 4 ... bars so they stay readable.
    std::int64_t labelBars = 1;
    while (labelBars * bar * ppt < minLabelSpacing && labelBars < (1 << 20))
        labelBars *= 2;

    const double secondsPerTick = tempo.secondsForBeats(1.0) / kTicksPerQuarter;
    const auto add = [&](std::int64_t tick)
    {
        GridLine line;
        line.seconds = static_cast<double>(tick) * secondsPerTick;
        if (tick % bar == 0)
        {
            line.kind = LineKind::Bar;
            line.bar = static_cast<int>(tick / bar) + 1;
            line.label = (tick / bar) % labelBars == 0;
        }
        else
            line.kind = (tick % bar) % beat == 0 ? LineKind::Beat : LineKind::Subdivision;
        out.push_back(line);
    };
    const auto lastTick = static_cast<std::int64_t>(std::ceil(end() / secondsPerTick));
    if (step % bar == 0)
    {
        // Whole bars, from the song's start.
        const auto first = std::max<std::int64_t>(
            0, static_cast<std::int64_t>(std::floor(start_ / secondsPerTick / static_cast<double>(step))));
        for (std::int64_t tick = first * step; tick <= lastTick + step; tick += step)
            add(tick);
        return;
    }
    // Below a bar: the lines start again at every bar.
    const auto firstBar = std::max<std::int64_t>(
        0, static_cast<std::int64_t>(std::floor(start_ / secondsPerTick / static_cast<double>(bar))));
    for (std::int64_t b = firstBar; b * bar <= lastTick; ++b)
        for (std::int64_t offset = 0; offset < bar; offset += step)
            add(b * bar + offset);
}

double Timeline::snap(double seconds, const TempoMap& tempo) const noexcept
{
    const double beat = gridRound(tempo.beatsForSeconds(seconds), gridStepBeats(tempo), tempo.beatsPerBar());
    return std::max(0.0, tempo.secondsForBeats(beat));
}

double Timeline::snapFloor(double seconds, const TempoMap& tempo) const noexcept
{
    const double beat = gridFloor(tempo.beatsForSeconds(seconds), gridStepBeats(tempo), tempo.beatsPerBar());
    return std::max(0.0, tempo.secondsForBeats(beat));
}
} // namespace fd::model
