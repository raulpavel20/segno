#pragma once

namespace fd::ui
{
/// The palette as a grid of swatches (docs/design/tracks-and-clips.md §8): 4 by 4, a row high
/// each, `itemGap` apart and padded like a text button, the current colour ringed. In a menu:
/// `addColourGrid`; a click on a swatch chooses it and closes the menu.
class ColourGrid final : public juce::PopupMenu::CustomComponent
{
public:
    /// `current`: the colour ringed (-1: none).
    ColourGrid(int current, std::function<void(int)> chosen);

    static constexpr int columns = 4;
    /// The swatch at `p` (-1: none) and each swatch's area (tests and hit-testing).
    [[nodiscard]] int swatchAt(juce::Point<int> p) const;
    [[nodiscard]] juce::Rectangle<int> swatch(int index) const;

    void getIdealSize(int& width, int& height) override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    int current_, hovered_ = -1;
    std::function<void(int)> chosen_;
};

/// Adds the palette to `menu`: with `follow`, a "Track colour" item first (ticked while
/// `following`), chosen as nullopt; then the grid.
void addColourGrid(juce::PopupMenu& menu, int current, std::function<void(std::optional<int>)> chosen,
                   bool follow = false, bool following = false);

/// A colour value in a property row (the inspector's Colour): its swatch, a row high, on the
/// hover wash when hovered; a click opens the palette under it.
class ColourSwatch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ColourSwatch();
    /// The colour shown (-1: none) and what the palette offers.
    void setColour(int colour, bool follow = false, bool following = false);
    std::function<void(std::optional<int>)> onChosen;

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    int colour_ = -1;
    bool follow_ = false, following_ = false, hovered_ = false;
};
} // namespace fd::ui
