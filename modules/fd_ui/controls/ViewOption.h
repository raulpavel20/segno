#pragma once

namespace fd::ui
{
/// A view option in a panel header (docs/design/ui-foundations.md, "Shared components"): a small
/// indicator and a label, without a button's fill. It changes only how a view looks (view state);
/// on or off things in the project are Toggles. Off: a ring and tertiary text; on: a filled
/// `active` disc and primary text, so the shape tells them apart, not just the colour.
class ViewOption final : public juce::Button
{
public:
    explicit ViewOption(const juce::String& text);

    /// The width that fits its indicator and label.
    [[nodiscard]] int preferredWidth() const;

    /// A right-click opens this instead of switching it (a Toggle's): what the option shows, one by
    /// one (the clip view's Others: which tracks).
    std::function<void()> onPopupMenu;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    bool popup_ = false;
    static constexpr float kDot = 6.0f;  // the indicator's diameter
    static constexpr int kPadding = 6;   // inside the hover wash, left and right
    static constexpr int kGap = 6;       // between the indicator and the label
};
} // namespace fd::ui
