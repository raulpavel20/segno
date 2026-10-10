#pragma once

namespace fd::ui
{
/// Minimise, maximise (restore while maximised) and close at the right end of our own title bar
/// (docs/design/menu-bar.md §4, ADR 0040): each `shell.windowButtonWidth` wide and as tall as the
/// bar, a glyph in `textSecondary`; hovered, `textPrimary` on the hover wash, and Close on `record`
/// (red), as on Windows. Where each one is answers the system's hit-testing (Windows' snap layouts
/// over Maximise).
class WindowButtons final : public juce::Component, public juce::TooltipClient
{
public:
    enum class Part
    {
        None,
        Minimise,
        Maximise,
        Close,
    };

    WindowButtons();

    void setMaximised(bool maximised);
    [[nodiscard]] bool isMaximised() const noexcept { return maximised_; }
    /// The width of the three.
    [[nodiscard]] static int widthFor();
    /// Each button's area, and the button at `p` (local coordinates; plain rectangle tests, safe
    /// from the system's hit-testing).
    [[nodiscard]] juce::Rectangle<int> area(Part part) const;
    [[nodiscard]] Part partAt(juce::Point<int> p) const;

    std::function<void(Part)> onClick;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

private:
    void setHovered(Part part);
    bool maximised_ = false;
    Part hovered_ = Part::None, pressed_ = Part::None;
};
} // namespace fd::ui
