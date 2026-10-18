namespace fd::ui
{
void NoteDisplay::refresh()
{
    if (feed_ == nullptr)
        return;
    const double bpb = std::max(1.0, view_.beatsPerBar);
    view_ = feed_->view(barsBefore * bpb, barsAfter * bpb);
    repaint();
}

juce::Rectangle<float> NoteDisplay::plotArea() const
{
    const auto& grid = Theme::current().deviceGrid;
    auto area = getLocalBounds().toFloat().reduced(static_cast<float>(grid.displayInset));
    area.removeFromTop(static_cast<float>(grid.displayTextHeight + grid.displayInset));
    return area;
}

void NoteDisplay::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto& grid = t.deviceGrid;
    paintDisplayWell(g, getLocalBounds().toFloat());
    auto label = getLocalBounds()
                     .toFloat()
                     .reduced(static_cast<float>(grid.displayInset))
                     .removeFromTop(static_cast<float>(grid.displayTextHeight));
    const auto plot = rollArea();
    const double bpb = std::max(1.0, view_.beatsPerBar);
    const double from = view_.now - barsBefore * bpb, to = view_.now + barsAfter * bpb;

    // The label line: the chord now and the next on the left, what it follows on the right.
    {
        const auto caption = font(TextStyle::Caption);
        g.setFont(caption);
        g.setColour(t.textTertiary);
        const float followW = juce::GlyphArrangement::getStringWidth(caption, view_.following);
        g.drawText(view_.following, label.removeFromRight(followW + 1.0f), juce::Justification::centredRight,
                   false);
        label.removeFromRight(static_cast<float>(grid.displayInset));
        if (view_.fallback.isNotEmpty())
            g.drawText(view_.fallback, label, juce::Justification::centredLeft, true);
        else
        {
            const NoteFeed::Chord* now = nullptr;
            const NoteFeed::Chord* next = nullptr;
            for (const auto& c : view_.chords)
                if (c.start <= view_.now && view_.now < c.end)
                    now = &c;
                else if (c.start > view_.now && next == nullptr)
                    next = &c;
            const auto value = font(TextStyle::Value);
            if (now != nullptr)
            {
                g.setFont(value);
                g.setColour(t.textSecondary);
                const float w = std::min(label.getWidth(),
                                         juce::GlyphArrangement::getStringWidth(value, now->name) + 1.0f);
                g.drawText(now->name, label.removeFromLeft(w), juce::Justification::centredLeft, true);
            }
            if (next != nullptr)
            {
                const auto arrow = juce::String::fromUTF8(" \xE2\x86\x92 ");
                g.setFont(caption);
                g.setColour(t.textTertiary);
                const float aw = juce::GlyphArrangement::getStringWidth(caption, arrow);
                if (aw < label.getWidth())
                {
                    g.drawText(arrow, label.removeFromLeft(aw), juce::Justification::centred, false);
                    g.setFont(value);
                    g.drawText(next->name, label, juce::Justification::centredLeft, true);
                }
            }
        }
    }

    // The rows: the notes in sight, at least an octave, a row's margin above and below.
    int lo = 128, hi = -1;
    for (const auto& n : view_.notes)
        if (n.end > from && n.start < to)
        {
            lo = std::min(lo, n.key);
            hi = std::max(hi, n.key);
        }
    if (hi < lo)
    {
        lo = 54;
        hi = 66;
    }
    if (hi - lo < 12)
    {
        const int mid = (lo + hi) / 2;
        lo = mid - 6;
        hi = lo + 12;
    }
    --lo;
    ++hi;
    const float rowH = plot.getHeight() / static_cast<float>(hi - lo + 1);
    const auto xOf = [&](double beat)
    { return plot.getX() + static_cast<float>((beat - from) / (to - from)) * plot.getWidth(); };
    const auto yOf = [&](int key) { return plot.getBottom() - static_cast<float>(key - lo + 1) * rowH; };

    {
        // Nothing in sight: only the empty state, nothing drawn through it.
        if (std::none_of(view_.notes.begin(), view_.notes.end(),
                         [&](const auto& n) { return n.end > from && n.start < to; }))
        {
            paintEmptyState(g, plot.toNearestInt(), "Nothing played yet");
            paintOver(g, plotArea(), view_);
            return;
        }
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(plot.toNearestInt());
        for (double beat = std::ceil(from); beat < to; beat += 1.0)
        {
            const bool bar = std::fmod(beat, bpb) == 0.0;
            g.setColour(bar ? t.roles.gridBar : t.roles.gridBeat);
            g.fillRect(juce::Rectangle<float>(std::round(xOf(beat)), plot.getY(), 1.0f, plot.getHeight()));
        }
        if (view_.sectionEnd && *view_.sectionEnd > from && *view_.sectionEnd < to)
        {
            g.setColour(t.roles.marker);
            g.fillRect(juce::Rectangle<float>(std::round(xOf(*view_.sectionEnd)) - 0.5f, plot.getY(), 2.0f,
                                              plot.getHeight()));
        }
        const float h = std::max(2.0f, rowH - 1.0f);
        for (const bool played : {false, true})
            for (const auto& n : view_.notes)
                if (n.played == played && n.end > from && n.start < to)
                {
                    const float x0 = xOf(n.start), x1 = std::max(x0 + 2.0f, xOf(n.end) - 1.0f);
                    const auto r = juce::Rectangle<float>(x0, yOf(n.key) + (rowH - h) * 0.5f, x1 - x0, h);
                    g.setColour(played ? t.noteFill(t.value, n.velocity, false) : t.ghost(t.textSecondary));
                    g.fillRoundedRectangle(r, 1.0f);
                }
        g.setColour(t.roles.playhead);
        g.fillRect(juce::Rectangle<float>(std::round(xOf(view_.now)), plot.getY(), 1.0f, plot.getHeight()));
    }
    paintOver(g, plotArea(), view_); // over the whole plot, not only the notes' part
}

juce::String NoteDisplay::getTooltip()
{
    return "What came in (faint) and what it plays, around the playhead; above, the chord sounding "
           "and the next one, and what it follows";
}
// ---- StepDisplay ------------------------------------------------------------------------------

void StepDisplay::setLane(int index)
{
    lane_ = index;
    repaint();
}

void StepDisplay::refresh()
{
    NoteDisplay::refresh();
    repaint();
}

juce::Rectangle<float> StepDisplay::stepArea() const
{
    auto plot = plotArea();
    return plot.removeFromTop(std::round(plot.getHeight() * 0.62f));
}

juce::Rectangle<float> StepDisplay::rollArea() const
{
    auto plot = plotArea();
    plot.removeFromTop(std::round(plot.getHeight() * 0.62f) +
                       static_cast<float>(Theme::current().deviceGrid.displayInset));
    return plot;
}

int StepDisplay::stepAt(float x) const
{
    const auto area = stepArea();
    return std::clamp(static_cast<int>((x - area.getX()) / area.getWidth() * maxSteps), 0, maxSteps - 1);
}

void StepDisplay::paintOver(juce::Graphics& g, juce::Rectangle<float>, const NoteFeed::View&)
{
    if (steps_ == nullptr)
        return;
    const auto& t = Theme::current();
    const auto area = stepArea();
    const auto lane = steps_->lane(lane_);
    const int playing = steps_->playing(lane_);
    const float w = area.getWidth() / static_cast<float>(maxSteps), gap = 2.0f;
    const auto yOf = [&](float v)
    { return area.getBottom() - (v - lane.min) / std::max(1.0e-6f, lane.max - lane.min) * area.getHeight(); };
    for (int i = 0; i < maxSteps; ++i)
    {
        const auto cell = juce::Rectangle<float>(area.getX() + static_cast<float>(i) * w + gap * 0.5f,
                                                 area.getY(), w - gap, area.getHeight());
        const bool inside = i < lane.length;
        // Past the lane's length, the well alone: its line after the last step marks the end.
        if (!inside || i >= static_cast<int>(lane.values.size()))
            continue;
        g.setColour(t.surface1);
        g.fillRect(cell);
        const float v = lane.values[static_cast<std::size_t>(i)];
        // The step playing is the brightest; one left at its neutral value is grey: it changes nothing.
        const auto ink = i == playing ? t.textPrimary : (v == lane.neutral ? t.textTertiary : t.value);
        g.setColour(ink);
        if (lane.pitched)
        {
            const float rowH = area.getHeight() / std::max(1.0f, lane.max - lane.min + 1.0f);
            g.fillRoundedRectangle(cell.withY(yOf(v) - rowH).withHeight(std::max(2.0f, rowH - 1.0f)), 1.0f);
        }
        else
        {
            // Bars grow from the lane's floor, or from its middle for a lane that goes both ways.
            const float base = lane.min < 0.0f ? yOf(0.0f) : area.getBottom();
            const float y = yOf(v);
            g.fillRect(cell.withY(std::min(y, base)).withHeight(std::max(1.0f, std::abs(base - y))));
        }
    }
    // The length's handle: after the lane's last step.
    const float hx = area.getX() + static_cast<float>(lane.length) * w - gap * 0.5f;
    g.setColour(t.textSecondary);
    g.fillRect(juce::Rectangle<float>(hx - 1.0f, area.getY(), 2.0f, area.getHeight()));
}

void StepDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (steps_ == nullptr || !stepArea().contains(e.position))
        return;
    const auto lane = steps_->lane(lane_);
    const float handle = stepArea().getX() + static_cast<float>(lane.length) * stepArea().getWidth() /
                                                 static_cast<float>(maxSteps);
    draggingLength_ = std::abs(e.position.x - handle) <= 4.0f;
    drawing_ = true;
    steps_->beginDrawing(lane_, draggingLength_);
    if (!draggingLength_)
        edit(e.position);
}

void StepDisplay::mouseUp(const juce::MouseEvent&)
{
    if (steps_ != nullptr && std::exchange(drawing_, false))
        steps_->endDrawing();
}

void StepDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (steps_ == nullptr || !drawing_)
        return;
    if (draggingLength_)
    {
        const auto area = stepArea();
        steps_->setLength(lane_, std::clamp(static_cast<int>(std::lround((e.position.x - area.getX()) /
                                                                         area.getWidth() * maxSteps)),
                                            1, maxSteps));
        return;
    }
    edit(e.position);
}

void StepDisplay::edit(juce::Point<float> at)
{
    const auto area = stepArea();
    const auto lane = steps_->lane(lane_);
    const float v = lane.min + (area.getBottom() - at.y) / area.getHeight() * (lane.max - lane.min);
    steps_->setStep(lane_, stepAt(at.x), std::clamp(lane.pitched ? std::round(v) : v, lane.min, lane.max));
}

juce::String StepDisplay::getTooltip()
{
    if (steps_ == nullptr)
        return NoteDisplay::getTooltip();
    const auto lane = steps_->lane(lane_);
    const auto mouse = getMouseXYRelative().toFloat();
    if (!stepArea().contains(mouse))
        return NoteDisplay::getTooltip();
    const int step = stepAt(mouse.x);
    if (step >= lane.length || step >= static_cast<int>(lane.values.size()))
        return lane.name + ": " + juce::String(lane.length) +
               " steps; drag the line after the last to change it";
    const float v = lane.values[static_cast<std::size_t>(step)];
    return lane.name + ", step " + juce::String(step + 1) + ": " +
           (lane.text ? lane.text(v) : juce::String(v, 2)) + ". Drag across the steps to draw them";
}
} // namespace fd::ui
