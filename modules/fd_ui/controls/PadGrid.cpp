namespace fd::ui
{
// ---- the pads ---------------------------------------------------------------------------------

PadGrid::PadGrid()
{
    setWantsKeyboardFocus(false);
    setRepaintsOnMouseActivity(false);
}

void PadGrid::setPads(int from, const std::array<Pad, shown>& pads, int selected)
{
    if (from == from_ && pads == pads_ && selected == selected_)
        return;
    from_ = from;
    pads_ = pads;
    selected_ = selected;
    repaint();
}

void PadGrid::setPlaying(const std::bitset<128>& notes)
{
    if (notes == playing_)
        return;
    playing_ = notes;
    repaint();
}

void PadGrid::setDropTarget(int note)
{
    if (note == dropTarget_)
        return;
    dropTarget_ = note;
    repaint();
}

const PadGrid::Pad& PadGrid::pad(int note) const
{
    static const Pad none;
    const int i = note - from_;
    return i >= 0 && i < shown ? pads_[static_cast<size_t>(i)] : none;
}

juce::Rectangle<int> PadGrid::padBounds(int note) const
{
    const int i = note - from_;
    if (i < 0 || i >= shown)
        return {};
    // Whole pixels: the pads share what's left after their gaps; the lowest row at the bottom.
    const int gap = Theme::current().deviceGrid.padGap;
    const int w = (getWidth() - (columns - 1) * gap) / columns;
    const int h = (getHeight() - (rows - 1) * gap) / rows;
    const int column = i % columns, row = i / columns;
    return {column * (w + gap), (rows - 1 - row) * (h + gap), w, h};
}

juce::Rectangle<int> PadGrid::nameArea(int note) const
{
    const auto& grid = Theme::current().deviceGrid;
    const auto inner = padBounds(note).reduced(grid.padInset);
    return inner.withHeight(juce::roundToInt(font(TextStyle::Control).getHeight()));
}

juce::Rectangle<int> PadGrid::noteArea(int note) const
{
    const auto& grid = Theme::current().deviceGrid;
    const auto inner = padBounds(note).reduced(grid.padInset);
    const int h = juce::roundToInt(font(TextStyle::ValueSmall).getHeight());
    return inner.withTop(inner.getBottom() - h);
}

int PadGrid::padAt(juce::Point<int> p) const
{
    for (int i = 0; i < shown; ++i)
        if (padBounds(from_ + i).contains(p))
            return from_ + i;
    return -1;
}

void PadGrid::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    for (int i = 0; i < shown; ++i)
    {
        const int note = from_ + i;
        const auto& p = pads_[static_cast<size_t>(i)];
        const auto area = padBounds(note).toFloat();
        // An off pad recedes, whole (dimAlpha, never compounded).
        const float alpha = p.sound && !p.on ? t.roles.dimAlpha : 1.0f;
        const bool hot = note == hovered_ || note == pressed_;

        g.setColour(
            (p.sound ? t.lift(t.surface3, hot, note == pressed_) : t.surface0).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(area, t.radiusControl);
        if (playing_.test(static_cast<size_t>(note)))
        {
            g.setColour(t.fill(t.value));
            g.fillRoundedRectangle(area, t.radiusControl);
        }

        // Its name over its note, cut before S where it's soloed; an empty pad shows its note alone.
        auto name = nameArea(note);
        if (p.sound && p.solo)
        {
            const auto s = juce::roundToInt(
                std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Micro), "S")));
            g.setColour(t.active);
            g.setFont(font(TextStyle::Micro));
            g.drawText("S", name.removeFromRight(s), juce::Justification::centredRight, false);
            name.removeFromRight(t.shell.itemGap);
        }
        if (p.sound)
        {
            g.setColour((hot ? t.textPrimary : t.textSecondary).withMultipliedAlpha(alpha));
            g.setFont(font(TextStyle::Control));
            g.drawText(p.name, name, juce::Justification::centredLeft, true);
        }
        const auto noteRow = noteArea(note);
        g.setColour(t.textTertiary.withMultipliedAlpha(alpha));
        g.setFont(font(TextStyle::ValueSmall));
        g.drawText(p.note, noteRow, juce::Justification::centredLeft, false);
        // Its choke group at the note's right.
        if (p.sound && p.choke > 0)
        {
            g.setFont(font(TextStyle::Micro));
            g.drawText(juce::String(p.choke), noteRow, juce::Justification::centredRight, false);
        }

        if (note == dropTarget_)
        {
            const auto line = static_cast<float>(t.shell.dropLine);
            g.setColour(t.active);
            g.drawRoundedRectangle(area.reduced(line * 0.5f), t.radiusControl, line);
        }
        else if (note == selected_)
        {
            g.setColour(t.active);
            g.drawRoundedRectangle(area.reduced(0.5f), t.radiusControl, 1.0f);
        }
    }
}

juce::String PadGrid::getTooltip()
{
    if (hovered_ < 0)
        return {};
    const auto& p = pad(hovered_);
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    if (!p.sound)
        return p.note + dot + "empty: drop a sample or an instrument on it";
    return p.name + dot + p.note;
}

void PadGrid::setHovered(int note)
{
    if (note == hovered_)
        return;
    hovered_ = note;
    repaint();
}

void PadGrid::mouseMove(const juce::MouseEvent& e)
{
    setHovered(padAt(e.getPosition()));
}

void PadGrid::mouseExit(const juce::MouseEvent&)
{
    setHovered(-1);
}

void PadGrid::mouseDown(const juce::MouseEvent& e)
{
    const int note = padAt(e.getPosition());
    if (note < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu(note);
        return;
    }
    pressed_ = note;
    dragging_ = false;
    repaint();
    if (onPress)
        onPress(note);
}

void PadGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (pressed_ < 0 || !pad(pressed_).sound)
        return;
    if (!dragging_ && e.getDistanceFromDragStart() >= Theme::current().input.dragThreshold)
    {
        // A drag: the note stops, and the pad's sound goes where it's dropped.
        dragging_ = true;
        if (onRelease)
            onRelease(pressed_);
    }
    if (dragging_)
    {
        const int over = padAt(e.getPosition());
        setDropTarget(over != pressed_ ? over : -1);
    }
}

void PadGrid::mouseUp(const juce::MouseEvent& e)
{
    if (pressed_ < 0)
        return;
    const int from = pressed_;
    pressed_ = -1;
    if (dragging_)
    {
        dragging_ = false;
        const int to = dropTarget_;
        setDropTarget(-1);
        if (to >= 0 && onMovePad)
            onMovePad(from, to, e.mods.isCommandDown());
    }
    else if (onRelease)
        onRelease(from);
    repaint();
}

void PadGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int note = padAt(e.getPosition());
    if (note >= 0 && pad(note).sound && onDoubleClick)
        onDoubleClick(note);
}

void PadGrid::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY != 0.0f && onScroll)
        onScroll(wheel.deltaY > 0.0f ? 1 : -1);
}

// ---- the overview -----------------------------------------------------------------------------

namespace
{
constexpr int overviewRows = 128 / PadGrid::columns;
}

PadOverview::PadOverview()
{
    setWantsKeyboardFocus(false);
    setRepaintsOnMouseActivity(false);
}

void PadOverview::setPads(const std::bitset<128>& sounds, int from, int selected)
{
    if (sounds == sounds_ && from == from_ && selected == selected_)
        return;
    sounds_ = sounds;
    from_ = from;
    selected_ = selected;
    repaint();
}

juce::Rectangle<int> PadOverview::rowBounds(int row) const
{
    // Whole pixels: a row's pitch is a cell and a gap; the rows and columns centred in the well,
    // a frame's room (two gaps) clear of its edges.
    const int gap = Theme::current().deviceGrid.overviewGap;
    const int inset = 2 * gap;
    const int pitchY = (getHeight() - 2 * inset + gap) / overviewRows;
    const int pitchX = (getWidth() - 2 * inset + gap) / PadGrid::columns;
    const int width = PadGrid::columns * pitchX - gap, height = overviewRows * pitchY - gap;
    const int x = (getWidth() - width) / 2, top = (getHeight() - height) / 2;
    return {x, top + (overviewRows - 1 - row) * pitchY, width, pitchY - gap};
}

juce::Rectangle<int> PadOverview::cellBounds(int note) const
{
    const int gap = Theme::current().deviceGrid.overviewGap;
    const auto row = rowBounds(note / PadGrid::columns);
    const int pitchX = (row.getWidth() + gap) / PadGrid::columns;
    return row.withX(row.getX() + (note % PadGrid::columns) * pitchX).withWidth(pitchX - gap);
}

juce::Rectangle<int> PadOverview::frameBounds() const
{
    const int gap = Theme::current().deviceGrid.overviewGap;
    const int first = from_ / PadGrid::columns;
    return rowBounds(first).getUnion(rowBounds(first + PadGrid::rows - 1)).expanded(gap);
}

void PadOverview::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    g.setColour(t.surface0); // a well, as a display's
    g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
    for (int note = 0; note < 128; ++note)
    {
        g.setColour(note == selected_                         ? t.active
                    : sounds_.test(static_cast<size_t>(note)) ? t.textSecondary
                                                              : t.surface2);
        g.fillRect(cellBounds(note));
    }
    g.setColour(t.textSecondary);
    g.drawRect(frameBounds(), 1);
}

void PadOverview::showAt(int y)
{
    // The row under the pointer, second from the bottom of the 16 shown: the frame follows it.
    int row = 0;
    for (int r = 0; r < overviewRows; ++r)
        if (y >= rowBounds(r).getY())
        {
            row = r;
            break;
        }
    const int from = std::clamp((row - 1) * PadGrid::columns, 0, 128 - PadGrid::shown);
    if (from != from_ && onShow)
        onShow(from);
}

void PadOverview::mouseDown(const juce::MouseEvent& e)
{
    showAt(e.y);
}

void PadOverview::mouseDrag(const juce::MouseEvent& e)
{
    showAt(e.y);
}
} // namespace fd::ui
