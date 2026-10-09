#include "ui/AutomationLane.h"

namespace fd::app
{
model::LaneMapping LaneGeometry::mapping() const
{
    return {.x = [this](double t) { return static_cast<double>(x(t)); },
            .time = [this](double px) { return time(static_cast<float>(px)); },
            .y = [this](float v) { return static_cast<double>(y(v)); }};
}

namespace
{
/// The curve as a path over [x0, x1], from the same function the engine plays: straight
/// segments exactly, steps as steps, bends sampled every two pixels.
juce::Path curvePath(const LaneGeometry& geo, const model::AutomationPoints& points, float x0, float x1)
{
    juce::Path path;
    const double t0 = geo.time(x0), t1 = geo.time(x1);
    path.startNewSubPath(x0, geo.y(model::automationValueAt(points, t0)));
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        if (p.time > t1)
            break;
        const bool hasNext = i + 1 < points.size();
        if (hasNext && points[i + 1].time < t0)
            continue; // the whole segment is left of the view
        if (p.time >= t0)
            path.lineTo(geo.x(p.time), geo.y(p.value));
        if (!hasNext)
            break;
        const auto& next = points[i + 1];
        if (p.curve > 0.5f) // a step: hold, then jump at the next point
        {
            path.lineTo(geo.x(std::min(next.time, t1)), geo.y(p.value));
            continue;
        }
        if (p.curve != 0.0f) // a bend
        {
            const float sx0 = std::max(geo.x(p.time), x0), sx1 = std::min(geo.x(next.time), x1);
            for (float sx = sx0 + 2.0f; sx < sx1; sx += 2.0f)
                path.lineTo(sx, geo.y(model::automationValueAt(points, geo.time(sx))));
        }
    }
    path.lineTo(x1, geo.y(model::automationValueAt(points, t1)));
    return path;
}
} // namespace

void paintAutomationLane(juce::Graphics& g, const LaneGeometry& geo, const LanePaint& lane, juce::Rectangle<int> visible)
{
    const auto& t = ui::Theme::current();
    const auto area = geo.area;
    const float x0 = std::max(area.getX(), static_cast<float>(visible.getX()) - 2.0f);
    const float x1 = std::min(area.getRight(), static_cast<float>(visible.getRight()) + 2.0f);
    if (x1 <= x0)
        return;
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.getSmallestIntegerContainer());

    const auto& points = *lane.points;
    if (points.empty())
    {
        if (lane.current)
        {
            const float y = geo.y(*lane.current);
            g.setColour(t.roles.automationOff);
            const float dashes[] = {4.0f, 4.0f};
            g.drawDashedLine(juce::Line<float>(x0, y, x1, y), dashes, 2, 1.0f);
        }
        if (area.getHeight() >= 24.0f)
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(lane.assigned ? "Double-click to add a point"
                                     : juce::String::fromUTF8("Drives nothing yet: Assign parameter\xE2\x80\xA6 in the inspector, or "
                                                              "right-click a control \xE2\x86\x92 Assign to automation lane"),
                       area.reduced(8.0f, 0.0f).withX(std::max(area.getX(), x0) + 8.0f), juce::Justification::centredLeft,
                       false);
        }
        return;
    }

    auto line = curvePath(geo, points, x0, x1);
    // The reserved gradient under the curve, down to the lane's floor (ui-principles §4).
    auto fill = line;
    fill.lineTo(x1, area.getBottom());
    fill.lineTo(x0, area.getBottom());
    fill.closeSubPath();
    if (lane.on)
        g.setGradientFill(juce::ColourGradient(t.roles.automationFillTop, 0.0f, area.getY(), t.roles.automationFillBottom,
                                               0.0f, area.getBottom(), false));
    else
        g.setColour(t.roles.automationOffFill);
    g.fillPath(fill);
    g.setColour(lane.on ? t.roles.automation : t.roles.automationOff);
    g.strokePath(line, juce::PathStrokeType(1.5f));

    // Points: dots, larger under the mouse, ringed when selected.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const float px = geo.x(points[i].time);
        if (px < x0 - 6.0f || px > x1 + 6.0f)
            continue;
        const float d = lane.hovered == i ? 8.0f : 6.0f;
        const auto dot = juce::Rectangle<float>(d, d).withCentre({px, geo.y(points[i].value)});
        g.setColour(lane.on ? t.roles.automation : t.roles.automationOff);
        g.fillEllipse(dot);
        if (lane.selected.contains(i))
        {
            g.setColour(t.textPrimary);
            g.drawEllipse(dot.expanded(1.5f), 1.0f);
        }
    }
}
} // namespace fd::app
