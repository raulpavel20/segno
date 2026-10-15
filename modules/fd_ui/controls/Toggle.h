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
    /// Moved by modulators (modulators.md §7.4): a line along its bottom edge in the modulation
    /// gradient, as a bar's; full while the modulation has it the other way from where it's set
    /// (`turned`), faint while it doesn't; dimmed while another modulator is shown. In segmented
    /// buttons (§7.3), the choice the modulation has it on, another than the one set, is outlined
    /// in the gradient too (`now`).
    struct Modulation
    {
        bool turned = false, dimmed = false, now = false;
        bool operator==(const Modulation&) const = default;
    };
    void setModulation(std::optional<Modulation> modulation);
    [[nodiscard]] const std::optional<Modulation>& modulation() const noexcept { return modulation_; }
    /// While a modulator is dragged: the line in `active` where it would land, faint where it could.
    void setDropTarget(bool target);
    void setDropCandidate(bool candidate);
    [[nodiscard]] bool isDropTarget() const noexcept { return dropTarget_; }
    [[nodiscard]] bool isDropCandidate() const noexcept { return dropCandidate_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    AutomationState automation_ = AutomationState::None;
    std::optional<Icon> icon_;
    std::optional<Modulation> modulation_;
    bool popup_ = false, arm_ = false, partly_ = false, cuts_ = false, modified_ = false;
    bool dropTarget_ = false, dropCandidate_ = false;
};

/// What modulators do to a switch or a choice, drawn over it (modulators.md §7.4): the line along
/// the bottom edge of `bounds`, inside its rounded shape, in `active` where a dragged handle would
/// land (faint where it could), else in the modulation gradient as `modulation` says; and the
/// outline of the choice it's on now. On a switch that's on (`onFill`), the line has a dark halo
/// above it, as the lines on hover do, so it reads on the fill. A switch, segmented buttons and a
/// dropdown share it.
void paintModulationEdge(juce::Graphics& g, juce::Rectangle<float> bounds,
                         const std::optional<Toggle::Modulation>& modulation, bool dropTarget,
                         bool dropCandidate, bool onFill = false);
} // namespace fd::ui
