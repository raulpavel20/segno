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

/// A lane's gestures (docs/design/automation.md §5), shared by the arrangement's automation lanes
/// and the clip view's controller lanes (piano-roll.md §11), so both behave alike: a press decides
/// what the gesture is from what's under the pointer and the keys held, a drag previews it, a
/// release keeps it. Double-click adds a point (Alt: free of the grid), or deletes one (Alt:
/// straightens the segment after it); a drag moves points (Shift: fine, Alt: free) or a segment;
/// Alt+drag bends a segment; Ctrl+drag draws; a drag from an empty spot box-selects (Shift adds).
class LaneGesture
{
public:
    /// The lane a gesture works on.
    struct Lane
    {
        const model::AutomationPoints* points = nullptr;
        model::LaneRules rules;
        LaneGeometry geometry;
        std::set<size_t> selection; // its selected points now
        /// A time on the grid (`free`: Alt, as it is).
        std::function<double(double seconds, bool free)> snap;
        /// What drawing is called as an undo step.
        const char* drawName = "Draw automation";
    };

    /// What the lane is asked to do.
    struct Outcome
    {
        std::optional<model::PointEdit> keep; // an edit to keep, one undo step named `undoName`
        std::string undoName;
        std::optional<model::PointEdit> preview; // to show until the gesture ends
        std::optional<std::set<size_t>> select;  // the lane's selection now
        bool steps = false;                      // Alt on a stepped lane: nothing bends, say why
    };

    /// What's under the pointer: a point, or a segment (to move or bend).
    struct Hover
    {
        std::optional<size_t> point;
        bool segment = false;
    };
    [[nodiscard]] static Hover hover(const Lane& lane, juce::Point<float> at);

    /// A press; `additive` (Shift or Ctrl) adds to the selection.
    Outcome press(const juce::MouseEvent& e, const Lane& lane);
    /// A drag from `start` (past the drag threshold).
    Outcome drag(const juce::MouseEvent& e, juce::Point<float> start, const Lane& lane);
    /// The release: what to keep, or the click's selection.
    Outcome release(bool moved, const Lane& lane);
    [[nodiscard]] bool active() const noexcept { return kind_ != Kind::None; }
    /// A box selection is being dragged (its box is drawn).
    [[nodiscard]] bool boxing() const noexcept { return kind_ == Kind::Box; }

private:
    enum class Kind
    {
        None,
        Points,
        Segment,
        Bend,
        Box,
        Draw
    };
    Kind kind_ = Kind::None;
    model::AutomationPoints base_;
    std::set<size_t> selection_, boxBase_;
    size_t index_ = 0; // the grabbed point, or the segment's first point
    float bend_ = 0.0f;
    model::AutomationPoints drawn_;
    std::optional<size_t> clickedInSelection_;
    std::optional<model::PointEdit> preview_;
};
} // namespace fd::app
