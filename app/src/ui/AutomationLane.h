#pragma once

#include "model/Automation.h"
#include "model/Timeline.h"

#include <fd_ui/fd_ui.h>

#include <optional>
#include <set>

namespace fd::app
{
/// Drawing an automation lane (docs/design/automation.md §4): the curve the engine plays, the
/// reserved gradient under it, the points. The lane's height is the control's travel.
struct LaneGeometry
{
    juce::Rectangle<float> area; // the lane, in view pixels
    float laneX0 = 0.0f;         // the x of the timeline's 0 (the lanes' left edge)
    const model::Timeline* timeline = nullptr;

    /// Points sit this far inside the lane's top and bottom, so values at the ends stay visible.
    static constexpr float inset = 5.0f;

    [[nodiscard]] float x(double seconds) const { return laneX0 + static_cast<float>(timeline->xForTime(seconds)); }
    [[nodiscard]] double time(float px) const { return timeline->timeForX(px - laneX0); }
    [[nodiscard]] float y(float value) const
    {
        return area.getBottom() - inset - std::clamp(value, 0.0f, 1.0f) * (area.getHeight() - 2.0f * inset);
    }
    [[nodiscard]] float value(float py) const
    {
        const float h = std::max(1.0f, area.getHeight() - 2.0f * inset);
        return std::clamp((area.getBottom() - inset - py) / h, 0.0f, 1.0f);
    }
    [[nodiscard]] model::LaneMapping mapping() const;
};

struct LanePaint
{
    const model::AutomationPoints* points = nullptr;
    std::set<size_t> selected;
    std::optional<size_t> hovered;
    bool on = true;
    juce::Colour colour; // the lane's own (its row colour)
    /// With no points: the parameter's value now, drawn dashed.
    std::optional<float> current;
    /// False while the lane drives nothing: an empty one says how to give it a parameter.
    bool assigned = true;
    /// An empty lane's hint shows only while the lane is selected (it would crowd the arrangement).
    bool hint = false;
};

void paintAutomationLane(juce::Graphics& g, const LaneGeometry& geometry, const LanePaint& lane,
                         juce::Rectangle<int> visible);
} // namespace fd::app
