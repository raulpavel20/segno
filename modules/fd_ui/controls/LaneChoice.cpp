namespace fd::ui
{
LaneChoice::LaneChoice() : juce::Button({})
{
    setWantsKeyboardFocus(false); // the view keeps its keys
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LaneChoice::setValue(const juce::String& label)
{
    if (label == value_)
        return;
    value_ = label;
    setTitle(label); // for accessibility: what it shows
    repaint();
}

bool LaneChoice::fits(const juce::String& label, int width)
{
    const auto& s = Theme::current().shell;
    return juce::GlyphArrangement::getStringWidth(capsLabelFont(), label.toUpperCase()) <=
           static_cast<float>(width - 2 * s.itemGap);
}

void LaneChoice::showMenu(juce::PopupMenu menu)
{
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void LaneChoice::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto& s = t.shell;
    const auto bounds = getLocalBounds().toFloat().reduced(static_cast<float>(s.itemGap));
    if (highlighted || down)
    {
        g.setColour(t.roles.hoverWash);
        g.fillRoundedRectangle(bounds, t.radiusControl);
    }
    // The label a row high and the chevron under it, together in the middle.
    const auto row = static_cast<float>(s.rowHeight), gap = static_cast<float>(s.itemGap);
    const float top = bounds.getCentreY() - (row + gap + kChevron) * 0.5f;
    const auto colour = highlighted || down ? t.textPrimary : t.textSecondary;
    paintCapsLabel(g, value_, juce::Rectangle<float>(bounds.getX(), top, bounds.getWidth(), row),
                   juce::Justification::centred, colour);
    const auto chevron =
        juce::Rectangle<float>(bounds.getCentreX() - kChevron * 0.5f, top + row + gap, kChevron, kChevron);
    drawIcon(g, Icon::ChevronDown, chevron, colour);
}
} // namespace fd::ui
