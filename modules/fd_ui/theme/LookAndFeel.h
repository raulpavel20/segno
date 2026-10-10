#pragma once

namespace fd::ui
{
/// A menu bar model that names the letter of each menu's title that Alt opens it with (ADR 0040,
/// docs/design/menu-bar.md §5); the menu bar underlines it while `mnemonicsShown` is set on it.
class MenuMnemonics
{
public:
    virtual ~MenuMnemonics() = default;
    /// The index of menu `menu`'s mnemonic letter in its title, or -1.
    [[nodiscard]] virtual int mnemonicIndex(int menu) const = 0;
};
/// A MenuBarComponent's property (bool): its mnemonics are underlined (Alt is held).
inline const juce::Identifier mnemonicsShown{"fdMnemonicsShown"};

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

    // The menu bar in the title bar (ADR 0040): titles in `Field`, padded like text buttons; the
    // hovered one on the hover wash, the open one on its menu's surface, reaching down to it.
    juce::Font getMenuBarFont(juce::MenuBarComponent& bar, int itemIndex, const juce::String& itemText) override;
    int getMenuBarItemWidth(juce::MenuBarComponent& bar, int itemIndex, const juce::String& itemText) override;
    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool isMouseOverBar, juce::MenuBarComponent& bar) override;
    void drawMenuBarItem(juce::Graphics& g, int width, int height, int itemIndex, const juce::String& itemText,
                         bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent& bar) override;
    // A window without the system's title bar (ADR 0040): the darkest surface, and a 1 px line
    // round it while it isn't maximised.
    void fillResizableWindowBackground(juce::Graphics& g, int w, int h, const juce::BorderSize<int>& border,
                                       juce::ResizableWindow& window) override;
    void drawResizableWindowBorder(juce::Graphics& g, int w, int h, const juce::BorderSize<int>& border,
                                   juce::ResizableWindow& window) override;
};
} // namespace fd::ui
