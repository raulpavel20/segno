#pragma once

namespace fd::ui
{
/// A song setting chosen rather than dragged: the song's key in the top bar (key-and-scale.md §6,
/// ADR 0056). It looks like the NumberField beside it (its value in the mono at the field's size,
/// the hover fill) and dims a placeholder ("No key"). A click, Return or Space calls onOpen, where
/// its owner opens the chooser; a right-click calls onPopupMenu. No wheel or drag: a choice is
/// never scrolled through by accident.
class ChoiceField final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceField();

    /// The value shown; a placeholder is dimmed (nothing is chosen).
    void setValue(const juce::String& value, bool placeholder = false);
    [[nodiscard]] const juce::String& value() const noexcept { return value_; }
    [[nodiscard]] bool isPlaceholder() const noexcept { return placeholder_; }
    void setFontSize(float size) noexcept { fontSize_ = size; }
    /// The width it needs to show `text`.
    [[nodiscard]] int widthFor(const juce::String& text) const;
    /// While its chooser shows, it stays lit as if hovered.
    void setOpen(bool open);

    std::function<void()> onOpen;
    std::function<void()> onPopupMenu;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    [[nodiscard]] juce::Font font() const;

    juce::String value_;
    bool placeholder_ = false, open_ = false;
    float fontSize_ = 13.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceField)
};
} // namespace fd::ui
