#pragma once

namespace fd::ui
{
/// One of a few named values, shown side by side (docs/design/ui-foundations.md, "Shared
/// components"): a `surface2` well with each value as a segment, the chosen one filled `active`.
/// For short lists whose every value is worth seeing at once: the Chord popover's Kind, Voicing
/// and Inversion (docs/design/piano-roll.md §7.3). Longer lists are a ChoiceField.
class Segments final : public juce::Component
{
public:
    explicit Segments(juce::StringArray items = {});

    void setItems(juce::StringArray items);
    [[nodiscard]] const juce::StringArray& items() const noexcept { return items_; }
    /// The chosen value (−1: none); repaints, without calling onChange.
    void setSelected(int index);
    [[nodiscard]] int selected() const noexcept { return selected_; }
    /// A segment was clicked: its index.
    std::function<void(int index)> onChange;

    /// The width that fits every segment: each its label and a text button's padding.
    [[nodiscard]] int preferredWidth() const;
    /// Segment `index`'s area (for tests).
    [[nodiscard]] juce::Rectangle<int> segmentArea(int index) const;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] int segmentAt(juce::Point<int> p) const;

    juce::StringArray items_;
    int selected_ = -1, hovered_ = -1;
};
} // namespace fd::ui
