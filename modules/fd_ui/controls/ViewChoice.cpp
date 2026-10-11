namespace fd::ui
{
ViewChoice::ViewChoice() : juce::Button({})
{
    setWantsKeyboardFocus(false); // the view keeps its keys
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ViewChoice::setValue(const juce::String& value)
{
    if (value == value_)
        return;
    value_ = value;
    setTitle(value); // for accessibility: what it's set to
    repaint();
}

int ViewChoice::preferredWidth() const
{
    const auto text = juce::GlyphArrangement::getStringWidth(font(TextStyle::Control), value_);
    return kPadding + static_cast<int>(std::ceil(text)) + kGap + static_cast<int>(kChevron) + kPadding;
}

void ViewChoice::showMenu(juce::PopupMenu menu)
{
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void ViewChoice::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const auto body = bounds.withSizeKeepingCentre(
        bounds.getWidth(), std::min(bounds.getHeight(), static_cast<float>(t.shell.rowHeight)));
    if (highlighted || down)
    {
        g.setColour(t.roles.hoverWash);
        g.fillRoundedRectangle(body, t.radiusControl);
    }

    const auto colour = highlighted || down ? t.textPrimary : t.textSecondary;
    const auto f = font(TextStyle::Control);
    auto area = body.reduced(static_cast<float>(kPadding), 0.0f);
    const auto chevron = area.removeFromRight(kChevron).withSizeKeepingCentre(kChevron, kChevron);
    area.removeFromRight(static_cast<float>(kGap));
    g.setColour(colour);
    g.setFont(f);
    g.drawText(value_, area.toNearestInt(), juce::Justification::centredLeft, true);
    drawIcon(g, Icon::ChevronDown, chevron, colour);
}
} // namespace fd::ui
