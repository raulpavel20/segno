#include "ui/ArrangementView.h"

#include "model/TrackHeaderLayout.h"
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

/// A lane's name sits this far right of its track's, so lanes read as their track's children.
constexpr float kLaneIndent = 12.0f;
/// How close to a row's bottom edge (in the header column) a drag resizes the row.
constexpr float kResizeGrab = 3.0f;
} // namespace

ArrangementView::HeaderControls::HeaderControls(engine::Session& session, const model::TrackInfo& channel)
    : mute(session, ChannelToggle::Kind::Mute, channel.id), solo(session, ChannelToggle::Kind::Solo, channel.id),
      volume(session, channel.id), pan(session, channel.id), meter(session, channel.id)
{
    // Header bars are narrow: values only; the tooltips name them.
    volume.setName({});
    pan.setName({});
}

ArrangementView::ArrangementView(engine::Session& session) : session_(session)
{
    headerLayer_.setInterceptsMouseClicks(false, true); // the header's background still selects
    addAndMakeVisible(headerLayer_);
    waveforms_.onReady = [this] { repaint(); };
    setSnapshot(session_.snapshot());
    setWantsKeyboardFocus(false);
}

void ArrangementView::setSnapshot(std::shared_ptr<const model::Snapshot> snapshot)
{
    snapshot_ = std::move(snapshot);
    preview_.clear();
    lanePreview_.reset();
    hoveredPoint_.reset();
    for (const auto& t : snapshot_->tracks)
        for (const auto& c : t.clips)
            waveforms_.request(c.sourcePath);
    // Drop what's gone. The owner reads the selection after a new snapshot, so this tells no one.
    selection_ = notified_ = model::pruned(selection_, *snapshot_);
    syncRows();
}

void ArrangementView::syncRows()
{
    syncLayout();

    // Mixer controls for every channel row, lane controls for every lane row: made for new rows,
    // dropped for gone ones.
    std::set<std::uint64_t> shown;
    std::set<std::string> lanes;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        const auto* channel = rowChannel(i);
        if (channel == nullptr)
            continue;
        if (const auto* lane = rowLane(i))
        {
            const auto key = rows_[static_cast<size_t>(i)].key();
            lanes.insert(key);
            auto& controls = laneControls_[key];
            if (controls == nullptr)
            {
                controls = std::make_unique<LaneControls>(session_, laneTarget(i));
                headerLayer_.addChildComponent(controls->on);
                headerLayer_.addChildComponent(controls->bar);
            }
            controls->update(*lane);
            const auto value = session_.laneValue(laneTarget(i)); // the header's readout
            laneTexts_[key] = value ? session_.laneText(laneTarget(i), *value) : std::string();
            continue;
        }
        shown.insert(channel->id);
        auto& controls = headerControls_[channel->id];
        if (controls == nullptr)
        {
            controls = std::make_unique<HeaderControls>(session_, *channel);
            if (channel->role != model::TrackInfo::Role::Master) // the master has no mute or solo
            {
                headerLayer_.addChildComponent(controls->mute);
                headerLayer_.addChildComponent(controls->solo);
            }
            headerLayer_.addChildComponent(controls->volume);
            headerLayer_.addChildComponent(controls->pan);
            headerLayer_.addChildComponent(controls->meter);
        }
        controls->mute.update(channel->mix.mute);
        controls->solo.update(channel->mix.solo);
        controls->volume.update(*channel);
        controls->pan.update(*channel);
    }
    for (auto it = headerControls_.begin(); it != headerControls_.end();)
        it = shown.contains(it->first) ? std::next(it) : headerControls_.erase(it);
    for (auto it = laneControls_.begin(); it != laneControls_.end();)
        it = lanes.contains(it->first) ? std::next(it) : laneControls_.erase(it);
    placeHeaderControls();
    repaint();
}

const model::TrackInfo* ArrangementView::rowChannel(int row) const noexcept
{
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return nullptr;
    return snapshot_->channel(rows_[static_cast<size_t>(row)].channel);
}

const model::LaneInfo* ArrangementView::rowLane(int row) const noexcept
{
    if (row < 0 || row >= static_cast<int>(rows_.size()) || !rows_[static_cast<size_t>(row)].isLane())
        return nullptr;
    const auto* channel = rowChannel(row);
    return channel != nullptr ? channel->lane(rows_[static_cast<size_t>(row)].lane) : nullptr;
}

engine::Session::LaneTarget ArrangementView::laneTarget(int row) const
{
    const auto& r = rows_[static_cast<size_t>(row)];
    return {r.channel, r.lane};
}

juce::Rectangle<float> ArrangementView::rowBounds(int row, juce::Rectangle<int> column) const
{
    return {static_cast<float>(column.getX()), static_cast<float>(column.getY() + layout_.top(row)),
            static_cast<float>(column.getWidth()), static_cast<float>(layout_.height(row))};
}

LaneGeometry ArrangementView::laneGeometry(int row) const
{
    const auto lanes = laneArea();
    return {.area = rowBounds(row, lanes).withTrimmedBottom(1.0f), .laneX0 = static_cast<float>(lanes.getX()),
            .timeline = &timeline_};
}

juce::Rectangle<float> ArrangementView::chevronArea(int row) const
{
    const auto* channel = rowChannel(row);
    if (channel == nullptr || rowLane(row) != nullptr || channel->lanes.empty())
        return {};
    const auto r = rowBounds(row, headerArea());
    const auto slots = model::layoutTrackHeader(r.getWidth(), r.getHeight(), 0, false);
    // In the left padding, beside the name (which doesn't move).
    return juce::Rectangle<float>(3.0f, static_cast<float>(slots.name.y), 9.0f, static_cast<float>(slots.name.h))
        .translated(r.getX(), r.getY());
}

void ArrangementView::setMixRowsVisible(bool visible)
{
    if (visible == showMixRows_)
        return;
    showMixRows_ = visible;
    if (!visible && selection_.channel)
        if (const auto* channel = snapshot_->channel(*selection_.channel); channel != nullptr && !channel->holdsClips())
            selectTrack(std::nullopt);
    syncRows(); // not setSnapshot: pruning against this snapshot could drop a clip made since
}

void ArrangementView::syncLayout()
{
    rows_ = model::arrangementRows(*snapshot_, showMixRows_);
    layout_.setTrackCount(static_cast<int>(rows_.size()));
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        const auto& row = rows_[static_cast<size_t>(i)];
        const auto it = heights_.find(row.key());
        layout_.setHeight(i, it != heights_.end() ? it->second
                             : row.isLane()    ? model::kLaneDefaultHeight
                                               : model::TrackLayout::defaultHeight);
    }
    layout_.setViewportHeight(laneArea().getHeight());
    headerLayer_.setBounds(headerArea());
}

void ArrangementView::setRowHeights(std::map<std::string, double> heights)
{
    heights_ = std::move(heights);
    syncLayout();
    placeHeaderControls();
    repaint();
}

void ArrangementView::placeHeaderControls()
{
    const auto area = headerArea();
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto* channel = rowChannel(i);
        if (channel == nullptr)
            continue;
        const double top = layout_.top(i), height = layout_.height(i);
        const bool visible = top + height > 0.0 && top < area.getHeight();
        auto at = [top](const model::HeaderRect& r, float indent = 0.0f) {
            return juce::Rectangle<double>(r.x + indent, r.y + top, std::max(0.0, r.w - indent), r.h).toNearestInt();
        };
        auto show = [visible](juce::Component& comp, const model::HeaderRect& r, juce::Rectangle<int> bounds) {
            comp.setVisible(visible && !r.isEmpty());
            if (comp.isVisible())
                comp.setBounds(bounds);
        };
        const model::HeaderRect none{};
        if (rowLane(i) != nullptr)
        {
            const auto it = laneControls_.find(rows_[static_cast<size_t>(i)].key());
            if (it == laneControls_.end())
                continue;
            const auto slots = model::layoutTrackHeader(area.getWidth(), height, 1, false);
            // The on switch only means something with points to play.
            const bool playable = !rowLane(i)->points.empty();
            show(it->second->on, slots.buttons.empty() || !playable ? none : slots.buttons[0],
                 slots.buttons.empty() ? juce::Rectangle<int>() : at(slots.buttons[0]));
            show(it->second->bar, slots.controls, at(slots.controls, kLaneIndent));
            if (it->second->bar.isVisible())
                it->second->bar.refresh(); // follows the played value
            continue;
        }
        auto it = headerControls_.find(channel->id);
        if (it == headerControls_.end())
            continue;
        auto& c = *it->second;
        const bool master = channel->role == model::TrackInfo::Role::Master;
        const auto slots = model::layoutTrackHeader(area.getWidth(), height, master ? 0 : 2, true);
        show(c.mute, slots.buttons.size() > 0 ? slots.buttons[0] : none, slots.buttons.size() > 0 ? at(slots.buttons[0]) : juce::Rectangle<int>());
        show(c.solo, slots.buttons.size() > 1 ? slots.buttons[1] : none, slots.buttons.size() > 1 ? at(slots.buttons[1]) : juce::Rectangle<int>());
        auto controls = at(slots.controls);
        const int gap = ui::Theme::current().shell.itemGap;
        show(c.volume, slots.controls, controls.removeFromLeft((controls.getWidth() - gap) / 2));
        show(c.pan, slots.controls, controls.withTrimmedLeft(gap));
        show(c.meter, slots.meter, at(slots.meter));
    }
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

double ArrangementView::gridStepSeconds() const
{
    return snapshot_->tempo.secondsForBeats(timeline_.gridStepBeats(snapshot_->tempo));
}

void ArrangementView::resized()
{
    const bool first = timeline_.width() <= 1.0 || timeline_.width() == 1000.0;
    timeline_.setWidth(laneArea().getWidth());
    if (first) // open on 32 bars
        timeline_.setView(0.0, laneArea().getWidth() / snapshot_->tempo.secondsForBeats(32.0 * snapshot_->tempo.beatsPerBar()));
    syncLayout();
    placeHeaderControls();
}

// ---- per frame -----------------------------------------------------------------------------

void ArrangementView::frame(double dt, double playheadSeconds)
{
    placeHeaderControls(); // follows scrolling and row heights (moves only what changed)
    if (timeline_.advance(dt) || timeline_.isAnimating())
        repaint();

    // Lane headers show their parameter's value now (it moves while automation plays).
    const auto header = headerArea();
    for (int i = 0; i < layout_.trackCount(); ++i)
        if (rowLane(i) != nullptr)
        {
            const auto bounds = rowBounds(i, header);
            if (bounds.getBottom() < static_cast<float>(header.getY()) || bounds.getY() > static_cast<float>(header.getBottom()))
                continue;
            const auto target = laneTarget(i);
            const auto value = session_.laneValue(target);
            auto text = value ? session_.laneText(target, *value) : std::string();
            if (auto& shown = laneTexts_[rows_[static_cast<size_t>(i)].key()]; shown != text)
            {
                shown = std::move(text);
                repaint(bounds.getSmallestIntegerContainer());
            }
        }

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
    {
        layout_.setHeight(i, height);
        heights_[rows_[static_cast<size_t>(i)].key()] = layout_.height(i);
    }
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

void ArrangementView::setSelection(model::Selection selection)
{
    setQuietly(std::move(selection));
    notifySelection();
}

void ArrangementView::setQuietly(model::Selection selection)
{
    if (selection == selection_)
        return;
    selection_ = std::move(selection);
    repaint();
}

void ArrangementView::notifySelection()
{
    if (selection_ == notified_)
        return;
    notified_ = selection_;
    if (onSelectionChanged)
        onSelectionChanged();
}

void ArrangementView::selectLane(std::uint64_t channel, const std::string& lane)
{
    setSelection({.channel = channel, .lane = lane});
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
    const int mixRow = model::firstMixRow(rows_);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto* channel = rowChannel(i);
        if (channel == nullptr)
            continue;
        const auto& track = *channel;
        auto row = rowBounds(i, area);
        if (row.getBottom() < static_cast<float>(area.getY()) || row.getY() > static_cast<float>(area.getBottom()))
            continue;
        const auto* lane = rowLane(i);
        const bool master = track.role == model::TrackInfo::Role::Master;
        const bool selected = selection_.channel == track.id &&
                              (lane != nullptr ? selection_.lane == lane->key : !selection_.lane.has_value());
        if (selected || lane != nullptr)
        {
            g.setColour(selected ? t.surface2 : t.roles.subLane);
            g.fillRect(row);
        }
        // The slots of the header (docs/design/ui-foundations.md, "Track headers"); the mixer
        // (C3), lanes (C4) and recording (C5) add their buttons, controls and meter to it.
        const auto slots = lane != nullptr ? model::layoutTrackHeader(row.getWidth(), row.getHeight(), 1, false)
                                           : model::layoutTrackHeader(row.getWidth(), row.getHeight(), master ? 0 : 2, true);
        auto at = [&row](const model::HeaderRect& r, float indent = 0.0f) {
            return juce::Rectangle<float>(static_cast<float>(r.x) + indent, static_cast<float>(r.y),
                                          std::max(0.0f, static_cast<float>(r.w) - indent), static_cast<float>(r.h))
                .translated(row.getX(), row.getY());
        };
        g.setColour(master ? t.textTertiary : t.clipColour(track.colour));
        g.fillRect(at(slots.colour));
        g.setColour(t.line);
        if (i == mixRow)
            g.fillRect(row.withHeight(2.0f)); // the divider above the returns and the master
        g.fillRect(juce::Rectangle<float>(row).removeFromBottom(1.0f)); // the row separator, continued across the lanes

        if (lane != nullptr)
        {
            // A lane: the parameter, its module and its value now, under its track (§3).
            g.setColour(t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::Name));
            g.drawText(juce::String::fromUTF8(lane->name.c_str()), at(slots.name, kLaneIndent), juce::Justification::centredLeft,
                       true);
            if (!slots.subtitle.isEmpty())
            {
                auto subtitle = at(slots.subtitle, kLaneIndent);
                const auto it = laneTexts_.find(rows_[static_cast<size_t>(i)].key());
                if (it != laneTexts_.end() && !it->second.empty())
                {
                    g.setColour(t.textSecondary);
                    g.setFont(ui::font(ui::TextStyle::Value));
                    g.drawText(juce::String::fromUTF8(it->second.c_str()), subtitle, juce::Justification::centredRight, false);
                }
                g.setColour(t.textTertiary);
                g.setFont(ui::font(ui::TextStyle::Caption));
                g.drawText(juce::String::fromUTF8(lane->module.c_str()), subtitle.withTrimmedRight(60.0f),
                           juce::Justification::centredLeft, true);
            }
            continue;
        }

        // The fold chevron: ▾ shows the channel's lanes, ▸ folds them.
        if (const auto chevron = chevronArea(i); !chevron.isEmpty())
        {
            const auto c = chevron.getCentre();
            juce::Path p;
            if (track.lanesFolded)
                p.addTriangle(c.x - 1.5f, c.y - 3.0f, c.x - 1.5f, c.y + 3.0f, c.x + 2.0f, c.y);
            else
                p.addTriangle(c.x - 3.0f, c.y - 1.5f, c.x + 3.0f, c.y - 1.5f, c.x, c.y + 2.0f);
            g.setColour(t.textTertiary);
            g.fillPath(p);
        }

        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::Name));
        g.drawText(juce::String::fromUTF8(track.name.c_str()), at(slots.name), juce::Justification::centredLeft, true);
        if (slots.subtitle.isEmpty())
            continue;
        const auto folded = track.lanesFolded && !track.lanes.empty()
                                ? juce::String::fromUTF8("\xC2\xB7 ") + juce::String(static_cast<int>(track.lanes.size())) +
                                      (track.lanes.size() == 1 ? " lane" : " lanes")
                                : juce::String();
        auto joined = [&folded](juce::String label) {
            return folded.isEmpty() ? label : label.isEmpty() ? folded : label + " " + folded;
        };
        if (!track.holdsClips())
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(joined(master ? "Master" : "Return"), at(slots.subtitle), juce::Justification::centredLeft, true);
            continue;
        }
        // One kind of track (ADR 0019): show the instrument, or that MIDI has nothing to play it.
        const bool hasMidi = std::any_of(track.clips.begin(), track.clips.end(),
                                         [](const auto& c) { return c.kind == model::ClipInfo::Kind::Midi; });
        const auto label = !track.instrument.empty() ? juce::String::fromUTF8(track.instrument.c_str())
                           : hasMidi                 ? juce::String("No instrument")
                                                     : juce::String();
        g.setColour(track.instrument.empty() && hasMidi ? t.roles.warning : t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(joined(label), at(slots.subtitle), juce::Justification::centredLeft, true);
    }
}

void ArrangementView::paintLanes(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = laneArea();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    auto rowVisible = [&](int i) {
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        return !(y > static_cast<float>(area.getBottom()) || y + static_cast<float>(layout_.height(i)) < static_cast<float>(area.getY()));
    };

    // Row backgrounds: close tone steps; the selected track (or lane) a step lighter; lanes,
    // returns and the master on sub-lanes.
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        if (!rowVisible(i))
            continue;
        const auto& r = rows_[static_cast<size_t>(i)];
        const bool selected = selection_.channel == r.channel &&
                              (r.isLane() ? selection_.lane == r.lane : !selection_.lane.has_value());
        g.setColour(selected ? t.roles.laneSelected : r.holdsClips() ? t.roles.lane : t.roles.subLane);
        g.fillRect(rowBounds(i, area));
    }

    // Grid: bar, beat and subdivision lines at falling contrast, all quieter than the track
    // separators (the strongest lines in the arrangement).
    const float bottom = static_cast<float>(area.getY() + std::min<double>(layout_.top(layout_.trackCount()), area.getHeight()));
    time::paintGrid(g, grid_, timeline_, area.withBottom(static_cast<int>(bottom)));

    // Row separators: one pixel of `line`, a step lighter than the lanes, across the width.
    g.setColour(t.line);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const float y = static_cast<float>(area.getY() + layout_.top(i) + layout_.height(i)) - 1.0f;
        if (y >= static_cast<float>(area.getY()) && y <= static_cast<float>(area.getBottom()))
            g.fillRect(static_cast<float>(area.getX()), y, static_cast<float>(area.getWidth()), 1.0f);
    }
    if (const int mixRow = model::firstMixRow(rows_); mixRow < layout_.trackCount()) // the divider above the returns and the master
        g.fillRect(static_cast<float>(area.getX()), static_cast<float>(area.getY() + layout_.top(mixRow)),
                   static_cast<float>(area.getWidth()), 2.0f);

    // Clips: a solid title strip with dark text over a dark body of the same hue. Only what
    // intersects the invalidated area is drawn (a playhead-only repaint touches a few columns).
    // Clips being dragged are drawn at their new place instead.
    const auto visible = area.getIntersection(g.getClipBounds());
    std::set<std::uint64_t> moving;
    for (const auto& e : preview_)
        moving.insert(e.clip);
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto& r = rows_[static_cast<size_t>(i)];
        if (!r.holdsClips() || !rowVisible(i))
            continue;
        const auto& track = snapshot_->tracks[static_cast<size_t>(r.track)];
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        const float h = static_cast<float>(layout_.height(i));
        for (const auto& clip : track.clips)
            if (!moving.contains(clip.id))
                paintClip(g, clip, y, h, area, visible, selection_.clips.contains(clip.id));
    }
    for (const auto& e : preview_)
    {
        const auto found = model::findClip(*snapshot_, e.clip);
        int index = 0;
        while (index < static_cast<int>(snapshot_->tracks.size()) && snapshot_->tracks[static_cast<size_t>(index)].id != e.track)
            ++index;
        const int row = model::rowOfTrack(rows_, index);
        if (!found || row < 0)
            continue;
        auto clip = *found->first;
        clip.start = e.start;
        clip.length = e.length;
        clip.offset = e.offset;
        paintClip(g, clip, static_cast<float>(area.getY() + layout_.top(row)), static_cast<float>(layout_.height(row)), area,
                  visible, true);
    }

    // Automation lanes (C4): the curve the engine plays, over the reserved gradient.
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto* lane = rowLane(i);
        if (lane == nullptr || !rowVisible(i))
            continue;
        const auto key = rows_[static_cast<size_t>(i)].key();
        const bool previewing = lanePreview_ && lanePreview_->first == key;
        LanePaint paint{.points = previewing ? &lanePreview_->second.points : &lane->points, .on = lane->on};
        if (previewing)
            paint.selected = lanePreview_->second.selection;
        else if (selection_.channel == rows_[static_cast<size_t>(i)].channel && selection_.lane == lane->key)
            paint.selected = selection_.points;
        if (hoveredPoint_ && hoveredPoint_->first == i)
            paint.hovered = hoveredPoint_->second;
        if (lane->points.empty())
            paint.current = session_.laneValue(laneTarget(i));
        paintAutomationLane(g, laneGeometry(i), paint, visible);
    }

    // Box selection (of clips, or of a lane's points).
    if (drag_ == Drag::Box || (drag_ == Drag::Lane && laneDrag_.kind == LaneDrag::Kind::Box && dragMoved_))
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
        const auto [track, seconds] = *dropTarget_;
        const int row = track >= 0 ? model::rowOfTrack(rows_, track) : -1;
        const float y = row >= 0 ? static_cast<float>(area.getY() + layout_.top(row)) : bottom;
        const float h = row >= 0 ? static_cast<float>(layout_.height(row)) : static_cast<float>(model::TrackLayout::defaultHeight);
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
    std::optional<std::pair<int, size_t>> hovered;
    if (loopStripArea().contains(e.getPosition()))
    {
        const auto zone = loopZoneAt(e.position.x);
        cursor = zone == LoopZone::Start || zone == LoopZone::End ? juce::MouseCursor::LeftRightResizeCursor
                                                                  : juce::MouseCursor::NormalCursor;
    }
    else if (headerArea().contains(e.getPosition()))
    {
        // A row's bottom edge resizes it.
        if (const auto row = layout_.hit(e.position.y - static_cast<float>(lanes.getY()) + kResizeGrab);
            row && row->offset < 2.0f * kResizeGrab && row->track > 0)
            cursor = juce::MouseCursor::UpDownResizeCursor;
    }
    else if (lanes.contains(e.getPosition()))
    {
        const auto row = layout_.hit(e.position.y - static_cast<float>(lanes.getY()));
        if (row && rowLane(row->track) != nullptr)
        {
            const auto geo = laneGeometry(row->track);
            const auto& points = rowLane(row->track)->points;
            const auto map = geo.mapping();
            if (const auto p = model::hitPoint(points, map, e.position.x, e.position.y, ui::Theme::current().input.edgeGrab))
            {
                hovered = std::pair{row->track, *p};
                cursor = juce::MouseCursor::DraggingHandCursor;
            }
            else if (model::hitSegment(points, map, e.position.x, e.position.y, 4.0))
                cursor = juce::MouseCursor::UpDownResizeCursor;
        }
        else
        {
            std::vector<int> rowTracks;
            for (const auto& r : rows_)
                rowTracks.push_back(r.holdsClips() ? r.track : -1);
            if (const auto hit = model::hitClip(*snapshot_, layout_, rowTracks, timeline_,
                                                e.position.x - static_cast<float>(lanes.getX()),
                                                e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab))
                cursor = hit->zone == model::ClipHit::Zone::Body ? juce::MouseCursor::NormalCursor
                                                                  : juce::MouseCursor::LeftRightResizeCursor;
        }
    }
    if (hovered != hoveredPoint_)
    {
        hoveredPoint_ = hovered;
        repaint(lanes);
    }
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
        // A row's bottom edge: resize it (Alt: every row of its kind).
        if (const auto edge = layout_.hit(e.position.y - static_cast<float>(lanes.getY()) + kResizeGrab);
            edge && edge->offset < 2.0f * kResizeGrab && edge->track > 0 && !e.mods.isPopupMenu())
        {
            drag_ = Drag::Resize;
            resizeRow_ = edge->track - 1;
            resizeFrom_ = layout_.height(*resizeRow_);
            return;
        }
        const auto row = layout_.hit(pos.y - lanes.getY());
        if (!row)
        {
            selectTrack(std::nullopt);
            return;
        }
        const auto* channel = rowChannel(row->track);
        // The fold chevron (Alt: every channel).
        if (const auto chevron = chevronArea(row->track); chevron.contains(e.position) && channel != nullptr)
        {
            const bool fold = !channel->lanesFolded;
            if (e.mods.isAltDown())
            {
                for (const auto* list : {&snapshot_->tracks, &snapshot_->returns})
                    for (const auto& t : *list)
                        if (!t.lanes.empty())
                            session_.setLanesFolded(t.id, fold);
                if (!snapshot_->master.lanes.empty())
                    session_.setLanesFolded(snapshot_->master.id, fold);
            }
            else
                session_.setLanesFolded(channel->id, fold);
            return;
        }
        // A header selects its channel alone, or its lane (selection.md §2, automation.md §9).
        if (rowLane(row->track) != nullptr)
        {
            selectLane(rows_[static_cast<size_t>(row->track)].channel, rows_[static_cast<size_t>(row->track)].lane);
            if (onLaneActivated)
                onLaneActivated();
        }
        else
            selectTrack(channel != nullptr ? std::optional(channel->id) : std::nullopt);
        if (e.mods.isPopupMenu())
            headerMenu(row->track);
        return;
    }
    if (!lanes.contains(pos))
        return;

    auto row = layout_.hit(pos.y - lanes.getY());
    if (row && rowLane(row->track) != nullptr)
    {
        laneMouseDown(e, row->track);
        return;
    }
    const auto* rowChannelInfo = row ? rowChannel(row->track) : nullptr;
    const auto rowTrack = rowChannelInfo != nullptr ? std::optional(rowChannelInfo->id) : std::nullopt;
    if (row && !rows_[static_cast<size_t>(row->track)].holdsClips())
    {
        selectTrack(rowTrack); // returns and the master hold no clips
        return;
    }
    std::vector<int> rowTracks;
    for (const auto& r : rows_)
        rowTracks.push_back(r.holdsClips() ? r.track : -1);
    const auto hit = model::hitClip(*snapshot_, layout_, rowTracks, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                                    e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab);
    if (e.getNumberOfClicks() >= 2 && row)
    {
        // Double-click: open a clip in the clip view, or make a MIDI clip on an empty spot.
        drag_ = Drag::None;
        if (hit)
        {
            setSelection(model::withClips(*snapshot_, {hit->clip}, rowTrack, rowTrack));
            if (onOpenClip)
                onOpenClip(hit->clip);
        }
        else
            createMidiClip(rows_[static_cast<size_t>(row->track)].track,
                           timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX())), e.mods.isAltDown());
        return;
    }
    clickedInSelection_.reset();
    if (hit)
    {
        if (!additive && selection_.clips.size() > 1 && selection_.clips.contains(hit->clip))
            clickedInSelection_ = hit->clip; // stays a group for a drag; just this clip for a click
        auto clips = selection_.clips;
        if (additive && clips.contains(hit->clip))
            clips.erase(hit->clip);
        else if (additive)
            clips.insert(hit->clip);
        else if (!clips.contains(hit->clip))
            clips = {hit->clip};
        // The clip's track; if Shift-click removed it, the topmost remaining clip's.
        const bool kept = clips.contains(hit->clip);
        auto next = model::withClips(*snapshot_, std::move(clips), kept ? rowTrack : std::nullopt, rowTrack);
        if (clickedInSelection_)
            setQuietly(std::move(next)); // told at mouseUp, once it's a click or a drag
        else
            setSelection(std::move(next));
        if (!kept)
            return; // Shift-click removed it
        drag_ = Drag::Clip;
        dragStartTrack_ = hit->track;
        clipDrag_ = {.kind = hit->zone == model::ClipHit::Zone::StartEdge ? model::DragKind::TrimStart
                             : hit->zone == model::ClipHit::Zone::EndEdge  ? model::DragKind::TrimEnd
                                                                           : model::DragKind::Move,
                     .anchor = hit->clip};
        return;
    }
    // Empty lane: box selection (Shift adds to the selection), told at mouseUp since a box changes
    // the selection on every move.
    drag_ = Drag::Box;
    boxBase_ = additive ? selection_.clips : std::set<std::uint64_t>{};
    boxChannel_ = rowTrack ? rowTrack : selection_.channel;
    if (!additive)
        setQuietly({.channel = boxChannel_});
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
    if (drag_ == Drag::Resize && resizeRow_)
    {
        // Alt: every row of the same kind (tracks, or lanes) takes the height.
        const double height = resizeFrom_ + (e.position.y - dragStart_.y);
        const bool lane = rows_[static_cast<size_t>(*resizeRow_)].isLane();
        for (int i = 0; i < layout_.trackCount(); ++i)
            if (i == *resizeRow_ || (e.mods.isAltDown() && rows_[static_cast<size_t>(i)].isLane() == lane))
            {
                layout_.setHeight(i, height);
                heights_[rows_[static_cast<size_t>(i)].key()] = layout_.height(i);
            }
        placeHeaderControls();
        repaint();
        return;
    }
    lastDrag_ = e.position;
    if (!dragMoved_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        return; // a click, not a drag
    dragMoved_ = true;

    if (drag_ == Drag::Lane)
    {
        laneMouseDrag(e);
        return;
    }
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

    const int tracks = static_cast<int>(snapshot_->tracks.size());
    auto trackAt = [&](float y) {
        const auto row = layout_.hit(y - static_cast<float>(lanes.getY()));
        return model::clipTrackOf(rows_, row ? row->track : (y < static_cast<float>(lanes.getY()) ? -1 : layout_.trackCount()),
                                  tracks);
    };
    if (drag_ == Drag::Clip)
    {
        clipDrag_.deltaSeconds = (e.position.x - dragStart_.x) / timeline_.pixelsPerSecond();
        if (clipDrag_.kind == model::DragKind::Move)
            clipDrag_.deltaTracks = trackAt(e.position.y) - dragStartTrack_;
        preview_ = model::dragClips(*snapshot_, selection_.clips, clipDrag_, snapFunction(e.mods.isAltDown()));
        repaint();
    }
    else if (drag_ == Drag::Box)
    {
        const double t0 = timeline_.timeForX(dragStart_.x - static_cast<float>(lanes.getX()));
        const double t1 = timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX()));
        auto selection = boxBase_;
        for (auto id : model::clipsInBox(*snapshot_, t0, t1, trackAt(dragStart_.y), trackAt(e.position.y)))
            selection.insert(id);
        setQuietly(model::withClips(*snapshot_, std::move(selection), std::nullopt, boxChannel_));
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
    if (drag_ == Drag::Lane)
        laneMouseUp(e);
    if (drag_ == Drag::Resize)
        resizeRow_.reset();
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
    {
        session_.applyClipEdits(preview_); // one undo step; the snapshot follows
        // The channel follows the grabbed clip to its new track (the snapshot is still the old one).
        for (const auto& edit : preview_)
            if (edit.clip == clipDrag_.anchor)
                setQuietly({.channel = edit.track, .clips = selection_.clips});
    }
    if (drag_ == Drag::Clip && !dragMoved_ && clickedInSelection_)
        setQuietly({.channel = selection_.channel, .clips = {*clickedInSelection_}});
    clickedInSelection_.reset();
    notifySelection(); // once per gesture
    // Keep drawing the preview until the new snapshot arrives (no flicker back).
    if (drag_ != Drag::Clip || !dragMoved_)
        preview_.clear();
    drag_ = Drag::None;
    repaint();
}

// ---- automation lanes ----------------------------------------------------------------------

void ArrangementView::applyLaneEdit(int row, const model::PointEdit& edit, const std::string& undoName)
{
    const auto target = laneTarget(row);
    lanePreview_ = std::pair{rows_[static_cast<size_t>(row)].key(), edit}; // until the snapshot has it
    session_.setAutomation(target, edit.points, std::nullopt, undoName);
    setQuietly({.channel = target.channel, .lane = target.lane, .points = edit.selection});
    repaint();
}

void ArrangementView::laneMouseDown(const juce::MouseEvent& e, int row)
{
    const auto* lane = rowLane(row);
    if (lane == nullptr)
        return;
    if (onLaneActivated)
        onLaneActivated();
    const auto target = laneTarget(row);
    const auto geo = laneGeometry(row);
    const auto map = geo.mapping();
    const auto rules = lane->rules();
    const auto& points = lane->points;
    const auto grab = ui::Theme::current().input.edgeGrab;
    const auto hitPoint = model::hitPoint(points, map, e.position.x, e.position.y, grab);
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();
    const bool sameLane = selection_.channel == target.channel && selection_.lane == target.lane;
    drag_ = Drag::Lane;
    laneDrag_ = {.row = row, .base = points};

    if (e.getNumberOfClicks() >= 2)
    {
        drag_ = Drag::None;
        if (hitPoint && e.mods.isAltDown()) // Alt + double-click straightens the segment after it
            applyLaneEdit(row, model::bendSegment(points, *hitPoint, 0.0f, rules), "Bend");
        else if (hitPoint)
            applyLaneEdit(row, model::deletePoints(points, {*hitPoint}), "Delete point");
        else
            applyLaneEdit(row,
                          model::addPoint(points, snapped(geo.time(e.position.x), e.mods.isAltDown()), geo.value(e.position.y), rules),
                          "Add point");
        notifySelection();
        return;
    }
    if (hitPoint)
    {
        auto selection = sameLane ? selection_.points : std::set<size_t>{};
        if (!additive && selection.size() > 1 && selection.contains(*hitPoint))
            laneDrag_.clickedInSelection = *hitPoint; // stays a group for a drag; just this point for a click
        else if (additive && selection.contains(*hitPoint))
            selection.erase(*hitPoint);
        else if (additive)
            selection.insert(*hitPoint);
        else
            selection = {*hitPoint};
        setQuietly({.channel = target.channel, .lane = target.lane, .points = selection});
        laneDrag_.kind = selection.contains(*hitPoint) ? LaneDrag::Kind::Points : LaneDrag::Kind::None;
        laneDrag_.selection = selection;
        laneDrag_.index = *hitPoint;
        return;
    }
    if (e.mods.isCommandDown()) // Ctrl + drag draws
    {
        laneDrag_.kind = LaneDrag::Kind::Draw;
        const double at = snapped(geo.time(e.position.x), e.mods.isAltDown());
        laneDrag_.drawn = {{at, geo.value(e.position.y), 0.0f}};
        setQuietly({.channel = target.channel, .lane = target.lane});
        return;
    }
    if (const auto segment = model::hitSegment(points, map, e.position.x, e.position.y, 4.0))
    {
        laneDrag_.kind = e.mods.isAltDown() ? LaneDrag::Kind::Bend : LaneDrag::Kind::Segment;
        laneDrag_.index = *segment;
        laneDrag_.bend = model::bendOf(points[*segment]);
        setQuietly({.channel = target.channel, .lane = target.lane, .points = {*segment, *segment + 1}});
        return;
    }
    // An empty spot: box-select points (Shift adds).
    laneDrag_.kind = LaneDrag::Kind::Box;
    laneDrag_.boxBase = additive && sameLane ? selection_.points : std::set<size_t>{};
    setQuietly({.channel = target.channel, .lane = target.lane, .points = laneDrag_.boxBase});
}

void ArrangementView::laneMouseDrag(const juce::MouseEvent& e)
{
    const int row = laneDrag_.row;
    const auto* lane = rowLane(row);
    if (lane == nullptr)
        return;
    const auto geo = laneGeometry(row);
    const auto rules = lane->rules();
    const auto key = rows_[static_cast<size_t>(row)].key();
    const float travel = std::max(1.0f, geo.area.getHeight() - 2.0f * LaneGeometry::inset);
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    const float dv = static_cast<float>(dragStart_.y - e.position.y) / travel * fine;
    switch (laneDrag_.kind)
    {
    case LaneDrag::Kind::Points:
    {
        // The grabbed point snaps in time; the others follow by the same amount.
        const auto& anchor = laneDrag_.base[laneDrag_.index];
        const double moved = geo.time(e.position.x) - geo.time(dragStart_.x);
        const double dt = snapped(anchor.time + moved, e.mods.isAltDown()) - anchor.time;
        lanePreview_ = std::pair{key, model::movePoints(laneDrag_.base, laneDrag_.selection, dt, dv, rules)};
        break;
    }
    case LaneDrag::Kind::Segment:
        lanePreview_ = std::pair{key, model::moveSegment(laneDrag_.base, laneDrag_.index, dv, rules)};
        break;
    case LaneDrag::Kind::Bend:
    {
        // Up bows the segment up: early for a rising segment, late for a falling one.
        const auto& a = laneDrag_.base[laneDrag_.index];
        const auto& b = laneDrag_.base[laneDrag_.index + 1];
        const float up = static_cast<float>(dragStart_.y - e.position.y) / 60.0f * fine;
        const float bend = std::clamp(laneDrag_.bend + (b.value >= a.value ? -up : up), -1.0f, 1.0f);
        lanePreview_ = std::pair{key, model::bendSegment(laneDrag_.base, laneDrag_.index, bend, rules)};
        break;
    }
    case LaneDrag::Kind::Draw:
    {
        // One point per grid step passed (or where the mouse is, with snap off).
        const double at = snapped(geo.time(e.position.x), e.mods.isAltDown());
        auto& drawn = laneDrag_.drawn;
        const auto it = std::find_if(drawn.begin(), drawn.end(), [at](const auto& p) { return std::abs(p.time - at) < 1e-9; });
        if (it != drawn.end())
            it->value = geo.value(e.position.y);
        else
            drawn.push_back({at, geo.value(e.position.y), 0.0f});
        lanePreview_ = std::pair{key, model::drawPoints(laneDrag_.base, drawn, rules)};
        break;
    }
    case LaneDrag::Kind::Box:
    {
        auto inside = model::pointsInBox(laneDrag_.base, geo.time(dragStart_.x), geo.time(e.position.x),
                                         geo.value(dragStart_.y), geo.value(e.position.y));
        inside.insert(laneDrag_.boxBase.begin(), laneDrag_.boxBase.end());
        setQuietly({.channel = selection_.channel, .lane = selection_.lane, .points = inside});
        break;
    }
    case LaneDrag::Kind::None: break;
    }
    repaint();
}

void ArrangementView::laneMouseUp(const juce::MouseEvent&)
{
    const int row = laneDrag_.row;
    if (dragMoved_ && lanePreview_ && rowLane(row) != nullptr)
    {
        const char* name = laneDrag_.kind == LaneDrag::Kind::Bend   ? "Bend"
                           : laneDrag_.kind == LaneDrag::Kind::Draw ? "Draw automation"
                                                                    : "Move points";
        applyLaneEdit(row, lanePreview_->second, name);
    }
    else if (!dragMoved_ && laneDrag_.clickedInSelection)
        setQuietly({.channel = selection_.channel, .lane = selection_.lane, .points = {*laneDrag_.clickedInSelection}});
    else if (!dragMoved_ && laneDrag_.kind == LaneDrag::Kind::Draw && rowLane(row) != nullptr)
        applyLaneEdit(row, model::drawPoints(laneDrag_.base, laneDrag_.drawn, rowLane(row)->rules()), "Draw automation");
    laneDrag_ = {};
}

void ArrangementView::deletePoints()
{
    if (!hasPointSelection() || !selection_.channel)
        return;
    const auto* channel = snapshot_->channel(*selection_.channel);
    const auto* lane = channel != nullptr ? channel->lane(*selection_.lane) : nullptr;
    if (lane == nullptr)
        return;
    session_.setAutomation({*selection_.channel, lane->key}, model::deletePoints(lane->points, selection_.points).points,
                           std::nullopt, "Delete points");
    setSelection({.channel = selection_.channel, .lane = selection_.lane});
}

void ArrangementView::duplicatePoints()
{
    if (!hasPointSelection() || !selection_.channel)
        return;
    const auto* channel = snapshot_->channel(*selection_.channel);
    const auto* lane = channel != nullptr ? channel->lane(*selection_.lane) : nullptr;
    if (lane == nullptr)
        return;
    const auto edit = model::duplicatePoints(lane->points, selection_.points, gridStepSeconds(), lane->rules());
    session_.setAutomation({*selection_.channel, lane->key}, edit.points, std::nullopt, "Duplicate points");
    setSelection({.channel = selection_.channel, .lane = selection_.lane, .points = edit.selection});
}

void ArrangementView::selectAllPoints()
{
    if (!selection_.lane || !selection_.channel)
        return;
    const auto* channel = snapshot_->channel(*selection_.channel);
    const auto* lane = channel != nullptr ? channel->lane(*selection_.lane) : nullptr;
    if (lane == nullptr)
        return;
    std::set<size_t> all;
    for (size_t i = 0; i < lane->points.size(); ++i)
        all.insert(i);
    setSelection({.channel = selection_.channel, .lane = selection_.lane, .points = std::move(all)});
}

void ArrangementView::nudgePoints(int gridSteps, float value)
{
    if (!hasPointSelection() || !selection_.channel)
        return;
    const auto* channel = snapshot_->channel(*selection_.channel);
    const auto* lane = channel != nullptr ? channel->lane(*selection_.lane) : nullptr;
    if (lane == nullptr)
        return;
    const auto edit =
        model::movePoints(lane->points, selection_.points, gridSteps * gridStepSeconds(), value, lane->rules());
    session_.setAutomation({*selection_.channel, lane->key}, edit.points, std::nullopt, "Nudge points");
    setSelection({.channel = selection_.channel, .lane = selection_.lane, .points = edit.selection});
}

void ArrangementView::removeSelectedLane()
{
    if (!selection_.lane || !selection_.channel)
        return;
    session_.removeLane({*selection_.channel, *selection_.lane});
    selectTrack(selection_.channel);
}

void ArrangementView::headerMenu(int row)
{
    const auto* channel = rowChannel(row);
    if (channel == nullptr)
        return;
    juce::PopupMenu menu;
    if (const auto* lane = rowLane(row))
    {
        const auto target = laneTarget(row);
        menu.addItem(lane->on ? "Turn lane off" : "Turn lane on", !lane->points.empty(),
                     false, [this, target, on = lane->on] { session_.setLaneOn(target, !on); });
        menu.addItem("Remove lane", [this, target] {
            session_.removeLane(target);
            selectTrack(target.channel);
        });
    }
    else
    {
        const auto id = channel->id;
        menu.addItem("Add automation lane...", [this, id] {
            if (onAddLane)
                onAddLane(id);
        });
        if (!channel->lanes.empty())
            menu.addItem(channel->lanesFolded ? "Show lanes" : "Fold lanes",
                         [this, id, folded = channel->lanesFolded] { session_.setLanesFolded(id, !folded); });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

// ---- edit commands -------------------------------------------------------------------------

void ArrangementView::deleteSelection()
{
    session_.deleteClips({selection_.clips.begin(), selection_.clips.end()});
    selectTrack(selection_.channel);
}

void ArrangementView::duplicateSelection()
{
    const auto created = session_.duplicateClips(model::duplicatePlacements(*snapshot_, selection_.clips));
    setSelection({.channel = selection_.channel, .clips = {created.begin(), created.end()}});
}

void ArrangementView::splitAtPlayhead()
{
    std::vector<std::uint64_t> targets(selection_.clips.begin(), selection_.clips.end());
    if (targets.empty() && selection_.channel)
        for (const auto& t : snapshot_->tracks)
            if (t.id == *selection_.channel)
                for (const auto& c : t.clips)
                    targets.push_back(c.id);
    const auto right = session_.splitClips(targets, playhead_);
    if (!selection_.clips.empty())
    {
        auto clips = selection_.clips;
        clips.insert(right.begin(), right.end());
        setSelection({.channel = selection_.channel, .clips = std::move(clips)});
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
    setSelection({.channel = track.id, .clips = {id}});
    if (addedInstrument && onMessage)
        onMessage("Added a Subtractive Synth to play the MIDI clip (swap it in the module chain)");
    if (onOpenClip)
        onOpenClip(id);
    return id;
}

std::uint64_t ArrangementView::createMidiClipAtPlayhead()
{
    int index = 0;
    if (selection_.channel)
        for (size_t i = 0; i < snapshot_->tracks.size(); ++i)
            if (snapshot_->tracks[i].id == *selection_.channel)
                index = static_cast<int>(i);
    return createMidiClip(index, playhead_);
}

void ArrangementView::selectAllClips()
{
    std::set<std::uint64_t> all;
    for (const auto& t : snapshot_->tracks)
        for (const auto& c : t.clips)
            all.insert(c.id);
    setSelection(model::withClips(*snapshot_, std::move(all), selection_.channel, selection_.channel));
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
    // Onto a track, or one of its lanes; below the tracks a new track.
    dropTarget_ = std::pair{hit ? rows_[static_cast<size_t>(hit->track)].track : -1, seconds};
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
        selectTrack(first);
    repaint();
}
} // namespace fd::app
