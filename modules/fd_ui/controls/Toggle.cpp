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

void Toggle::setArmStyle(bool arm)
{
    arm_ = arm;
    repaint();
}

void Toggle::setPartlyOn(bool partly)
{
    if (partly == partly_)
        return;
    partly_ = partly;
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

    // An arm button: a record dot, filled when armed, a ring when the selection arms it.
    if (arm_)
    {
        const auto dot = bounds.withSizeKeepingCentre(std::min(bounds.getWidth(), bounds.getHeight()),
                                                      std::min(bounds.getWidth(), bounds.getHeight()))
                             .reduced(bounds.getHeight() * 0.25f);
        if (on)
        {
            g.setColour(t.lift(t.record, highlighted, down));
            g.fillEllipse(dot);
        }
        else
        {
            g.setColour(t.lift(t.surface3, highlighted, down));
            g.fillEllipse(dot);
            if (partly_)
            {
                g.setColour(t.record);
                g.drawEllipse(dot.reduced(0.75f), 1.5f);
            }
        }
        return;
    }

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
