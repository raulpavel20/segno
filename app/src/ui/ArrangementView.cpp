#include "ui/ArrangementView.h"

#include "ui/time/TimeView.h"

#include <juce_opengl/juce_opengl.h>

namespace fd::app
{
namespace
{
bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;w64;aif;aiff;aifc;flac;mp3");
}

bool isMidiFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("mid;midi");
}

} // namespace

ArrangementView::ArrangementView(engine::Session& session) : session_(session)
{
    waveforms_.onReady = [this] { repaint(); };
    setSnapshot(session_.snapshot());
    setWantsKeyboardFocus(false);
}

void ArrangementView::setSnapshot(std::shared_ptr<const model::Snapshot> snapshot)
{
    snapshot_ = std::move(snapshot);
    preview_.clear();
    std::set<std::uint64_t> existing;
    for (const auto& t : snapshot_->tracks)
        for (const auto& c : t.clips)
        {
            waveforms_.request(c.sourcePath);
            if (selectedClips_.contains(c.id))
                existing.insert(c.id);
        }
    selectedClips_ = std::move(existing);
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
    return getLocalBounds().removeFromTop(rulerHeight()).withTrimmedLeft(headerWidth());
}

juce::Rectangle<int> ArrangementView::loopStripArea() const
{
    return rulerArea().removeFromTop(loopStripHeight());
}

ArrangementView::LoopZone ArrangementView::loopZoneAt(float x) const
{
    const auto [a, b] = loopPreview_.value_or(loop_);
    if (!(b > a))
        return LoopZone::None;
    const float x0 = laneX(a), x1 = laneX(b);
    const float edge = std::min(ui::Theme::current().input.edgeGrab, (x1 - x0) / 3.0f);
    if (std::abs(x - x0) <= edge)
        return LoopZone::Start;
    if (std::abs(x - x1) <= edge)
        return LoopZone::End;
    return x > x0 && x < x1 ? LoopZone::Body : LoopZone::None;
}

juce::Rectangle<int> ArrangementView::headerArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight()).removeFromLeft(headerWidth());
}

juce::Rectangle<int> ArrangementView::laneArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight()).withTrimmedLeft(headerWidth());
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

    // Waveforms render at the exact zoom once it has settled (WaveformTiles).
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (timeline_.pixelsPerSecond() != lastPixelsPerSecond_)
    {
        lastPixelsPerSecond_ = timeline_.pixelsPerSecond();
        lastZoomChange_ = now;
        zoomSettled_ = false;
    }
    else if (!zoomSettled_ && now - lastZoomChange_ > ui::Theme::current().motion.settle)
    {
        zoomSettled_ = true;
        repaint();
    }
    if (tiles_.wantsRepaint())
        repaint();

    // Loop brace state (live from the transport, like the playhead).
    const auto loop = session_.loopRange();
    const bool looping = session_.isLooping();
    if (loop != loop_ || looping != looping_)
    {
        loop_ = loop;
        looping_ = looping;
        repaint(rulerArea());
    }

    // Follow: when the playhead nears the right edge (or is off screen), page the view so it
    // sits near the left again, eased over a few frames.
    if (follow_ && session_.isPlaying() && drag_ == Drag::None && !timeline_.isAnimating())
    {
        const double px = timeline_.xForTime(playheadSeconds);
        if (!scrollTarget_ && (px > timeline_.width() * 0.88 || px < 0.0))
            scrollTarget_ = std::max(0.0, playheadSeconds - timeline_.width() * 0.12 / timeline_.pixelsPerSecond());
    }
    if (scrollTarget_)
    {
        const double k = 1.0 - std::exp(-dt / ui::Theme::current().motion.follow);
        double start = timeline_.start() + (*scrollTarget_ - timeline_.start()) * k;
        if (std::abs(*scrollTarget_ - start) * timeline_.pixelsPerSecond() < 0.5)
        {
            start = *scrollTarget_;
            scrollTarget_.reset();
        }
        timeline_.setView(start, timeline_.pixelsPerSecond());
        repaint();
    }

    playhead_ = playheadSeconds;
    playheadTracker_.update(*this, timeline_, laneArea(), playhead_);
}

void ArrangementView::zoomAt(double laneXPos, double factor)
{
    timeline_.zoomAround(laneXPos, factor);
    repaint();
}

void ArrangementView::setView(double startSeconds, double pixelsPerSecond)
{
    timeline_.setView(startSeconds, pixelsPerSecond);
    repaint();
}

void ArrangementView::setAllTrackHeights(double height)
{
    for (int i = 0; i < layout_.trackCount(); ++i)
        layout_.setHeight(i, height);
    repaint();
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
    const double start = juce::Time::getMillisecondCounterHiRes();
    tiles_.beginPaint();
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);
    timeline_.gridLines(snapshot_->tempo, grid_);
    paintLanes(g);
    paintHeaders(g);
    paintRuler(g);

    // Corner above the headers.
    g.setColour(t.surface1);
    g.fillRect(getLocalBounds().removeFromTop(rulerHeight()).removeFromLeft(headerWidth()));
    lastPaintMs_ = juce::Time::getMillisecondCounterHiRes() - start;
}

void ArrangementView::paintRuler(juce::Graphics& g)
{
    auto area = rulerArea();
    time::paintLoopStrip(g, timeline_, area.removeFromTop(loopStripHeight()), loopPreview_.value_or(loop_), looping_);
    time::paintRuler(g, grid_, timeline_, area, {.playhead = playhead_});
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
        g.setColour(ui::Theme::current().clipColour(track.colour));
        g.fillRect(row.withWidth(3.0f).reduced(0.0f, 1.0f));
        g.setColour(t.line);
        g.fillRect(row.removeFromBottom(1.0f)); // the track separator, continued across the lanes

        auto text = row.reduced(12.0f, 6.0f);
        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::Name));
        g.drawText(juce::String::fromUTF8(track.name.c_str()), text.removeFromTop(16.0f), juce::Justification::centredLeft, true);
        if (text.getHeight() >= 12.0f)
        {
            // One kind of track (ADR 0019): show the instrument, or that MIDI has nothing to play it.
            const bool hasMidi = std::any_of(track.clips.begin(), track.clips.end(),
                                             [](const auto& c) { return c.kind == model::ClipInfo::Kind::Midi; });
            const auto label = !track.instrument.empty() ? juce::String::fromUTF8(track.instrument.c_str())
                               : hasMidi                 ? juce::String("No instrument")
                                                         : juce::String();
            g.setColour(track.instrument.empty() && hasMidi ? t.roles.warning : t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(label, text.removeFromTop(14.0f), juce::Justification::centredLeft, true);
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
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        const float h = static_cast<float>(layout_.height(i));
        if (y > static_cast<float>(area.getBottom()) || y + h < static_cast<float>(area.getY()))
            continue;
        const bool selected = selectedTrack_ == snapshot_->tracks[static_cast<size_t>(i)].id;
        g.setColour(selected ? t.roles.laneSelected : t.roles.lane);
        g.fillRect(juce::Rectangle<float>(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), h));
    }

    // Grid: bar, beat and subdivision lines at falling contrast, all quieter than the track
    // separators (the strongest lines in the arrangement).
    const float bottom = static_cast<float>(area.getY() + std::min<double>(layout_.top(layout_.trackCount()), area.getHeight()));
    time::paintGrid(g, grid_, timeline_, area.withBottom(static_cast<int>(bottom)));

    // Track separators: one pixel of `line`, a step lighter than the lanes, across the width.
    g.setColour(t.line);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const float y = static_cast<float>(area.getY() + layout_.top(i) + layout_.height(i)) - 1.0f;
        if (y >= static_cast<float>(area.getY()) && y <= static_cast<float>(area.getBottom()))
            g.fillRect(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), 1.0f);
    }

    // Clips: a solid title strip with dark text over a dark body of the same hue. Only what
    // intersects the invalidated area is drawn (a playhead-only repaint touches a few columns).
    // Clips being dragged are drawn at their new place instead.
    const auto visible = area.getIntersection(g.getClipBounds());
    std::set<std::uint64_t> moving;
    for (const auto& e : preview_)
        moving.insert(e.clip);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto& track = snapshot_->tracks[static_cast<size_t>(i)];
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        const float h = static_cast<float>(layout_.height(i));
        if (y > static_cast<float>(area.getBottom()) || y + h < static_cast<float>(area.getY()))
            continue;
        for (const auto& clip : track.clips)
            if (!moving.contains(clip.id))
                paintClip(g, clip, y, h, area, visible, selectedClips_.contains(clip.id));
    }
    for (const auto& e : preview_)
    {
        const auto found = model::findClip(*snapshot_, e.clip);
        int index = 0;
        while (index < layout_.trackCount() && snapshot_->tracks[static_cast<size_t>(index)].id != e.track)
            ++index;
        if (!found || index >= layout_.trackCount())
            continue;
        auto clip = *found->first;
        clip.start = e.start;
        clip.length = e.length;
        clip.offset = e.offset;
        paintClip(g, clip, static_cast<float>(area.getY() + layout_.top(index)), static_cast<float>(layout_.height(index)),
                  area, visible, true);
    }

    // Box selection.
    if (drag_ == Drag::Box)
        time::paintBoxSelection(g, juce::Rectangle<float>(dragStart_, lastDrag_));

    // Empty project hint.
    if (snapshot_->contentEnd() <= 0.0 && !dropTarget_)
    {
        ui::paintEmptyState(g, area.withTop(static_cast<int>(bottom)).removeFromTop(t.shell.listRowHeight * 3),
                            "Drop audio files onto a track, or double-click one to make a MIDI clip");
    }

    // File drop preview: the target lane and an insertion line at the snapped time.
    if (dropTarget_)
    {
        const auto [index, seconds] = *dropTarget_;
        const float y = index >= 0 ? static_cast<float>(area.getY() + layout_.top(index)) : bottom;
        const float h = index >= 0 ? static_cast<float>(layout_.height(index)) : static_cast<float>(model::TrackLayout::defaultHeight);
        const float x = laneX(seconds);
        g.setColour(t.roles.dropTarget);
        g.fillRect(juce::Rectangle<float>(x, y, static_cast<float>(area.getRight()) - x, h - 1.0f));
        g.setColour(t.active);
        g.fillRect(x - 1.0f, y, 2.0f, h - 1.0f);
    }

    time::paintPlayhead(g, timeline_, area, playhead_);
}

void ArrangementView::paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h,
                                juce::Rectangle<int> area, juce::Rectangle<int> visible, bool selected)
{
    const auto& t = ui::Theme::current();
    if (clip.end() < timeline_.start() || clip.start > timeline_.end())
        return;
    const float x0 = std::max(laneX(clip.start), static_cast<float>(area.getX()) - 2.0f);
    const float x1 = std::min(laneX(clip.end()), static_cast<float>(area.getRight()) + 2.0f);
    const auto box = juce::Rectangle<float>(x0, y + 1.0f, std::max(1.0f, x1 - x0), h - 3.0f);
    if (!box.intersects(visible.toFloat()))
        return;
    const auto colour = ui::Theme::current().clipColour(clip.colour);
    g.setColour(t.clipBody(colour, selected));
    g.fillRect(box);
    const float strip = std::min(15.0f, box.getHeight() * 0.35f);
    const auto title = box.withHeight(strip);
    g.setColour(colour);
    g.fillRect(title);
    if (clip.kind == model::ClipInfo::Kind::Midi)
        paintNotes(g, clip, box.withTrimmedTop(strip).reduced(0.0f, 2.0f), colour);
    else
        paintWaveform(g, clip, box.withTrimmedTop(strip), visible, colour);
    if (title.getWidth() > 16.0f && strip >= 10.0f)
    {
        g.setColour(t.surface0);
        g.setFont(ui::font(ui::TextStyle::Control));
        g.drawText(juce::String::fromUTF8(clip.name.c_str()), title.reduced(4.0f, 0.0f), juce::Justification::centredLeft,
                   true);
    }
    if (selected)
    {
        g.setColour(t.textPrimary);
        g.drawRect(box, 1.0f);
    }
}

void ArrangementView::paintNotes(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                                 juce::Colour colour)
{
    // A preview of the notes across the clip's own pitch range (the piano roll edits them).
    if (clip.notes.empty() || body.getHeight() < 4.0f)
        return;
    int lo = 127, hi = 0;
    for (const auto& n : clip.notes)
    {
        lo = std::min(lo, n.pitch);
        hi = std::max(hi, n.pitch);
    }
    const int span = std::max(12, hi - lo + 1);
    lo = std::max(0, (lo + hi) / 2 - span / 2);
    const float row = body.getHeight() / static_cast<float>(span);
    g.setColour(ui::Theme::current().clipContent(colour));
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(body.getSmallestIntegerContainer());
    const double contentStart = clip.start - clip.offset; // follows the clip while it is dragged
    const auto& tempo = snapshot_->tempo;
    for (const auto& n : clip.notes)
    {
        const float x0 = laneX(contentStart + tempo.secondsForBeats(n.start));
        const float x1 = laneX(contentStart + tempo.secondsForBeats(n.start + n.length));
        const float y = body.getBottom() - static_cast<float>(n.pitch - lo + 1) * row;
        g.fillRect(juce::Rectangle<float>(x0, y, std::max(1.0f, x1 - x0 - 1.0f), std::max(1.0f, row - 1.0f)));
    }
}

void ArrangementView::paintWaveform(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                                    juce::Rectangle<int> visible, juce::Colour colour)
{
    const auto* peaks = waveforms_.find(clip.sourcePath);
    if (peaks == nullptr)
        return;
    const auto tint = ui::Theme::current().clipContent(colour);
    // Tiles pay off on the GPU; the software renderer scales images slowly (measured).
    if (juce::OpenGLContext::getCurrentContext() != nullptr)
        tiles_.draw(g, *peaks, clip, body, visible, timeline_, laneArea().getX(), tint, zoomSettled_);
    else
        WaveformTiles::drawDirect(g, *peaks, clip, body, visible, timeline_, laneArea().getX(), tint);
}

// ---- input ---------------------------------------------------------------------------------

std::function<double(double)> ArrangementView::snapFunction(bool bypass) const
{
    if (!snap_ || bypass)
        return [](double t) { return std::max(0.0, t); };
    return [this](double t) { return timeline_.snap(t, snapshot_->tempo); };
}

void ArrangementView::mouseMove(const juce::MouseEvent& e)
{
    const auto lanes = laneArea();
    auto cursor = juce::MouseCursor::NormalCursor;
    if (loopStripArea().contains(e.getPosition()))
    {
        const auto zone = loopZoneAt(e.position.x);
        cursor = zone == LoopZone::Start || zone == LoopZone::End ? juce::MouseCursor::LeftRightResizeCursor
                                                                  : juce::MouseCursor::NormalCursor;
    }
    else if (lanes.contains(e.getPosition()))
        if (const auto hit = model::hitClip(*snapshot_, layout_, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                                            e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab))
            cursor = hit->zone == model::ClipHit::Zone::Body ? juce::MouseCursor::NormalCursor
                                                              : juce::MouseCursor::LeftRightResizeCursor;
    setMouseCursor(cursor);
}

void ArrangementView::mouseDown(const juce::MouseEvent& e)
{
    if (onActivated)
        onActivated();
    const auto pos = e.getPosition();
    const auto lanes = laneArea();
    lastDrag_ = dragStart_ = e.position;
    dragMoved_ = false;
    preview_.clear();
    if (loopStripArea().contains(pos))
    {
        drag_ = Drag::Loop;
        loopDrag_ = loopZoneAt(e.position.x);
        loopAtDragStart_ = loop_;
        return;
    }
    if (rulerArea().contains(pos))
    {
        drag_ = Drag::Ruler;
        rulerGesture_.begin(e.position.x - static_cast<float>(lanes.getX()));
        return;
    }
    drag_ = Drag::None;
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();
    if (headerArea().contains(pos))
    {
        const auto row = layout_.hit(pos.y - lanes.getY());
        select(row ? std::optional(snapshot_->tracks[static_cast<size_t>(row->track)].id) : std::nullopt);
        return;
    }
    if (!lanes.contains(pos))
        return;

    const auto row = layout_.hit(pos.y - lanes.getY());
    if (row)
        select(snapshot_->tracks[static_cast<size_t>(row->track)].id);
    const auto hit = model::hitClip(*snapshot_, layout_, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                                    e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab);
    if (e.getNumberOfClicks() >= 2 && row)
    {
        // Double-click: open a clip in the clip view, or make a MIDI clip on an empty spot.
        drag_ = Drag::None;
        if (hit && onOpenClip)
            onOpenClip(hit->clip);
        else if (!hit)
            createMidiClip(row->track, timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX())), e.mods.isAltDown());
        return;
    }
    clickedInSelection_.reset();
    if (hit)
    {
        if (!additive && selectedClips_.size() > 1 && selectedClips_.contains(hit->clip))
            clickedInSelection_ = hit->clip; // stays a group for a drag; just this clip for a click
        auto selection = selectedClips_;
        if (additive && selection.contains(hit->clip))
            selection.erase(hit->clip);
        else if (additive)
            selection.insert(hit->clip);
        else if (!selection.contains(hit->clip))
            selection = {hit->clip};
        setClipSelection(std::move(selection));
        if (!selectedClips_.contains(hit->clip))
            return; // Shift-click removed it
        drag_ = Drag::Clip;
        dragStartTrack_ = hit->track;
        clipDrag_ = {.kind = hit->zone == model::ClipHit::Zone::StartEdge ? model::DragKind::TrimStart
                             : hit->zone == model::ClipHit::Zone::EndEdge  ? model::DragKind::TrimEnd
                                                                           : model::DragKind::Move,
                     .anchor = hit->clip};
        return;
    }
    // Empty lane: box selection (Shift adds to the selection).
    drag_ = Drag::Box;
    boxBase_ = additive ? selectedClips_ : std::set<std::uint64_t>{};
    if (!additive)
        setClipSelection({});
}

void ArrangementView::mouseDrag(const juce::MouseEvent& e)
{
    const auto lanes = laneArea();
    if (drag_ == Drag::Ruler)
    {
        rulerGesture_.drag(timeline_, e.position - lastDrag_, e.position.x - static_cast<float>(lanes.getX()));
        lastDrag_ = e.position;
        repaint();
        return;
    }
    lastDrag_ = e.position;
    if (!dragMoved_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        return; // a click, not a drag
    dragMoved_ = true;

    if (drag_ == Drag::Loop)
    {
        const auto snapFn = snapFunction(e.mods.isAltDown());
        const auto lx = static_cast<float>(lanes.getX());
        const double now = timeline_.timeForX(e.position.x - lx);
        const double then = timeline_.timeForX(dragStart_.x - lx);
        auto [a, b] = loopAtDragStart_;
        switch (loopDrag_)
        {
        case LoopZone::None: // draw a new brace
            a = snapFn(std::min(then, now));
            b = snapFn(std::max(then, now));
            break;
        case LoopZone::Body:
        {
            const double moved = snapFn(a + (now - then)) - a;
            a = std::max(0.0, a + moved);
            b = a + (loopAtDragStart_.second - loopAtDragStart_.first);
            break;
        }
        case LoopZone::Start: a = std::min(snapFn(now), b - 0.01); break;
        case LoopZone::End: b = std::max(snapFn(now), a + 0.01); break;
        }
        loopPreview_ = std::pair{a, b};
        repaint(rulerArea());
        return;
    }

    if (drag_ == Drag::Clip)
    {
        clipDrag_.deltaSeconds = (e.position.x - dragStart_.x) / timeline_.pixelsPerSecond();
        if (clipDrag_.kind == model::DragKind::Move)
            if (const auto row = layout_.hit(e.position.y - static_cast<float>(lanes.getY())))
                clipDrag_.deltaTracks = row->track - dragStartTrack_;
        preview_ = model::dragClips(*snapshot_, selectedClips_, clipDrag_, snapFunction(e.mods.isAltDown()));
        repaint();
    }
    else if (drag_ == Drag::Box)
    {
        const double t0 = timeline_.timeForX(dragStart_.x - static_cast<float>(lanes.getX()));
        const double t1 = timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX()));
        auto trackAt = [&](float y) {
            const auto row = layout_.hit(y - static_cast<float>(lanes.getY()));
            return row ? row->track : (y < static_cast<float>(lanes.getY()) ? 0 : layout_.trackCount() - 1);
        };
        auto selection = boxBase_;
        for (auto id : model::clipsInBox(*snapshot_, t0, t1, trackAt(dragStart_.y), trackAt(e.position.y)))
            selection.insert(id);
        setClipSelection(std::move(selection));
        repaint();
    }
}

void ArrangementView::mouseUp(const juce::MouseEvent& e)
{
    if (drag_ == Drag::Ruler && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
    {
        // A click in the ruler moves the playhead (snapped; Alt for free).
        session_.setPosition(time::rulerTime(timeline_, snapshot_->tempo, e.position.x - static_cast<float>(laneArea().getX()),
                                             !snap_ || e.mods.isAltDown()));
    }
    if (drag_ == Drag::Loop)
    {
        if (dragMoved_ && loopPreview_ && loopPreview_->second > loopPreview_->first)
        {
            session_.setLoopRange(loopPreview_->first, loopPreview_->second);
            if (loopDrag_ == LoopZone::None)
                session_.setLooping(true); // drawing a new brace turns looping on
        }
        else if (!dragMoved_ && loopDrag_ != LoopZone::None)
            session_.setLooping(!session_.isLooping()); // a click on the brace toggles it
        loopPreview_.reset();
    }
    if (drag_ == Drag::Clip && dragMoved_ && !preview_.empty())
        session_.applyClipEdits(preview_); // one undo step; the snapshot follows
    if (drag_ == Drag::Clip && !dragMoved_ && clickedInSelection_)
        setClipSelection({*clickedInSelection_});
    clickedInSelection_.reset();
    // Keep drawing the preview until the new snapshot arrives (no flicker back).
    if (drag_ != Drag::Clip || !dragMoved_)
        preview_.clear();
    drag_ = Drag::None;
    repaint();
}

// ---- edit commands -------------------------------------------------------------------------

void ArrangementView::setClipSelection(std::set<std::uint64_t> clips)
{
    if (clips == selectedClips_)
        return;
    selectedClips_ = std::move(clips);
    repaint();
    if (onSelectionChanged)
        onSelectionChanged(selectedTrack_);
}

void ArrangementView::deleteSelection()
{
    session_.deleteClips({selectedClips_.begin(), selectedClips_.end()});
    setClipSelection({});
}

void ArrangementView::duplicateSelection()
{
    const auto created = session_.duplicateClips(model::duplicatePlacements(*snapshot_, selectedClips_));
    setClipSelection({created.begin(), created.end()});
}

void ArrangementView::splitAtPlayhead()
{
    std::vector<std::uint64_t> targets(selectedClips_.begin(), selectedClips_.end());
    if (targets.empty() && selectedTrack_)
        for (const auto& t : snapshot_->tracks)
            if (t.id == *selectedTrack_)
                for (const auto& c : t.clips)
                    targets.push_back(c.id);
    const auto right = session_.splitClips(targets, playhead_);
    if (!selectedClips_.empty())
    {
        auto selection = selectedClips_;
        selection.insert(right.begin(), right.end());
        setClipSelection(std::move(selection));
    }
}

std::uint64_t ArrangementView::createMidiClip(int trackIndex, double seconds, bool bypassSnap)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(snapshot_->tracks.size()))
        return 0;
    const auto& track = snapshot_->tracks[static_cast<size_t>(trackIndex)];
    const auto& tempo = snapshot_->tempo;
    double start = std::max(0.0, seconds);
    if (snap_ && !bypassSnap)
    {
        const double step = tempo.secondsForBeats(timeline_.gridStepBeats(tempo));
        start = std::floor(start / step + 1e-9) * step;
    }
    // Never over another clip (ADR 0019 would cut it): start after the one it lands in, end
    // before the next.
    for (const auto& c : track.clips) // sorted by start
        if (c.start <= start + 1e-9 && c.end() > start)
            start = c.end();
    double end = start + tempo.secondsForBeats(tempo.beatsPerBar());
    for (const auto& c : track.clips)
        if (c.start > start - 1e-9 && c.start < end)
            end = c.start;
    if (end - start < model::minClipLength)
        return 0;
    bool addedInstrument = false;
    const auto id = session_.createMidiClip(track.id, start, end - start, &addedInstrument);
    if (id == 0)
        return 0;
    select(track.id);
    setClipSelection({id});
    if (addedInstrument && onMessage)
        onMessage("Added a Subtractive Synth to play the MIDI clip (swap it in the module chain)");
    if (onOpenClip)
        onOpenClip(id);
    return id;
}

std::uint64_t ArrangementView::createMidiClipAtPlayhead()
{
    int index = 0;
    if (selectedTrack_)
        for (size_t i = 0; i < snapshot_->tracks.size(); ++i)
            if (snapshot_->tracks[i].id == *selectedTrack_)
                index = static_cast<int>(i);
    return createMidiClip(index, playhead_);
}

void ArrangementView::selectAllClips()
{
    std::set<std::uint64_t> all;
    for (const auto& t : snapshot_->tracks)
        for (const auto& c : t.clips)
            all.insert(c.id);
    setClipSelection(std::move(all));
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    time::applyWheel(timeline_, e.position.x - static_cast<float>(laneArea().getX()), e.mods, wheel,
                     [this](double pixels) { layout_.scrollBy(pixels); });
    repaint();
}

void ArrangementView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    time::applyMagnify(timeline_, e.position.x - static_cast<float>(laneArea().getX()), scaleFactor);
    repaint();
}

// ---- file drops ----------------------------------------------------------------------------

bool ArrangementView::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const juce::String& f) { return isAudioFile(f) || isMidiFile(f); });
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
        const bool midi = isMidiFile(path);
        if (!isAudioFile(path) && !midi)
            continue;
        std::uint64_t trackId = 0;
        if (target >= 0 && target < static_cast<int>(snapshot_->tracks.size()))
            trackId = snapshot_->tracks[static_cast<size_t>(target)].id;
        else
            trackId = session_.addTrack(); // a MIDI drop brings its instrument (ADR 0019)
        bool addedInstrument = false;
        const auto clip = midi ? session_.insertMidiFile(trackId, juce::File(path), seconds, &addedInstrument)
                               : session_.insertAudioFile(trackId, juce::File(path), seconds);
        if (addedInstrument && onMessage)
            onMessage("Added a Subtractive Synth to play the MIDI clip (swap it in the module chain)");
        if (clip != 0 && !first)
            first = trackId;
        if (target >= 0)
            ++target;
    }
    if (first)
        select(first);
    repaint();
}
} // namespace fd::app
