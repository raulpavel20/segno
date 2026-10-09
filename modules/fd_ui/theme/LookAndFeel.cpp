namespace fd::ui
{
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypeface(fonts::typeface(fonts::Weight::Regular));
    applyTheme();
}

void LookAndFeel::applyTheme()
{
    const auto& t = Theme::current();
    setColourScheme({t.surface1, t.surface0, t.surface1, t.line, t.textPrimary, t.surface3, t.textPrimary, t.active,
                     t.textPrimary});

    setColour(juce::ResizableWindow::backgroundColourId, t.surface0);
    setColour(juce::DocumentWindow::textColourId, t.textPrimary);
    setColour(juce::Label::textColourId, t.textPrimary);
    setColour(juce::TextEditor::backgroundColourId, t.surface0);
    setColour(juce::TextEditor::textColourId, t.textPrimary);
    setColour(juce::TextEditor::outlineColourId, t.line);
    setColour(juce::TextEditor::focusedOutlineColourId, t.active);
    setColour(juce::TextEditor::highlightColourId, t.roles.textHighlight);
    setColour(juce::CaretComponent::caretColourId, t.active);
    setColour(juce::TextButton::buttonColourId, t.surface3);
    setColour(juce::TextButton::buttonOnColourId, t.active);
    setColour(juce::TextButton::textColourOffId, t.textSecondary);
    setColour(juce::TextButton::textColourOnId, t.onActive);
    setColour(juce::PopupMenu::backgroundColourId, t.surface2);
    setColour(juce::PopupMenu::textColourId, t.textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.surface3);
    setColour(juce::PopupMenu::highlightedTextColourId, t.textPrimary);
    setColour(juce::TooltipWindow::backgroundColourId, t.surface3);
    setColour(juce::TooltipWindow::textColourId, t.textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, t.line);
    setColour(juce::ScrollBar::thumbColourId, t.surface3);
    setColour(juce::ComboBox::backgroundColourId, t.surface3);
    setColour(juce::ComboBox::textColourId, t.textPrimary);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return fonts::typeface(font.isBold() ? fonts::Weight::SemiBold : fonts::Weight::Regular);
    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font LookAndFeel::getLabelFont(juce::Label& label)
{
    // A combo box's text is the combo's font (11 px semibold), like every other control.
    if (auto* combo = dynamic_cast<juce::ComboBox*>(label.getParentComponent()))
        return getComboBoxFont(*combo);
    return fonts::font(Theme::current().textSm);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return fonts::font(Theme::current().textSm, fonts::Weight::Medium);
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return fonts::font(Theme::current().textMd);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                       bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto bounds = button.getLocalBounds().toFloat();
    g.setColour(t.lift(button.getToggleState() ? t.active : backgroundColour, highlighted, down));
    g.fillRoundedRectangle(bounds, t.radiusControl);
}

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& t = Theme::current();
    const auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(t.surface3);
    g.fillRoundedRectangle(bounds, t.radiusControl);
    g.setColour(t.line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), t.radiusControl, 1.0f);
    g.setColour(t.textPrimary);
    g.setFont(fonts::font(t.textSm));
    g.drawFittedText(text, bounds.toNearestInt().reduced(8, 4), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                   juce::Rectangle<int> parentArea)
{
    const auto f = fonts::font(Theme::current().textSm);
    const int w = juce::jmin(320, juce::GlyphArrangement::getStringWidthInt(f, tipText) + 18);
    const int h = static_cast<int>(f.getHeight()) + 10;
    return juce::Rectangle<int>(screenPos.x + 12, screenPos.y + 16, w, h).constrainedWithin(parentArea);
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto& t = Theme::current();
    g.fillAll(t.surface2);
    g.setColour(t.line);
    g.drawRect(0, 0, width, height, 1);
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                               juce::ComboBox& box)
{
    const auto& t = Theme::current();
    const auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(t.lift(t.surface3, box.isMouseOver(true), isButtonDown));
    g.fillRoundedRectangle(bounds, t.radiusControl);
    // Small flat chevron on the right.
    const float cx = static_cast<float>(width) - 9.0f, cy = static_cast<float>(height) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.0f, cy - 1.5f);
    chevron.lineTo(cx, cy + 1.5f);
    chevron.lineTo(cx + 3.0f, cy - 1.5f);
    g.setColour(t.textSecondary);
    g.strokePath(chevron, juce::PathStrokeType(1.2f));
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return fonts::font(Theme::current().textXs, fonts::Weight::SemiBold);
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    // Text from 6 px up to the chevron's 14 px: a combo fits its widest choice + 26 px
    // (docs/design/device-panels.md), with 6 px to spare. No extra label insets.
    label.setBounds(6, 0, box.getWidth() - 20, box.getHeight());
    label.setBorderSize({});
    label.setMinimumHorizontalScale(1.0f);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}
} // namespace fd::ui
