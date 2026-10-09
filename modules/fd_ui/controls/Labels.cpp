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
} // namespace fd::ui
