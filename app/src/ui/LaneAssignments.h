#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

#include <memory>
#include <optional>

namespace fd::app
{
/// The inspector's ASSIGNMENTS section for an automation lane (docs/design/automation.md §9,
/// selection.md §5): per assignment its on switch, "Track › Parameter" (a click shows its module),
/// a remove button, and its range as from and to fields in the parameter's units (to below from
/// inverts it); then "Assign parameter…". Rebuilt only when the assignments change.
class LaneAssignments final : public juce::Component
{
public:
    explicit LaneAssignments(engine::Session& session);

    /// The lane to show (nullopt: none).
    void setLane(const std::optional<model::AutomationLaneInfo>& lane);
    [[nodiscard]] int preferredHeight() const;

    /// "Assign parameter…": the picker, for this lane.
    std::function<void(std::uint64_t lane)> onAssign;
    /// A click on an assignment's name: show its module (0: a mixer parameter, its channel).
    std::function<void(std::uint64_t channel, std::uint64_t module)> onShowParameter;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    struct Row
    {
        engine::Session::ParamTarget target;
        std::uint64_t module = 0;
        juce::String label;
        ui::Toggle on;
        ui::IconButton remove{ui::Icon::Close, "Remove"};
        ui::NumberField from, to;
        std::optional<engine::Session::EditGroup> gesture;
        juce::Rectangle<int> labelArea;
    };
    void build(const model::AutomationLaneInfo& lane);
    void setupRange(Row& row, ui::NumberField& field, bool isFrom);

    engine::Session& session_;
    std::optional<model::AutomationLaneInfo> lane_;
    std::vector<std::unique_ptr<Row>> rows_;
    ui::Toggle assign_{juce::String::fromUTF8("Assign parameter\xE2\x80\xA6")};
    juce::Rectangle<int> hintArea_;
};
} // namespace fd::app
