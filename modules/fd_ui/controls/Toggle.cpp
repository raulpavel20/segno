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

void Toggle::setCutsText(bool cuts)
{
    if (cuts == cuts_)
        return;
    cuts_ = cuts;
    repaint();
}

void Toggle::setModified(bool modified)
{
    if (modified == modified_)
        return;
    modified_ = modified;
    repaint();
}

void Toggle::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (onDoubleClick && !e.mods.isPopupMenu())
        onDoubleClick();
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

int Toggle::widthFor(const juce::String& text, bool icon)
{
    const auto& s = Theme::current().shell;
    return static_cast<int>(
               std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), text))) +
           2 * s.buttonTextPadding + (icon ? s.toggleIconSize + s.itemGap : 0);
}

void Toggle::setIcon(std::optional<Icon> icon)
{
    icon_ = icon;
    repaint();
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

    // A text-less toggle is a round indicator (device power, a source's On), centred.
    if (getButtonText().isEmpty())
    {
        const float side = std::min(bounds.getWidth(), bounds.getHeight());
        const auto dot = bounds.withSizeKeepingCentre(side, side).reduced(side * 0.22f);
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

    const auto ink = on ? t.onActive : (highlighted ? t.textPrimary : t.textSecondary);
    g.setColour(ink);
    g.setFont(font(TextStyle::Control));
    if (icon_)
    {
        // The icon, a gap, the label, together in the middle.
        const auto& s = t.shell;
        const int text = static_cast<int>(
            std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), getButtonText())));
        const int content = s.toggleIconSize + s.itemGap + text;
        auto area = getLocalBounds().withSizeKeepingCentre(content, getHeight());
        const auto size = static_cast<float>(s.toggleIconSize);
        drawIcon(g, *icon_, area.removeFromLeft(s.toggleIconSize).toFloat().withSizeKeepingCentre(size, size),
                 ink);
        area.removeFromLeft(s.itemGap);
        g.drawText(getButtonText(), area, juce::Justification::centredLeft, false);
    }
    else if (cuts_ || modified_) // a name of your own: cut to fit, its changed mark never cut
        paintNameWithMark(g, getButtonText(), modified_, getLocalBounds().reduced(t.shell.buttonTextPadding, 0),
                          juce::Justification::centred);
    else
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    paintAutomationIndicator(g, {3.5f, 3.5f}, automation_); // over the corner

    // Modulators (modulators.md §7.4): a line along the bottom edge, inside, as on a bar.
    if (dropTarget_ || dropCandidate_ || modulation_)
    {
        juce::Graphics::ScopedSaveState saved(g);
        juce::Path shape;
        shape.addRoundedRectangle(bounds, t.radiusControl);
        g.reduceClipRegion(shape);
        const auto line = bounds.withTop(bounds.getBottom() - t.knobRingThickness);
        if (dropTarget_ || dropCandidate_)
            g.setColour(dropTarget_ ? t.active : t.textTertiary);
        else
        {
            g.setGradientFill(juce::ColourGradient(t.effectStart, line.getTopLeft(), t.effectEnd,
                                                   line.getTopRight(), false));
            g.setOpacity(modulation_->dimmed || !modulation_->turned ? t.roles.dimAlpha : 1.0f);
        }
        g.fillRect(line);
    }
}

void Toggle::setModulation(std::optional<Modulation> modulation)
{
    if (modulation != modulation_)
    {
        modulation_ = modulation;
        repaint();
    }
}

void Toggle::setDropTarget(bool target)
{
    if (target != dropTarget_)
    {
        dropTarget_ = target;
        repaint();
    }
}

void Toggle::setDropCandidate(bool candidate)
{
    if (candidate != dropCandidate_)
    {
        dropCandidate_ = candidate;
        repaint();
    }
}
} // namespace fd::ui
