namespace fd::ui
{
Toggle::Toggle(const juce::String& text) : juce::Button(text)
{
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
}

void Toggle::setAutomationState(AutomationState state)
{
    if (state == automation_)
        return;
    automation_ = state;
    repaint();
}

void Toggle::mouseDown(const juce::MouseEvent& e)
{
    popup_ = e.mods.isPopupMenu();
    if (popup_)
    {
        if (onPopupMenu)
            onPopupMenu();
        return;
    }
    juce::Button::mouseDown(e);
}

void Toggle::mouseUp(const juce::MouseEvent& e)
{
    if (std::exchange(popup_, false))
        return;
    juce::Button::mouseUp(e);
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
        paintAutomationIndicator(g, {3.5f, 3.5f}, automation_);
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
    paintAutomationIndicator(g, {3.5f, 3.5f}, automation_); // over the corner
}
} // namespace fd::ui
