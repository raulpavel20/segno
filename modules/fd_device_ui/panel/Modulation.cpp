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
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setWantsKeyboardFocus(true);
}

void ModulationHandle::setSource(int source)
{
    source_ = source;
    refresh();
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

void ModulationHandle::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    drawIcon(g, Icon::Move, getLocalBounds().toFloat().reduced(1.0f), lit_ ? t.effectEnd : t.textTertiary);
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

void ModulationHandle::mouseDown(const juce::MouseEvent&)
{
    cancelled_ = false;
    grabKeyboardFocus(); // Escape cancels the drag
}

void ModulationHandle::mouseDrag(const juce::MouseEvent& e)
{
    if (cancelled_)
        return;
    if (auto* panel = findParentComponentOfClass<DevicePanel>())
        panel->dragSource(source_, e.getEventRelativeTo(panel).getPosition());
}

void ModulationHandle::mouseUp(const juce::MouseEvent& e)
{
    if (auto* panel = findParentComponentOfClass<DevicePanel>())
        panel->dropSource(source_, e.getEventRelativeTo(panel).getPosition(),
                          cancelled_ || !e.mouseWasDraggedSinceMouseDown());
}

bool ModulationHandle::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey || model_.draggedSource != source_)
        return false;
    cancelled_ = true;
    if (auto* panel = findParentComponentOfClass<DevicePanel>())
        panel->dropSource(source_, {}, true);
    return true;
}
} // namespace fd::ui
