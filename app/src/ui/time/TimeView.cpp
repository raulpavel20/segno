#include "ui/time/TimeView.h"

namespace fd::app::time
{
namespace
{
float xOf(const model::Timeline& timeline, juce::Rectangle<int> area, double seconds)
{
    return static_cast<float>(area.getX() + timeline.xForTime(seconds));
}
} // namespace

void paintGrid(juce::Graphics& g, const std::vector<model::Timeline::GridLine>& lines, const model::Timeline& timeline,
               juce::Rectangle<int> area, bool subdivisions)
{
    const auto& roles = ui::Theme::current().roles;
    for (const auto& line : lines)
    {
        switch (line.kind)
        {
        case model::Timeline::LineKind::Bar: g.setColour(roles.gridBar); break;
        case model::Timeline::LineKind::Beat: g.setColour(roles.gridBeat); break;
        case model::Timeline::LineKind::Subdivision:
            if (!subdivisions)
                continue;
            g.setColour(roles.gridSub);
            break;
        }
        g.fillRect(xOf(timeline, area, line.seconds), static_cast<float>(area.getY()), 1.0f, static_cast<float>(area.getHeight()));
    }
}

void paintRuler(juce::Graphics& g, const std::vector<model::Timeline::GridLine>& lines, const model::Timeline& timeline,
                juce::Rectangle<int> area, const RulerOptions& options)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.surface1);
    g.fillRect(area);
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);

    // The range strip along the bottom; ticks stand on it.
    constexpr float rangeHeight = 3.0f;
    float base = static_cast<float>(area.getBottom());
    if (options.range)
    {
        const float x0 = xOf(timeline, area, options.range->first), x1 = xOf(timeline, area, options.range->second);
        g.setColour(options.rangeColour);
        g.fillRect(x0, base - rangeHeight, x1 - x0, rangeHeight);
        base -= rangeHeight;
    }

    g.setFont(ui::font(ui::TextStyle::Value));
    for (const auto& line : lines)
    {
        const float x = xOf(timeline, area, line.seconds);
        const float tick = line.kind == model::Timeline::LineKind::Bar    ? 9.0f
                           : line.kind == model::Timeline::LineKind::Beat ? 5.0f
                                                                           : 3.0f;
        g.setColour(line.kind == model::Timeline::LineKind::Bar ? t.textTertiary : t.roles.gridBar);
        g.fillRect(x, base - tick, 1.0f, tick);
        if (line.label)
        {
            g.setColour(t.textSecondary);
            g.drawText(juce::String(line.bar),
                       juce::Rectangle<float>(x + 4.0f, static_cast<float>(area.getY()) + 1.0f, 48.0f, 14.0f),
                       juce::Justification::centredLeft, false);
        }
    }

    if (options.playhead)
    {
        const float px = xOf(timeline, area, *options.playhead);
        const auto bottom = static_cast<float>(area.getBottom());
        juce::Path marker;
        marker.addTriangle(px - 5.0f, bottom - 8.0f, px + 5.0f, bottom - 8.0f, px, bottom);
        g.setColour(t.roles.playhead);
        g.fillPath(marker);
    }
}

void paintLoopStrip(juce::Graphics& g, const model::Timeline& timeline, juce::Rectangle<int> area,
                    std::pair<double, double> range, bool looping)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.roles.strip);
    g.fillRect(area);
    const auto [start, end] = range;
    if (!(end > start))
        return;
    const auto brace = juce::Rectangle<float>(xOf(timeline, area, start), static_cast<float>(area.getY()) + 2.0f,
                                              xOf(timeline, area, end) - xOf(timeline, area, start),
                                              static_cast<float>(area.getHeight()) - 4.0f);
    g.setColour(looping ? t.active : t.roles.loopInactive);
    g.fillRect(brace);
}

void paintPlayhead(juce::Graphics& g, const model::Timeline& timeline, juce::Rectangle<int> area, double seconds)
{
    g.setColour(ui::Theme::current().roles.playhead);
    g.fillRect(xOf(timeline, area, seconds), static_cast<float>(area.getY()), 1.0f, static_cast<float>(area.getHeight()));
}

void paintBoxSelection(juce::Graphics& g, juce::Rectangle<float> box)
{
    const auto& roles = ui::Theme::current().roles;
    g.setColour(roles.selectionFill);
    g.fillRect(box);
    g.setColour(roles.selectionStroke);
    g.drawRect(box, 1.0f);
}

void PlayheadTracker::update(juce::Component& component, const model::Timeline& timeline, juce::Rectangle<int> lanes,
                             double seconds)
{
    const float x = xOf(timeline, lanes, seconds);
    if (std::abs(x - drawnX_) < 0.5f)
        return;
    for (float px : {drawnX_, x})
        if (px >= static_cast<float>(lanes.getX()) - 8.0f)
            component.repaint(juce::Rectangle<int>(static_cast<int>(px) - 6, 0, 13, component.getHeight()));
    drawnX_ = x;
}

void RulerGesture::drag(model::Timeline& timeline, juce::Point<float> delta, float laneX)
{
    const auto& input = ui::Theme::current().input;
    if (delta.y != 0.0f)
        timeline.zoomAround(anchorX_, std::exp(static_cast<double>(delta.y) * input.rulerZoom));
    if (delta.x != 0.0f)
    {
        timeline.scrollBy(-static_cast<double>(delta.x));
        anchorX_ = laneX;
    }
}

void applyWheel(model::Timeline& timeline, double laneX, const juce::ModifierKeys& mods,
                const juce::MouseWheelDetails& wheel, const std::function<void(double pixels)>& otherAxis)
{
    const auto& input = ui::Theme::current().input;
    const double dy = wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);
    const double dx = wheel.deltaX * (wheel.isReversed ? -1.0 : 1.0);
    if (mods.isCommandDown())
        timeline.zoomAround(std::max(0.0, laneX), std::pow(input.wheelZoom, dy * 2.0));
    else if (mods.isShiftDown())
        timeline.scrollBy(-dy * input.wheelScroll);
    else
    {
        if (dx != 0.0)
            timeline.scrollBy(-dx * input.wheelScroll);
        if (dy != 0.0 && otherAxis)
            otherAxis(-dy * input.wheelScroll);
    }
}

void applyMagnify(model::Timeline& timeline, double laneX, float scaleFactor)
{
    timeline.zoomAround(std::max(0.0, laneX), scaleFactor);
}

double rulerTime(const model::Timeline& timeline, const model::TempoMap& tempo, double laneX, bool free)
{
    const double seconds = timeline.timeForX(laneX);
    return free ? std::max(0.0, seconds) : timeline.snap(seconds, tempo);
}
} // namespace fd::app::time
