#pragma once

namespace fd::ui
{
/// Flat toggle: dim surface when off, solid `active` fill when on (ui-principles §4, ADR 0014).
class Toggle : public juce::Button
{
public:
    explicit Toggle(const juce::String& text = {});
    /// The width a text toggle needs for `text`: its label and the button padding each side.
    [[nodiscard]] static int widthFor(const juce::String& text);

    /// Shows whether its parameter is automated (automation.md §6).
    void setAutomationState(AutomationState state);
    /// An arm button (recording.md §4): a round indicator in the `record` colour, filled when on
    /// and a ring when only partly on (armed by the selection).
    void setArmStyle(bool arm);
    void setPartlyOn(bool partly);
    [[nodiscard]] AutomationState automationState() const noexcept { return automation_; }
    /// A right-click: the control's menu (it doesn't toggle).
    std::function<void()> onPopupMenu;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    AutomationState automation_ = AutomationState::None;
    bool popup_ = false, arm_ = false, partly_ = false;
};
} // namespace fd::ui
