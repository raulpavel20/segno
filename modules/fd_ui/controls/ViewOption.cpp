namespace fd::ui
{
ViewOption::ViewOption(const juce::String& text) : juce::Button(text)
{
    setClickingTogglesState(true);
    setWantsKeyboardFocus(false); // the view keeps its keys (the mixer's arrows)
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int ViewOption::preferredWidth() const
{
    const auto text = juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), getButtonText());
    return kPadding + static_cast<int>(kDot) + kGap + static_cast<int>(std::ceil(text)) + kPadding;
}

void ViewOption::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const bool on = getToggleState();
    const auto bounds = getLocalBounds().toFloat();
    const auto body = bounds.withSizeKeepingCentre(bounds.getWidth(), std::min(bounds.getHeight(), static_cast<float>(t.shell.rowHeight)));

    if (highlighted || down)
    {
        g.setColour(t.roles.hoverWash);
        g.fillRoundedRectangle(body, t.radiusControl);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(t.roles.focusRing);
        g.drawRoundedRectangle(body.reduced(0.5f), t.radiusControl, 1.0f);
    }

    const juce::Rectangle<float> dot(body.getX() + kPadding, body.getCentreY() - kDot * 0.5f, kDot, kDot);
    if (on)
    {
        g.setColour(t.active);
        g.fillEllipse(dot);
    }
    else
    {
        g.setColour(highlighted ? t.textSecondary : t.textTertiary);
        g.drawEllipse(dot.reduced(0.5f), 1.0f);
    }

    g.setColour(on ? t.textPrimary : (highlighted ? t.textSecondary : t.textTertiary));
    g.setFont(font(TextStyle::Control));
    g.drawText(getButtonText(), body.withTrimmedLeft(kPadding + kDot + kGap).toNearestInt(), juce::Justification::centredLeft,
               false);
}
} // namespace fd::ui
