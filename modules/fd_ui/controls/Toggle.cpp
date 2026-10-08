namespace fd::ui
{
Toggle::Toggle(const juce::String& text) : juce::Button(text)
{
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
}

void Toggle::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float lift = down ? 0.12f : (highlighted ? 0.06f : 0.0f);

    // A text-less square toggle is a round indicator (e.g. device power).
    if (getButtonText().isEmpty() && getWidth() == getHeight())
    {
        const auto dot = bounds.reduced(bounds.getWidth() * 0.22f);
        g.setColour(on ? t.active.brighter(lift) : t.surface3.brighter(lift + 0.15f));
        g.fillEllipse(dot);
        return;
    }

    g.setColour(on ? t.active.brighter(lift) : t.surface3.brighter(lift));
    g.fillRoundedRectangle(bounds, t.radiusControl);

    if (hasKeyboardFocus(false))
    {
        g.setColour(t.active.withAlpha(0.7f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), t.radiusControl, 1.0f);
    }

    g.setColour(on ? t.onActive : (highlighted ? t.textPrimary : t.textSecondary));
    g.setFont(fonts::font(t.textXs, fonts::Weight::SemiBold));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}
} // namespace fd::ui
