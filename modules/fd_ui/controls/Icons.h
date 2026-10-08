#pragma once

namespace fd::ui
{
/// The icon set (ui-principles §3): line icons drawn in code on a 16 px grid with a 1.5 px
/// stroke; transport glyphs are filled. Add icons here only; every icon needs a tooltip.
enum class Icon
{
    Play,
    Stop,
    Record,
    Loop,
    Metronome,
    PanelLeft,   // browser
    PanelRight,  // inspector
    PanelBottom, // detail panel
    Plus,
    Settings,
    Undo,
    Redo
};

/// Draws `icon` fitted (aspect kept) into `area` in `colour`.
void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

/// Flat icon button (ui-principles §4): dim when off, `active` fill when on (toggles),
/// `surface3` on hover. Tooltip text should include the shortcut.
class IconButton : public juce::Button
{
public:
    explicit IconButton(Icon icon, const juce::String& name = {});

    void setIcon(Icon icon);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon_;
};
} // namespace fd::ui
