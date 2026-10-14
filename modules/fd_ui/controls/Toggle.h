#pragma once

namespace fd::ui
{
/// Flat toggle: dim surface when off, solid `active` fill when on (ui-principles §4, ADR 0014).
class Toggle : public juce::Button
{
public:
    explicit Toggle(const juce::String& text = {});
    /// The width a text toggle needs for `text`: its label and the button padding each side, and
    /// with `icon`, the icon and a gap before the label.
    [[nodiscard]] static int widthFor(const juce::String& text, bool icon = false);
    /// An icon before the label (the clip view's Transform and Generate, piano-roll.md §25).
    void setIcon(std::optional<Icon> icon);

    /// Shows whether its parameter is automated (automation.md §6).
    void setAutomationState(AutomationState state);
    /// An arm button (recording.md §4): a round indicator in the `record` colour, filled when on
    /// and a ring when only partly on (armed by the selection).
    void setArmStyle(bool arm);
    void setPartlyOn(bool partly);
    [[nodiscard]] AutomationState automationState() const noexcept { return automation_; }
    /// A right-click: the control's menu (it doesn't toggle).
    std::function<void()> onPopupMenu;
    /// A double-click (a name of your own: rename it).
    std::function<void()> onDoubleClick;
    /// Its text is a name of your own (a rack's snapshot, racks.md §18.2): cut to fit, as the
    /// layout can't know how long it is.
    void setCutsText(bool cuts);
    /// What it names has changed since it was kept: the modified mark after the text, never cut
    /// (as a preset's name in its field, ADR 0044). Cuts the text to leave room.
    void setModified(bool modified);
    [[nodiscard]] bool isModified() const noexcept { return modified_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    AutomationState automation_ = AutomationState::None;
    std::optional<Icon> icon_;
    bool popup_ = false, arm_ = false, partly_ = false, cuts_ = false, modified_ = false;
};
} // namespace fd::ui
