namespace fd::ui
{
Segments::Segments(juce::StringArray items) : items_(std::move(items))
{
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void Segments::setItems(juce::StringArray items)
{
    items_ = std::move(items);
    selected_ = std::min(selected_, items_.size() - 1);
    repaint();
}

void Segments::setSelected(int index)
{
    index = index >= 0 && index < items_.size() ? index : -1;
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
}

int Segments::preferredWidth() const
{
    int width = 0;
    for (const auto& item : items_)
        width += Toggle::widthFor(item);
    return width;
}

juce::Rectangle<int> Segments::segmentArea(int index) const
{
    if (index < 0 || index >= items_.size())
        return {};
    int x = 0;
    for (int i = 0; i < index; ++i)
        x += Toggle::widthFor(items_[i]);
    // A row high, centred in a taller component (a panel header's option).
    const int height = std::min(getHeight(), Theme::current().shell.rowHeight);
    return {x, (getHeight() - height) / 2, Toggle::widthFor(items_[index]), height};
}

int Segments::segmentAt(juce::Point<int> p) const
{
    for (int i = 0; i < items_.size(); ++i)
        if (segmentArea(i).contains(p))
            return i;
    return -1;
}

void Segments::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const int height = std::min(getHeight(), t.shell.rowHeight);
    const auto well =
        juce::Rectangle<int>(0, (getHeight() - height) / 2, std::min(getWidth(), preferredWidth()), height)
            .toFloat();
    juce::Path shape;
    shape.addRoundedRectangle(well, t.radiusControl);
    g.setColour(t.surface2);
    g.fillPath(shape);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(shape); // the chosen segment's fill keeps the well's corners
    g.setFont(font(TextStyle::Control));
    for (int i = 0; i < items_.size(); ++i)
    {
        const auto area = segmentArea(i);
        if (i == selected_)
        {
            g.setColour(t.active);
            g.fillRect(area);
        }
        else if (i == hovered_)
        {
            g.setColour(t.roles.hoverWash);
            g.fillRect(area);
        }
        g.setColour(i == selected_ ? t.onActive : i == hovered_ ? t.textPrimary : t.textSecondary);
        g.drawText(items_[i], area, juce::Justification::centred, false);
    }
}

void Segments::mouseMove(const juce::MouseEvent& e)
{
    if (const int at = segmentAt(e.getPosition()); at != hovered_)
    {
        hovered_ = at;
        repaint();
    }
}

void Segments::mouseExit(const juce::MouseEvent&)
{
    hovered_ = -1;
    repaint();
}

void Segments::mouseDown(const juce::MouseEvent& e)
{
    const int at = segmentAt(e.getPosition());
    if (at < 0 || e.mods.isPopupMenu())
        return;
    setSelected(at);
    if (onChange)
        onChange(at);
}
} // namespace fd::ui
