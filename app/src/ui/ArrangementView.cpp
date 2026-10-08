#include "ui/ArrangementView.h"

namespace fd::app
{
namespace
{
bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;w64;aif;aiff;aifc;flac;ogg;mp3");
}

juce::Colour paletteColour(int index)
{
    const auto& colours = ui::Theme::current().clipColours;
    return colours.isEmpty() ? juce::Colours::grey : colours[index % colours.size()];
}
} // namespace

ArrangementView::ArrangementView(engine::Session& session) : session_(session)
{
    setSnapshot(session_.snapshot());
    setWantsKeyboardFocus(false);
}

void ArrangementView::setSnapshot(std::shared_ptr<const model::Snapshot> snapshot)
{
    snapshot_ = std::move(snapshot);
    syncLayout();
    if (selectedTrack_)
    {
        const bool exists = std::any_of(snapshot_->tracks.begin(), snapshot_->tracks.end(),
                                        [&](const auto& t) { return t.id == *selectedTrack_; });
        if (!exists)
            select(std::nullopt);
    }
    repaint();
}

void ArrangementView::syncLayout()
{
    layout_.setTrackCount(static_cast<int>(snapshot_->tracks.size()));
    layout_.setViewportHeight(laneArea().getHeight());
}

// ---- geometry ------------------------------------------------------------------------------

juce::Rectangle<int> ArrangementView::rulerArea() const
{
    return getLocalBounds().removeFromTop(rulerHeight).withTrimmedLeft(headerWidth);
}

juce::Rectangle<int> ArrangementView::headerArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight).removeFromLeft(headerWidth);
}

juce::Rectangle<int> ArrangementView::laneArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight).withTrimmedLeft(headerWidth);
}

float ArrangementView::laneX(double seconds) const
{
    return static_cast<float>(laneArea().getX() + timeline_.xForTime(seconds));
}

double ArrangementView::snapped(double seconds, bool bypass) const
{
    return snap_ && !bypass ? timeline_.snap(seconds, snapshot_->tempo) : std::max(0.0, seconds);
}

void ArrangementView::resized()
{
    const bool first = timeline_.width() <= 1.0 || timeline_.width() == 1000.0;
    timeline_.setWidth(laneArea().getWidth());
    if (first) // open on 32 bars
        timeline_.setView(0.0, laneArea().getWidth() / snapshot_->tempo.secondsForBeats(32.0 * snapshot_->tempo.beatsPerBar()));
    syncLayout();
}

// ---- per frame -----------------------------------------------------------------------------

void ArrangementView::frame(double dt, double playheadSeconds)
{
    if (timeline_.advance(dt) || timeline_.isAnimating())
        repaint();

    playhead_ = playheadSeconds;
    const float x = laneX(playhead_);
    if (std::abs(x - drawnPlayheadX_) >= 0.5f)
    {
        // Invalidate only the old and new playhead strips (the GL thread redraws them).
        for (float px : {drawnPlayheadX_, x})
            if (px >= static_cast<float>(laneArea().getX()) - 8.0f)
                repaint(juce::Rectangle<int>(static_cast<int>(px) - 6, 0, 13, getHeight()));
        drawnPlayheadX_ = x;
    }
}

void ArrangementView::zoomIn()
{
    const double px = timeline_.xForTime(playhead_);
    timeline_.animateZoomAround(px >= 0.0 && px <= timeline_.width() ? px : timeline_.width() * 0.5, 2.0);
    repaint();
}

void ArrangementView::zoomOut()
{
    const double px = timeline_.xForTime(playhead_);
    timeline_.animateZoomAround(px >= 0.0 && px <= timeline_.width() ? px : timeline_.width() * 0.5, 0.5);
    repaint();
}

void ArrangementView::zoomToFit()
{
    const double minimum = snapshot_->tempo.secondsForBeats(8.0 * snapshot_->tempo.beatsPerBar());
    timeline_.animateZoomTo(0.0, std::max(snapshot_->contentEnd(), minimum));
    repaint();
}

void ArrangementView::select(std::optional<std::uint64_t> track)
{
    if (track == selectedTrack_)
        return;
    selectedTrack_ = track;
    repaint();
    if (onSelectionChanged)
        onSelectionChanged(selectedTrack_);
}

// ---- painting ------------------------------------------------------------------------------

void ArrangementView::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);
    timeline_.gridLines(snapshot_->tempo, grid_);
    paintLanes(g);
    paintHeaders(g);
    paintRuler(g);

    // Corner above the headers.
    g.setColour(t.surface1);
    g.fillRect(getLocalBounds().removeFromTop(rulerHeight).removeFromLeft(headerWidth));
}

void ArrangementView::paintRuler(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = rulerArea();
    g.setColour(t.surface1);
    g.fillRect(area);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::Medium, true));
    for (const auto& line : grid_)
    {
        const float x = laneX(line.seconds);
        const float tick = line.kind == model::Timeline::LineKind::Bar ? 9.0f
                           : line.kind == model::Timeline::LineKind::Beat ? 5.0f
                                                                           : 3.0f;
        g.setColour(line.kind == model::Timeline::LineKind::Bar ? t.textTertiary : t.surface3);
        g.fillRect(x, static_cast<float>(area.getBottom()) - tick, 1.0f, tick);
        if (line.label)
        {
            g.setColour(t.textSecondary);
            g.drawText(juce::String(line.bar), juce::Rectangle<float>(x + 4.0f, static_cast<float>(area.getY()) + 3.0f, 48.0f, 14.0f),
                       juce::Justification::centredLeft, false);
        }
    }

    // Playhead marker.
    const float px = laneX(playhead_);
    juce::Path marker;
    marker.addTriangle(px - 5.0f, static_cast<float>(area.getBottom()) - 8.0f, px + 5.0f,
                       static_cast<float>(area.getBottom()) - 8.0f, px, static_cast<float>(area.getBottom()));
    g.setColour(t.textPrimary);
    g.fillPath(marker);
}

void ArrangementView::paintHeaders(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = headerArea();
    g.setColour(t.surface1);
    g.fillRect(area);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto& track = snapshot_->tracks[static_cast<size_t>(i)];
        auto row = juce::Rectangle<float>(static_cast<float>(area.getX()),
                                                static_cast<float>(area.getY() + layout_.top(i)),
                                                static_cast<float>(area.getWidth()), static_cast<float>(layout_.height(i)));
        if (row.getBottom() < static_cast<float>(area.getY()) || row.getY() > static_cast<float>(area.getBottom()))
            continue;
        const bool selected = selectedTrack_ == track.id;
        if (selected)
        {
            g.setColour(t.surface2);
            g.fillRect(row);
        }
        g.setColour(paletteColour(track.colour));
        g.fillRect(row.withWidth(3.0f).reduced(0.0f, 1.0f));
        g.setColour(t.surface0);
        g.fillRect(row.removeFromBottom(1.0f)); // separation by tone, one pixel

        auto text = row.reduced(12.0f, 6.0f);
        g.setColour(selected ? t.textPrimary : t.textPrimary.withAlpha(0.9f));
        g.setFont(ui::fonts::font(t.textSm, ui::fonts::Weight::Medium));
        g.drawText(juce::String::fromUTF8(track.name.c_str()), text.removeFromTop(16.0f), juce::Justification::centredLeft, true);
        if (text.getHeight() >= 12.0f)
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::fonts::font(t.textXs));
            g.drawText(track.kind == model::TrackInfo::Kind::Instrument ? "Instrument" : "Audio",
                       text.removeFromTop(14.0f), juce::Justification::centredLeft, false);
        }
    }
}

void ArrangementView::paintLanes(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = laneArea();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);

    // Lane backgrounds: close tone steps; the selected track a step lighter.
    const auto laneTone = t.surface0.interpolatedWith(t.surface1, 0.45f);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        const float h = static_cast<float>(layout_.height(i));
        if (y > static_cast<float>(area.getBottom()) || y + h < static_cast<float>(area.getY()))
            continue;
        const bool selected = selectedTrack_ == snapshot_->tracks[static_cast<size_t>(i)].id;
        g.setColour(selected ? t.surface1.interpolatedWith(t.surface2, 0.4f) : laneTone);
        g.fillRect(juce::Rectangle<float>(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), h - 1.0f));
    }

    // Grid: bar, beat and subdivision lines at falling contrast.
    const float bottom = static_cast<float>(area.getY() + std::min<double>(layout_.top(layout_.trackCount()), area.getHeight()));
    for (const auto& line : grid_)
    {
        switch (line.kind)
        {
        case model::Timeline::LineKind::Bar: g.setColour(t.surface3.brighter(0.15f)); break;
        case model::Timeline::LineKind::Beat: g.setColour(t.surface3); break;
        case model::Timeline::LineKind::Subdivision: g.setColour(t.surface3.withAlpha(0.5f)); break;
        }
        g.fillRect(laneX(line.seconds), static_cast<float>(area.getY()), 1.0f, bottom - static_cast<float>(area.getY()));
    }

    // Clips: a solid title strip with dark text over a dark body of the same hue.
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto& track = snapshot_->tracks[static_cast<size_t>(i)];
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        const float h = static_cast<float>(layout_.height(i));
        if (y > static_cast<float>(area.getBottom()) || y + h < static_cast<float>(area.getY()))
            continue;
        for (const auto& clip : track.clips)
        {
            if (clip.end() < timeline_.start() || clip.start > timeline_.end())
                continue;
            const float x0 = std::max(laneX(clip.start), static_cast<float>(area.getX()) - 2.0f);
            const float x1 = std::min(laneX(clip.end()), static_cast<float>(area.getRight()) + 2.0f);
            const auto box = juce::Rectangle<float>(x0, y + 1.0f, std::max(1.0f, x1 - x0), h - 3.0f);
            const auto colour = paletteColour(clip.colour);
            g.setColour(colour.withBrightness(0.32f));
            g.fillRoundedRectangle(box, 2.0f);
            const float strip = std::min(15.0f, box.getHeight() * 0.35f);
            auto title = box.withHeight(strip);
            g.setColour(colour);
            g.fillRoundedRectangle(title, 2.0f);
            g.fillRect(title.withTrimmedTop(strip * 0.5f));
            if (title.getWidth() > 16.0f && strip >= 10.0f)
            {
                g.setColour(t.surface0);
                g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::SemiBold));
                g.drawText(juce::String::fromUTF8(clip.name.c_str()), title.reduced(4.0f, 0.0f),
                           juce::Justification::centredLeft, true);
            }
        }
    }

    // Empty project hint.
    if (snapshot_->contentEnd() <= 0.0 && !dropTarget_)
    {
        g.setColour(t.textTertiary);
        g.setFont(ui::fonts::font(t.textSm));
        g.drawText("Drop audio files onto a track", area.withTrimmedTop(static_cast<int>(bottom) - area.getY() + 24).removeFromTop(24),
                   juce::Justification::centred, false);
    }

    // File drop preview: the target lane and an insertion line at the snapped time.
    if (dropTarget_)
    {
        const auto [index, seconds] = *dropTarget_;
        const float y = index >= 0 ? static_cast<float>(area.getY() + layout_.top(index)) : bottom;
        const float h = index >= 0 ? static_cast<float>(layout_.height(index)) : static_cast<float>(model::TrackLayout::defaultHeight);
        const float x = laneX(seconds);
        g.setColour(t.active.withAlpha(0.12f));
        g.fillRect(juce::Rectangle<float>(x, y, static_cast<float>(area.getRight()) - x, h - 1.0f));
        g.setColour(t.active);
        g.fillRect(x - 1.0f, y, 2.0f, h - 1.0f);
    }

    // Playhead.
    g.setColour(t.textPrimary);
    g.fillRect(laneX(playhead_), static_cast<float>(area.getY()), 1.0f, static_cast<float>(area.getHeight()));
}

// ---- input ---------------------------------------------------------------------------------

void ArrangementView::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    lastDrag_ = e.position;
    if (rulerArea().contains(pos))
    {
        drag_ = Drag::Ruler;
        rulerAnchorX_ = e.position.x - static_cast<float>(laneArea().getX());
        return;
    }
    drag_ = Drag::None;
    if (headerArea().contains(pos) || laneArea().contains(pos))
    {
        const auto hit = layout_.hit(pos.y - laneArea().getY());
        select(hit ? std::optional(snapshot_->tracks[static_cast<size_t>(hit->track)].id) : std::nullopt);
    }
}

void ArrangementView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ != Drag::Ruler)
        return;
    // One gesture: vertical zooms around the press point, horizontal scrolls.
    const auto delta = e.position - lastDrag_;
    lastDrag_ = e.position;
    if (delta.y != 0.0f)
        timeline_.zoomAround(rulerAnchorX_, std::exp(static_cast<double>(delta.y) * 0.012));
    if (delta.x != 0.0f)
    {
        timeline_.scrollBy(-static_cast<double>(delta.x));
        rulerAnchorX_ = e.position.x - static_cast<float>(laneArea().getX());
    }
    repaint();
}

void ArrangementView::mouseUp(const juce::MouseEvent& e)
{
    if (drag_ == Drag::Ruler && e.getDistanceFromDragStart() < 3)
    {
        // A click in the ruler moves the playhead (snapped; Alt for free).
        const double seconds = timeline_.timeForX(e.position.x - static_cast<float>(laneArea().getX()));
        session_.setPosition(snapped(seconds, e.mods.isAltDown()));
    }
    drag_ = Drag::None;
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const double x = e.position.x - static_cast<float>(laneArea().getX());
    const double dy = wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);
    const double dx = wheel.deltaX * (wheel.isReversed ? -1.0 : 1.0);
    if (e.mods.isCommandDown())
        timeline_.zoomAround(std::max(0.0, x), std::pow(2.0, dy * 2.0));
    else if (e.mods.isShiftDown())
        timeline_.scrollBy(-dy * 400.0);
    else
    {
        if (dx != 0.0)
            timeline_.scrollBy(-dx * 400.0);
        if (dy != 0.0)
            layout_.scrollBy(-dy * 400.0);
    }
    repaint();
}

void ArrangementView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    timeline_.zoomAround(std::max(0.0f, e.position.x - static_cast<float>(laneArea().getX())), scaleFactor);
    repaint();
}

// ---- file drops ----------------------------------------------------------------------------

bool ArrangementView::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const juce::String& f) { return isAudioFile(f); });
}

void ArrangementView::fileDragMove(const juce::StringArray&, int x, int y)
{
    const auto lanes = laneArea();
    const auto hit = layout_.hit(y - lanes.getY());
    const double seconds = snapped(timeline_.timeForX(std::max(0, x - lanes.getX())), juce::ModifierKeys::currentModifiers.isAltDown());
    dropTarget_ = std::pair{hit ? hit->track : -1, seconds};
    repaint();
}

void ArrangementView::fileDragExit(const juce::StringArray&)
{
    dropTarget_.reset();
    repaint();
}

void ArrangementView::filesDropped(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
    const auto [index, seconds] = *dropTarget_;
    dropTarget_.reset();

    // Several files go onto consecutive tracks, creating tracks below as needed.
    int target = index;
    std::optional<std::uint64_t> first;
    for (const auto& path : files)
    {
        if (!isAudioFile(path))
            continue;
        std::uint64_t trackId = 0;
        if (target >= 0 && target < static_cast<int>(snapshot_->tracks.size()))
            trackId = snapshot_->tracks[static_cast<size_t>(target)].id;
        else
            trackId = session_.addAudioTrack();
        if (session_.insertAudioFile(trackId, juce::File(path), seconds) != 0 && !first)
            first = trackId;
        if (target >= 0)
            ++target;
    }
    if (first)
        select(first);
    repaint();
}
} // namespace fd::app
