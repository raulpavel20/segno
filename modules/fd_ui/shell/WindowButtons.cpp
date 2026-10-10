namespace fd::ui
{
WindowButtons::WindowButtons()
{
    setWantsKeyboardFocus(false);
}

void WindowButtons::setMaximised(bool maximised)
{
    if (maximised_ == maximised)
        return;
    maximised_ = maximised;
    repaint();
}

int WindowButtons::widthFor()
{
    return 3 * Theme::current().shell.windowButtonWidth;
}

juce::Rectangle<int> WindowButtons::area(Part part) const
{
    const int w = Theme::current().shell.windowButtonWidth;
    const int index = part == Part::Minimise ? 0 : part == Part::Maximise ? 1 : part == Part::Close ? 2 : -1;
    if (index < 0)
        return {};
    return {getWidth() - (3 - index) * w, 0, w, getHeight()};
}

WindowButtons::Part WindowButtons::partAt(juce::Point<int> p) const
{
    for (const auto part : {Part::Minimise, Part::Maximise, Part::Close})
        if (area(part).contains(p))
            return part;
    return Part::None;
}

void WindowButtons::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    for (const auto part : {Part::Minimise, Part::Maximise, Part::Close})
    {
        const auto r = area(part);
        const bool hot = hovered_ == part, down = pressed_ == part && hot;
        if (hot)
        {
            g.setColour(part == Part::Close ? t.lift(t.record, false, down)
                                            : t.lift(t.roles.hoverWash, false, down));
            g.fillRect(r);
        }
        const auto icon = part == Part::Minimise ? Icon::Minimise
                          : part == Part::Close  ? Icon::Close
                          : maximised_           ? Icon::Restore
                                                 : Icon::Maximise;
        // The glyph as an IconButton as tall as the bar draws it, centred in the wider button.
        const auto side = static_cast<float>(r.getHeight()) * 0.6f;
        drawIcon(g, icon, r.toFloat().withSizeKeepingCentre(side, side),
                 hot ? t.textPrimary : t.textSecondary);
    }
}

void WindowButtons::setHovered(Part part)
{
    if (hovered_ == part)
        return;
    hovered_ = part;
    repaint();
}

void WindowButtons::mouseMove(const juce::MouseEvent& e)
{
    setHovered(partAt(e.getPosition()));
}

void WindowButtons::mouseExit(const juce::MouseEvent&)
{
    setHovered(Part::None);
}

void WindowButtons::mouseDown(const juce::MouseEvent& e)
{
    pressed_ = e.mods.isLeftButtonDown() ? partAt(e.getPosition()) : Part::None;
    setHovered(partAt(e.getPosition()));
    repaint();
}

void WindowButtons::mouseUp(const juce::MouseEvent& e)
{
    const auto pressed = std::exchange(pressed_, Part::None);
    repaint();
    if (pressed != Part::None && partAt(e.getPosition()) == pressed && onClick)
        onClick(pressed);
}

juce::String WindowButtons::getTooltip()
{
    switch (hovered_)
    {
    case Part::Minimise:
        return "Minimise";
    case Part::Maximise:
        return maximised_ ? "Restore" : "Maximise";
    case Part::Close:
        return "Close";
    case Part::None:
        break;
    }
    return {};
}
} // namespace fd::ui
