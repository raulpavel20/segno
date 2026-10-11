namespace fd::ui
{
juce::Font capsLabelFont()
{
    return font(TextStyle::Section);
}

void paintCapsLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                    juce::Justification justification, std::optional<juce::Colour> colour)
{
    g.setColour(colour.value_or(Theme::current().textTertiary));
    g.setFont(capsLabelFont()); // mono: spaced already, no extra tracking
    g.drawText(text.toUpperCase(), area, justification, false);
}

const juce::String& modifiedMark()
{
    static const auto mark = juce::String::fromUTF8(" \xE2\x80\xA2"); // " •"
    return mark;
}

int nameWithMarkWidth(const juce::Font& font, const juce::String& name, bool modified)
{
    return static_cast<int>(std::ceil(
        juce::GlyphArrangement::getStringWidth(font, name + (modified ? modifiedMark() : juce::String()))));
}

void paintNameWithMark(juce::Graphics& g, const juce::String& name, bool modified, juce::Rectangle<int> area,
                       juce::Justification justification)
{
    const auto f = g.getCurrentFont();
    const int mark =
        modified ? static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(f, modifiedMark()))) : 0;
    const int text = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(f, name)));
    // Centred: the name and its mark as one, when they fit.
    if (justification.testFlags(juce::Justification::horizontallyCentred))
        area = area.withSizeKeepingCentre(std::min(area.getWidth(), text + mark), area.getHeight());
    // The mark right after the name; a name too long is cut so the mark still shows.
    const auto nameArea = area.removeFromLeft(std::max(0, std::min(text, area.getWidth() - mark)));
    const auto vertical = justification.getOnlyVerticalFlags();
    g.drawText(name, nameArea, juce::Justification(juce::Justification::left | vertical), true);
    if (modified)
        g.drawText(modifiedMark(), area.removeFromLeft(mark),
                   juce::Justification(juce::Justification::left | vertical), false);
}
} // namespace fd::ui
