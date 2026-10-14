namespace fd::ui
{
Tile::Tile(Icon icon, const juce::String& name) : juce::Button(name), icon_(icon)
{
    setClickingTogglesState(false);
    setWantsKeyboardFocus(false); // the view keeps its keys
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int Tile::preferredWidth() const
{
    const auto& s = Theme::current().shell;
    const auto text = juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), getButtonText());
    return s.buttonTextPadding + s.toolIconSize + s.itemGap + static_cast<int>(std::ceil(text)) +
           s.buttonTextPadding;
}

void Tile::setCompact(bool compact)
{
    if (compact == compact_)
        return;
    compact_ = compact;
    repaint();
}

void Tile::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto& s = t.shell;
    const auto bounds = getLocalBounds().toFloat();
    const bool on = getToggleState(), enabled = isEnabled();
    g.setColour(on ? t.lift(t.active, highlighted, down)
                   : t.lift(t.surface2, highlighted && enabled, down && enabled));
    g.fillRoundedRectangle(bounds, t.radiusControl);
    const auto ink = on            ? t.onActive
                     : !enabled    ? t.textTertiary
                     : highlighted ? t.textPrimary
                                   : t.textSecondary;
    const auto size = static_cast<float>(s.toolIconSize);
    if (compact_)
    {
        drawIcon(g, icon_, bounds.withSizeKeepingCentre(size, size), ink);
        return;
    }
    auto area = getLocalBounds().reduced(s.buttonTextPadding, 0);
    drawIcon(g, icon_, area.removeFromLeft(s.toolIconSize).toFloat().withSizeKeepingCentre(size, size), ink);
    area.removeFromLeft(s.itemGap);
    g.setColour(ink);
    g.setFont(font(TextStyle::Control));
    g.drawText(getButtonText(), area, juce::Justification::centredLeft, true);
}
} // namespace fd::ui
