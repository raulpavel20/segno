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
        const auto hintFont = ui::font(ui::TextStyle::Caption);
        const bool hint =
            lane.hint &&
            area.getHeight() >= hintFont.getHeight() + static_cast<float>(2 * t.shell.itemGap); // room for it
        const auto hintText =
            lane.assigned ? juce::String("Double-click to add a point")
                          : juce::String::fromUTF8(
                                "Drives nothing yet: Assign parameter\xE2\x80\xA6 in the inspector, or "
                                "right-click a control \xE2\x86\x92 Assign to automation lane");
        const auto hintArea =
            area.withLeft(std::max(area.getX(), x0) + static_cast<float>(t.shell.laneTextInset))
                .withTrimmedRight(static_cast<float>(t.shell.laneTextInset));
        if (lane.current)
        {
            // Dashed at the value now; it gives way to the hint where they'd cross (pitch bend rests in
            // the middle), so the two never touch.
            const float y = geo.y(*lane.current);
            g.setColour(t.automationLine(lane.colour, false));
            const float dashes[] = {4.0f, 4.0f};
            const float gap = static_cast<float>(t.shell.itemGap);
            const float half = hintFont.getHeight() * 0.5f + gap;
            if (hint && std::abs(y - hintArea.getCentreY()) < half)
            {
                const float textEnd =
                    hintArea.getX() + juce::GlyphArrangement::getStringWidth(hintFont, hintText) + gap;
                g.drawDashedLine(juce::Line<float>(x0, y, hintArea.getX() - gap, y), dashes, 2, 1.0f);
                g.drawDashedLine(juce::Line<float>(textEnd, y, x1, y), dashes, 2, 1.0f);
            }
            else
                g.drawDashedLine(juce::Line<float>(x0, y, x1, y), dashes, 2, 1.0f);
        }
        if (hint)
        {
            g.setColour(t.textTertiary);
            g.setFont(hintFont);
            g.drawText(hintText, hintArea, juce::Justification::centredLeft, false);
        }
        return;
    }

    auto line = curvePath(geo, points, x0, x1);
    // The reserved gradient under the curve, down to the lane's floor (ui-principles §4).
    auto fill = line;
    fill.lineTo(x1, area.getBottom());
    fill.lineTo(x0, area.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(t.automationFillTop(lane.colour, lane.on), 0.0f, area.getY(),
                                           t.automationFillBottom(lane.colour, lane.on), 0.0f, area.getBottom(), false));
    g.fillPath(fill);
    g.setColour(t.automationLine(lane.colour, lane.on));
    g.strokePath(line, juce::PathStrokeType(1.5f));

    // Points: dots, larger under the mouse, ringed when selected.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const float px = geo.x(points[i].time);
        if (px < x0 - 6.0f || px > x1 + 6.0f)
            continue;
        const float d = lane.hovered == i ? 8.0f : 6.0f;
        const auto dot = juce::Rectangle<float>(d, d).withCentre({px, geo.y(points[i].value)});
        g.setColour(t.automationLine(lane.colour, lane.on));
        g.fillEllipse(dot);
        if (lane.selected.contains(i))
        {
            g.setColour(t.textPrimary);
            g.drawEllipse(dot.expanded(1.5f), 1.0f);
        }
    }
}

LaneGesture::Hover LaneGesture::hover(const Lane& lane, juce::Point<float> at)
{
    const auto map = lane.geometry.mapping();
    if (const auto p = model::hitPoint(*lane.points, map, at.x, at.y, ui::Theme::current().input.edgeGrab))
        return {.point = p};
    return {.segment = model::hitSegment(*lane.points, map, at.x, at.y, 4.0).has_value()};
}

LaneGesture::Outcome LaneGesture::press(const juce::MouseEvent& e, const Lane& lane)
{
    *this = {};
    const auto& geo = lane.geometry;
    const auto map = geo.mapping();
    const auto& rules = lane.rules;
    const auto& points = *lane.points;
    const auto hitPoint =
        model::hitPoint(points, map, e.position.x, e.position.y, ui::Theme::current().input.edgeGrab);
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();
    base_ = points;
    Outcome out;

    if (e.getNumberOfClicks() >= 2)
    {
        if (hitPoint && e.mods.isAltDown() && rules.stepped())
            out.steps = true;
        else if (hitPoint && e.mods.isAltDown()) // Alt + double-click straightens the segment after it
        {
            out.keep = model::bendSegment(points, *hitPoint, 0.0f, rules);
            out.undoName = "Bend";
        }
        else if (hitPoint)
        {
            out.keep = model::deletePoints(points, {*hitPoint});
            out.undoName = "Delete point";
        }
        else
        {
            out.keep = model::addPoint(points, lane.snap(geo.time(e.position.x), e.mods.isAltDown()),
                                       geo.value(e.position.y), rules);
            out.undoName = "Add point";
        }
        if (out.keep)
            out.select = out.keep->selection;
        return out;
    }
    if (hitPoint)
    {
        auto selection = lane.selection;
        if (!additive && selection.size() > 1 && selection.contains(*hitPoint))
            clickedInSelection_ = *hitPoint; // stays a group for a drag; just this point for a click
        else if (additive && selection.contains(*hitPoint))
            selection.erase(*hitPoint);
        else if (additive)
            selection.insert(*hitPoint);
        else
            selection = {*hitPoint};
        out.select = selection;
        kind_ = selection.contains(*hitPoint) ? Kind::Points : Kind::None;
        selection_ = selection;
        index_ = *hitPoint;
        return out;
    }
    if (e.mods.isCommandDown()) // Ctrl + drag draws
    {
        kind_ = Kind::Draw;
        drawn_ = {{lane.snap(geo.time(e.position.x), e.mods.isAltDown()), geo.value(e.position.y), 0.0f}};
        out.select = std::set<size_t>{};
        return out;
    }
    if (const auto segment = model::hitSegment(points, map, e.position.x, e.position.y, 4.0))
    {
        if (e.mods.isAltDown() && rules.stepped())
        {
            out.steps = true; // nothing to bend: say why, rather than do nothing
            return out;
        }
        kind_ = e.mods.isAltDown() ? Kind::Bend : Kind::Segment;
        index_ = *segment;
        bend_ = model::bendOf(points[*segment]);
        out.select = std::set<size_t>{*segment, *segment + 1};
        return out;
    }
    // An empty spot: box-select points (Shift adds).
    kind_ = Kind::Box;
    boxBase_ = additive ? lane.selection : std::set<size_t>{};
    out.select = boxBase_;
    return out;
}

LaneGesture::Outcome LaneGesture::drag(const juce::MouseEvent& e, juce::Point<float> start, const Lane& lane)
{
    const auto& geo = lane.geometry;
    const auto& rules = lane.rules;
    const float travel = std::max(1.0f, geo.area.getHeight() - 2.0f * LaneGeometry::inset);
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    const float dv = (start.y - e.position.y) / travel * fine;
    Outcome out;
    switch (kind_)
    {
    case Kind::Points:
    {
        // The grabbed point snaps in time; the others follow by the same amount.
        const auto& anchor = base_[index_];
        const double moved = geo.time(e.position.x) - geo.time(start.x);
        const double dt = lane.snap(anchor.time + moved, e.mods.isAltDown()) - anchor.time;
        preview_ = model::movePoints(base_, selection_, dt, dv, rules);
        break;
    }
    case Kind::Segment:
        preview_ = model::moveSegment(base_, index_, dv, rules);
        break;
    case Kind::Bend:
    {
        // Up bows the segment up: early for a rising segment, late for a falling one.
        const auto& a = base_[index_];
        const auto& b = base_[index_ + 1];
        const float up = (start.y - e.position.y) / ui::Theme::current().input.bendTravel * fine;
        const float bend = std::clamp(bend_ + (b.value >= a.value ? -up : up), -1.0f, 1.0f);
        preview_ = model::bendSegment(base_, index_, bend, rules);
        break;
    }
    case Kind::Draw:
    {
        // One point per grid step passed (or where the mouse is, with snap off).
        const double at = lane.snap(geo.time(e.position.x), e.mods.isAltDown());
        const auto it = std::find_if(drawn_.begin(), drawn_.end(),
                                     [at](const auto& p) { return std::abs(p.time - at) < 1e-9; });
        if (it != drawn_.end())
            it->value = geo.value(e.position.y);
        else
            drawn_.push_back({at, geo.value(e.position.y), 0.0f});
        preview_ = model::drawPoints(base_, drawn_, rules);
        break;
    }
    case Kind::Box:
    {
        auto inside = model::pointsInBox(base_, geo.time(start.x), geo.time(e.position.x), geo.value(start.y),
                                         geo.value(e.position.y));
        inside.insert(boxBase_.begin(), boxBase_.end());
        out.select = inside;
        return out;
    }
    case Kind::None:
        return out;
    }
    out.preview = preview_;
    return out;
}

LaneGesture::Outcome LaneGesture::release(bool moved, const Lane& lane)
{
    Outcome out;
    if (moved && preview_)
    {
        out.keep = preview_;
        out.undoName = kind_ == Kind::Bend ? "Bend" : kind_ == Kind::Draw ? lane.drawName : "Move points";
    }
    else if (!moved && clickedInSelection_)
        out.select = std::set<size_t>{*clickedInSelection_};
    else if (!moved && kind_ == Kind::Draw)
    {
        out.keep = model::drawPoints(base_, drawn_, lane.rules);
        out.undoName = lane.drawName;
    }
    *this = {};
    return out;
}
} // namespace fd::app
