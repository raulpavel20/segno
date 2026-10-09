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

    // A text-less square toggle is a round indicator (e.g. device power).
    if (getButtonText().isEmpty() && getWidth() == getHeight())
    {
        const auto dot = bounds.reduced(bounds.getWidth() * 0.22f);
        g.setColour(t.lift(on ? t.active : t.surface3.brighter(0.15f), highlighted, down));
        g.fillEllipse(dot);
        return;
    }

    g.setColour(t.lift(on ? t.active : t.surface3, highlighted, down));
    g.fillRoundedRectangle(bounds, t.radiusControl);

    if (hasKeyboardFocus(false))
    {
        g.setColour(t.roles.focusRing);
        g.drawRoundedRectangle(bounds.reduced(0.5f), t.radiusControl, 1.0f);
    }

    g.setColour(on ? t.onActive : (highlighted ? t.textPrimary : t.textSecondary));
    g.setFont(font(TextStyle::Control));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}
} // namespace fd::ui
