#pragma once

namespace fd::ui
{
/// Flat toggle: dim surface when off, solid `active` fill when on (ui-principles §4, ADR 0014).
class Toggle : public juce::Button
{
public:
    explicit Toggle(const juce::String& text = {});

    /// Shows whether its parameter is automated (automation.md §6).
    void setAutomationState(AutomationState state);
    [[nodiscard]] AutomationState automationState() const noexcept { return automation_; }
    /// A right-click: the control's menu (it doesn't toggle).
    std::function<void()> onPopupMenu;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    AutomationState automation_ = AutomationState::None;
    bool popup_ = false;
};
} // namespace fd::ui
