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
    setColour(juce::TextEditor::highlightColourId, t.active.withAlpha(0.35f));
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

juce::Font LookAndFeel::getLabelFont(juce::Label&)
{
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
    auto colour = button.getToggleState() ? t.active : backgroundColour;
    if (down)
        colour = colour.brighter(0.12f);
    else if (highlighted)
        colour = colour.brighter(0.06f);
    g.setColour(colour);
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
} // namespace fd::ui
