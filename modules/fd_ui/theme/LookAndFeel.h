#pragma once

namespace fd::ui
{
/// Maps the current Theme onto JUCE's standard widgets (labels, text editors, menus, tooltips,
/// scrollbars). Our own controls (Knob, Toggle, Meter) paint themselves from the Theme.
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    /// Re-reads Theme::current(). Call after switching themes, then sendLookAndFeelChange().
    void applyTheme();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;
    juce::Font getLabelFont(juce::Label& label) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
};
} // namespace fd::ui
