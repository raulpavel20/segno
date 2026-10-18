namespace fd::ui
{
// ---- ModulationModel -----------------------------------------------------------------------------

std::optional<Knob::Ring> ModulationModel::ringFor(size_t param, float value) const
{
    const int shown = draggedSource >= 0 ? draggedSource : hoveredSource;
    std::vector<Route> mine;
    bool fromShown = false;
    for (const auto& r : routes())
        if (r.param == param)
        {
            mine.push_back(r);
            fromShown = fromShown || r.source == shown;
        }
    if (mine.empty())
        return std::nullopt;
    // From the value, the amounts summed each way: a source that swings both ways goes both.
    Knob::Ring ring{value, value, shown >= 0 && !fromShown};
    for (const auto& r : mine)
    {
        if (fromShown && r.source != shown)
            continue;
        if (r.bipolar)
        {
            ring.from -= std::abs(r.amount);
            ring.to += std::abs(r.amount);
        }
        else
            (r.amount < 0.0f ? ring.from : ring.to) += r.amount;
    }
    ring.from = std::clamp(ring.from, 0.0f, 1.0f);
    ring.to = std::clamp(ring.to, 0.0f, 1.0f);
    return ring;
}

std::optional<ModulationModel::Route> ModulationModel::routeToDrag(size_t param) const
{
    std::optional<Route> first;
    for (const auto& r : routes())
        if (r.param == param)
        {
            if (r.source == lastSource)
                return r;
            if (!first)
                first = r;
        }
    return first;
}

bool ModulationModel::movesHovered(int source) const
{
    if (!hoveredParam)
        return false;
    const auto all = routes();
    return std::any_of(all.begin(), all.end(),
                       [&](const Route& r) { return r.param == *hoveredParam && r.source == source; });
}

juce::String ModulationModel::handleTooltip(int source) const
{
    const auto how = "Drag onto a knob to modulate it with " + sourceName(source);
    return isFull() ? how + " (the matrix is full: remove a route first)" : how;
}

// ---- ModulationHandle ----------------------------------------------------------------------------

ModulationHandle::ModulationHandle(ModulationModel& model, int source) : model_(model), source_(source)
{
    setMouseCursor(hand_.get());
    setWantsKeyboardFocus(true);
}

void ModulationHandle::setSource(int source)
{
    source_ = source;
    refresh();
    repaint();
}

void paintModulationCable(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to)
{
    const auto& t = Theme::current();
    const float sag =
        std::min(0.15f * std::abs(to.x - from.x), static_cast<float>(t.devicePanelHeight) * 0.25f);
    juce::Path cable;
    cable.startNewSubPath(from);
    cable.cubicTo(from.translated(0.0f, sag), to.translated(0.0f, sag), to);
    const float line = t.knobRingThickness;
    g.setColour(t.surface0);
    g.strokePath(cable, juce::PathStrokeType(3.0f * line, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setGradientFill(juce::ColourGradient(t.effectStart, from, t.effectEnd, to, false));
    g.strokePath(cable, juce::PathStrokeType(line, juce::PathStrokeType::curved));
    const float dot = 2.0f * (line + 0.5f);
    g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre(from));
    g.fillEllipse(juce::Rectangle<float>(2.0f * dot, 2.0f * dot).withCentre(to));
}

void ModulationHandle::refresh()
{
    const bool lit =
        model_.hoveredSource == source_ || model_.draggedSource == source_ || model_.movesHovered(source_);
    if (lit != lit_)
    {
        lit_ = lit;
        repaint();
    }
}

void ModulationHandle::setCount(int routes)
{
    if (routes != count_)
    {
        count_ = routes;
        repaint();
    }
}

void ModulationHandle::setLocked(bool locked)
{
    if (locked != locked_)
    {
        locked_ = locked;
        repaint();
    }
}

void ModulationHandle::setCompact(bool compact)
{
    if (compact != compact_)
    {
        compact_ = compact;
        repaint();
    }
}

namespace
{
juce::String countText(int count)
{
    return count > 0 ? juce::String::fromUTF8("\xC2\xB7 ") + juce::String(count) : juce::String();
}
} // namespace

int ModulationHandle::preferredWidth() const
{
    const auto& t = Theme::current();
    const int gap = t.shell.itemGap, icon = t.deviceGrid.sourceHandle - gap;
    if (compact_)
        return gap + icon + gap;
    const auto width = [](const juce::String& text)
    {
        return static_cast<int>(
            std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Caption), text)));
    };
    int w = gap + icon + gap + width(model_.sourceName(source_)) + gap;
    if (count_ > 0)
        w += width(countText(count_)) + gap;
    if (locked_)
        w += icon + gap;
    return w;
}

void ModulationHandle::paint(juce::Graphics& g)
{
    // A chip: the arrow and the source's name, so it reads as something to pick up.
    const auto& t = Theme::current();
    const int gap = t.shell.itemGap, icon = t.deviceGrid.sourceHandle - gap;
    auto r = getLocalBounds();
    g.setColour(t.surface3);
    g.fillRoundedRectangle(r.toFloat(), t.radiusControl);
    const auto colour = lit_ ? t.effectEnd : t.textSecondary;
    r.removeFromLeft(gap);
    drawIcon(g, Icon::Move, r.removeFromLeft(icon).withSizeKeepingCentre(icon, icon).toFloat(), colour);
    if (compact_)
        return;
    r.removeFromLeft(gap);
    g.setFont(font(TextStyle::Caption));
    const auto name = model_.sourceName(source_);
    const auto take = [&](const juce::String& text)
    {
        return r.removeFromLeft(static_cast<int>(
            std::ceil(juce::GlyphArrangement::getStringWidth(font(TextStyle::Caption), text))));
    };
    g.setColour(colour);
    g.drawText(name, take(name), juce::Justification::centredLeft, false);
    if (count_ > 0)
    {
        r.removeFromLeft(gap);
        const auto count = countText(count_);
        g.setColour(t.textTertiary);
        g.drawText(count, take(count), juce::Justification::centredLeft, false);
    }
    if (locked_)
    {
        r.removeFromLeft(gap);
        drawIcon(g, Icon::Lock, r.removeFromLeft(icon).withSizeKeepingCentre(icon, icon).toFloat(),
                 t.textSecondary);
    }
}

void ModulationHandle::mouseEnter(const juce::MouseEvent&)
{
    model_.hoveredSource = model_.lastSource = source_;
    refresh();
}

void ModulationHandle::mouseExit(const juce::MouseEvent&)
{
    if (model_.hoveredSource == source_)
        model_.hoveredSource = -1;
    refresh();
}

void ModulationHandle::mouseDown(const juce::MouseEvent& e)
{
    cancelled_ = false;
    if (e.mods.isPopupMenu())
    {
        cancelled_ = true; // a right-click isn't a drag
        if (onPopupMenu)
            onPopupMenu();
        return;
    }
    grabKeyboardFocus(); // Escape cancels the drag
}

void ModulationHandle::mouseDrag(const juce::MouseEvent& e)
{
    if (cancelled_)
        return;
    if (onDrag)
        onDrag(e);
    else if (auto* panel = findParentComponentOfClass<DevicePanel>())
        panel->dragSource(source_, e.getEventRelativeTo(panel).getPosition());
}

void ModulationHandle::mouseUp(const juce::MouseEvent& e)
{
    const bool cancelled = cancelled_ || !e.mouseWasDraggedSinceMouseDown();
    if (cancelled_ && onRelease) // cancelled by Escape already
        return;
    if (onRelease)
        onRelease(e, cancelled);
    else if (auto* panel = findParentComponentOfClass<DevicePanel>())
        panel->dropSource(source_, e.getEventRelativeTo(panel).getPosition(), cancelled);
}

bool ModulationHandle::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey || model_.draggedSource != source_)
        return false;
    cancelled_ = true;
    if (onCancel)
        onCancel();
    else if (auto* panel = findParentComponentOfClass<DevicePanel>())
        panel->dropSource(source_, {}, true);
    return true;
}
} // namespace fd::ui
