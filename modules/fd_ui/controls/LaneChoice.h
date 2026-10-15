#pragma once

namespace fd::ui
{
/// A lane's head (docs/design/ui-foundations.md, "Shared components"): in the narrow column beside
/// a lane, what the lane shows, as a caps label with a chevron under it; a click opens the lane's
/// menu. The clip view's bottom lane (piano-roll.md §11): velocity, chance, a controller. Its
/// label is short ("VELOCITY", "BEND"); the menu and the tooltip give the whole name.
class LaneChoice final : public juce::Button
{
public:
    LaneChoice();

    /// The label shown, in caps; repaints when it changes.
    void setValue(const juce::String& label);
    [[nodiscard]] const juce::String& value() const noexcept { return value_; }
    /// A lock beside its chevron: what the lane shows is held as played (a clip's chance kept,
    /// piano-roll.md §26).
    void setLocked(bool locked);
    [[nodiscard]] bool isLocked() const noexcept { return locked_; }
    /// Whether `label` fits a head `width` wide.
    [[nodiscard]] static bool fits(const juce::String& label, int width);

    /// Opens `menu` beside the control.
    void showMenu(juce::PopupMenu menu);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    static constexpr float kChevron = 8.0f; // a ViewChoice's
    juce::String value_;
    bool locked_ = false;
};
} // namespace fd::ui
