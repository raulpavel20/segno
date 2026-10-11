#pragma once

namespace fd::ui
{
/// A view setting with a value (docs/design/ui-foundations.md, "Shared components"; ADR 0047): the
/// value in a view option's label style and padding, then a small chevron, without a button's
/// fill. A click calls onClick, where its owner opens the setting's menu (showMenu). Like a
/// ViewOption it changes only how a view looks or edits: the grid's step beside Snap.
class ViewChoice final : public juce::Button
{
public:
    ViewChoice();

    /// The value shown ("1/16"); repaints when it changes.
    void setValue(const juce::String& value);
    [[nodiscard]] const juce::String& value() const noexcept { return value_; }

    /// The width that fits its value and the chevron: its owner lays it out again when the value
    /// changes, so it sits as close to its neighbour as a view option does.
    [[nodiscard]] int preferredWidth() const;

    /// Opens `menu` under the control.
    void showMenu(juce::PopupMenu menu);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    static constexpr int kPadding = 6; // inside the hover wash, left and right (a ViewOption's)
    static constexpr int kGap = 4;     // between the value and the chevron
    static constexpr float kChevron = 8.0f;

    juce::String value_;
};
} // namespace fd::ui
