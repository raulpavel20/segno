#include "ui/ArrangementView.h"

#include "model/ClipLoops.h"
#include "model/Groups.h"
#include "model/MusicalTime.h"
#include "model/Takes.h"
#include "model/TrackHeaderLayout.h"
#include "ui/BrowserDrops.h"
#include "ui/GridMenu.h"
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

/// A channel header's buttons: mute and solo, and for a track its arm button; none on the master.
int buttonsOf(const model::TrackInfo& channel)
{
    return channel.role == model::TrackInfo::Role::Master ? 0 : channel.holdsClips() ? 3 : 2;
}

/// How close to a row's bottom edge (in the header column) a drag resizes the row.
constexpr float kResizeGrab = 3.0f;
} // namespace

ArrangementView::HeaderControls::HeaderControls(engine::Session& session, const model::TrackInfo& channel)
    : mute(session, ChannelToggle::Kind::Mute, channel.id), solo(session, ChannelToggle::Kind::Solo, channel.id),
      arm(session, ChannelToggle::Kind::Arm, channel.id),
      volume(session, channel.id), pan(session, channel.id), meter(session, channel.id)
{
    // Header bars are narrow: values only; the tooltips name them.
    volume.setName({});
    pan.setName({});
}

ArrangementView::ArrangementView(engine::Session& session) : session_(session), loopCursor_(ui::cursorFor(ui::Icon::Loop))
{
    headerLayer_.setInterceptsMouseClicks(false, true); // the header's background still selects
    addAndMakeVisible(headerLayer_);
    songLayer_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(songLayer_);
    waveforms_.onReady = [this] { repaint(); };
    sums_.onReady = [this] { repaint(laneArea()); };
    setSnapshot(session_.snapshot());
    setWantsKeyboardFocus(false);
    snapToggle_.setClickingTogglesState(false); // the action toggles it
    snapToggle_.onClick = [this] { setSnapEnabled(!isSnapEnabled()); };
    addAndMakeVisible(snapToggle_);
    gridChoice_.setTooltip(
        "The grid: the step it snaps to. Click to set it: adaptive or fixed, triplets, snap");
    gridChoice_.onClick = [this] { gridChoice_.showMenu(gridMenu()); };
    addAndMakeVisible(gridChoice_);
    syncGrid();
}

void ArrangementView::setSnapEnabled(bool on)
{
    auto g = grid();
    g.snap = on;
    setGrid(g);
}

void ArrangementView::setGrid(const model::GridSettings& grid)
{
    session_.setGrid(engine::Session::GridEditor::Arrangement, grid);
    syncGrid();
}

juce::PopupMenu ArrangementView::gridMenu()
{
    return app::gridMenu(grid(),
                         [safe = juce::Component::SafePointer(this)](const model::GridSettings& g)
                         {
                             if (safe != nullptr)
                                 safe->setGrid(g);
                         });
}

void ArrangementView::syncGrid()
{
    const auto g = session_.grid(engine::Session::GridEditor::Arrangement);
    if (g != timeline_.grid())
    {
        timeline_.setGrid(g);
        repaint();
    }
    snapToggle_.setToggleState(g.snap, juce::dontSendNotification);
    const auto step = timeline_.gridStepName(snapshot_->tempo);
    if (gridChoice_.value() != juce::String(step))
    {
        gridChoice_.setValue(step);
        session_.setGridStep(engine::Session::GridEditor::Arrangement, step);
    }
    // Beside Snap (ADR 0047), as wide as its step.
    const auto& sh = ui::Theme::current().shell;
    gridChoice_.setBounds(snapToggle_.getRight() + sh.itemGap, snapToggle_.getY(),
                          gridChoice_.preferredWidth(), snapToggle_.getHeight());
}

void ArrangementView::setSnapshot(std::shared_ptr<const model::Snapshot> snapshot)
{
    snapshot_ = std::move(snapshot);
    preview_.clear();
    lanePreview_.reset();
    if (!fadeDrag_)
        fadePreview_.reset();
    hoveredPoint_.reset();
    for (const auto& t : snapshot_->tracks)
    {
        for (const auto& c : t.clips)
            waveforms_.request(c.sourcePath);
        for (const auto& stack : t.stacks) // the takes shown under their clips
            if (stack.open)
                for (const auto& take : stack.takes)
                    for (const auto& c : take.clips)
                        waveforms_.request(c.sourcePath);
    }
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
        if (const auto* lane = rowLane(i))
        {
            const auto key = rows_[static_cast<size_t>(i)].key();
            lanes.insert(key);
            auto& controls = laneControls_[key];
            if (controls == nullptr)
            {
                controls = std::make_unique<LaneControls>(session_, lane->id);
                headerLayer_.addChildComponent(controls->on);
                headerLayer_.addChildComponent(controls->bar);
            }
            controls->update(*lane);
            const auto value = session_.laneValue(lane->id); // the header's readout
            laneTexts_[key] = value ? session_.laneText(lane->id, *value) : std::string();
            continue;
        }
        const auto* channel = rowChannel(i);
        if (channel == nullptr)
            continue;
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
            if (channel->holdsClips()) // tracks record (recording.md §4)
                headerLayer_.addChildComponent(controls->arm);
            headerLayer_.addChildComponent(controls->volume);
            headerLayer_.addChildComponent(controls->pan);
            headerLayer_.addChildComponent(controls->meter);
        }
        controls->mute.update(channel->mix.mute);
        controls->solo.update(channel->mix.solo);
        // Half lit (groups.md §7.3): a member silenced by its group's mute; a group holding a solo.
        controls->mute.setPartlyOn(model::inMutedGroup(*snapshot_, *channel));
        controls->solo.setPartlyOn(model::holdsSolo(*snapshot_, *channel));
        controls->arm.updateArm(channel->input);
        controls->volume.update(*channel);
        controls->pan.update(*channel);
    }
    for (auto it = headerControls_.begin(); it != headerControls_.end();)
        it = shown.contains(it->first) ? std::next(it) : headerControls_.erase(it);
    // The master's, in the song row's header (ADR 0049): its volume and pan bars, its meter.
    const auto& master = snapshot_->master;
    if (masterControls_ == nullptr || masterControlsFor_ != master.id)
    {
        masterControls_ = std::make_unique<HeaderControls>(session_, master);
        masterControlsFor_ = master.id;
        songLayer_.addChildComponent(masterControls_->volume);
        songLayer_.addChildComponent(masterControls_->pan);
        songLayer_.addChildComponent(masterControls_->meter);
    }
    masterControls_->volume.update(master);
    masterControls_->pan.update(master);
    placeSongControls();
    for (auto it = laneControls_.begin(); it != laneControls_.end();)
        it = lanes.contains(it->first) ? std::next(it) : laneControls_.erase(it);
    placeHeaderControls();
    // A lane selected before its snapshot came: into sight now that it has a row.
    if (laneToReveal_)
        if (const int row = model::rowOfLane(rows_, *laneToReveal_); row >= 0)
        {
            laneToReveal_.reset();
            revealRow(row);
        }
    repaint();
}

const model::TrackInfo* ArrangementView::rowChannel(int row) const noexcept
{
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(row)].isLane())
        return nullptr;
    return snapshot_->channel(rows_[static_cast<size_t>(row)].channel);
}

const model::AutomationLaneInfo* ArrangementView::rowLane(int row) const noexcept
{
    if (row < 0 || row >= static_cast<int>(rows_.size()) || !rows_[static_cast<size_t>(row)].isLane())
        return nullptr;
    return snapshot_->automationLane(rows_[static_cast<size_t>(row)].lane);
}

const model::AutomationLaneInfo* ArrangementView::selectedLane() const noexcept
{
    return selection_.lane ? snapshot_->automationLane(*selection_.lane) : nullptr;
}

void ArrangementView::selectTime(model::TimeSpan span, const std::set<std::uint64_t>& tracks,
                                 const std::set<std::uint64_t>& lanes, std::optional<std::uint64_t> channel)
{
    setSelection(model::withTime(*snapshot_, span, tracks, lanes, channel));
}

bool ArrangementView::rowInTime(const model::ArrangementRow& row) const noexcept
{
    if (!selection_.hasTime())
        return false;
    if (row.isLane())
        return selection_.rowLanes.contains(row.lane);
    return (row.holdsClips() || row.isGroup()) && selection_.rowTracks.contains(row.channel);
}

void ArrangementView::previewStretchMove(std::optional<StretchMove> move)
{
    stretchMove_ = move;
    repaint();
}

void ArrangementView::paintTimeSelection(juce::Graphics& g, juce::Rectangle<int> area,
                                         juce::Rectangle<int> visible)
{
    if (!selection_.hasTime())
        return;
    const auto span = *selection_.time;
    // On each of its rows: the fill over the main lane (open takes below aren't in it), its edges.
    auto rowArea = [&](int i)
    {
        const bool track = rows_[static_cast<size_t>(i)].holdsClips();
        const int h =
            static_cast<int>(track ? layout_.baseHeight(i) : layout_.height(i)) - 1; // above the separator
        return juce::Rectangle<int>(area.getX(), area.getY() + static_cast<int>(layout_.top(i)),
                                    area.getWidth(), h)
            .getIntersection(area);
    };
    for (int i = 0; i < layout_.trackCount(); ++i)
        if (rowInTime(rows_[static_cast<size_t>(i)]))
            time::paintTimeSelection(g, timeline_, rowArea(i), span.start, span.end);
    if (!stretchMove_)
        return;

    // Where a dragged stretch lands (§10): what's in it on each track, cut at its edges, drawn at its
    // new place as dragged clips are, under the stretch's own fill; lanes keep their rows. Tracks
    // land by tracks that hold clips, as the Session moves them (groups.md §6.5).
    const double dt = stretchMove_->seconds;
    const auto holders = model::clipTracks(*snapshot_);
    const auto landing = [&](const model::ArrangementRow& r)
    {
        const auto to = static_cast<long>(model::clipTrackRank(*snapshot_, r.track)) + stretchMove_->tracks;
        return to < 0 || to >= static_cast<long>(holders.size())
                   ? -1
                   : model::rowOfTrack(rows_, holders[static_cast<size_t>(to)]);
    };
    for (int from = 0; from < layout_.trackCount(); ++from)
    {
        if (!rows_[static_cast<size_t>(from)].holdsClips() || !rowInTime(rows_[static_cast<size_t>(from)]))
            continue;
        const int row = landing(rows_[static_cast<size_t>(from)]);
        if (row < 0)
            continue;
        const auto& source = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(from)].track)];
        const float y = static_cast<float>(area.getY() + layout_.top(row));
        const float h = static_cast<float>(layout_.baseHeight(row));
        for (const auto& c : source.clips)
        {
            const double a = std::max(c.start, span.start), b = std::min(c.end(), span.end);
            if (b <= a)
                continue;
            auto piece = c; // its part inside the stretch
            piece.offset += a - c.start;
            piece.start = a + dt;
            piece.length = b - a;
            paintClip(g, piece, y, h, visible, {.selected = true});
        }
    }
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto& r = rows_[static_cast<size_t>(i)];
        if (!rowInTime(r))
            continue;
        const int row = r.holdsClips() ? landing(r) : i;
        if (row < 0)
            continue;
        // The landing place looks as the stretch does: one look for selected time.
        time::paintTimeSelection(g, timeline_, rowArea(row), span.start + dt, span.end + dt);
    }
}

bool ArrangementView::rowSelected(const model::ArrangementRow& row) const noexcept
{
    if (row.isLane())
        return selection_.lane == row.lane;
    // Several tracks and groups chosen together (groups.md §8), or the channel: a take's track too.
    if (!selection_.hasTime() && selection_.rowTracks.contains(row.channel))
        return true;
    return !selection_.lane && selection_.channel == row.channel;
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

void ArrangementView::setMixRowsVisible(bool visible)
{
    if (visible == showMixRows_)
        return;
    showMixRows_ = visible;
    if (!visible && selection_.channel) // a return out of sight; the master's row stays
        if (const auto* channel = snapshot_->channel(*selection_.channel);
            channel != nullptr && channel->role == model::TrackInfo::Role::Return)
            selectTrack(std::nullopt);
    syncRows(); // not setSnapshot: pruning against this snapshot could drop a clip made since
}

void ArrangementView::setLanesVisible(bool visible)
{
    if (visible == showLanes_)
        return;
    showLanes_ = visible;
    if (!visible && selection_.lane)
        setSelection({.channel = selection_.channel});
    syncRows();
}

void ArrangementView::syncLayout()
{
    rows_ = model::arrangementRows(*snapshot_, showMixRows_, showLanes_, folded_);
    layout_.setTrackCount(static_cast<int>(rows_.size()));
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        const auto& row = rows_[static_cast<size_t>(i)];
        const auto it = heights_.find(row.key());
        layout_.setHeight(i, it != heights_.end() ? it->second
                             : row.isLane()    ? model::kLaneDefaultHeight
                                               : model::TrackLayout::defaultHeight);
        // A track grows by its open takes, shown under their clips (ADR 0032).
        layout_.setExtra(i, row.holdsClips() ? model::takeStripsHeight(snapshot_->tracks[static_cast<size_t>(row.track)],
                                                                         ui::Theme::current().shell.takeStripHeight)
                                             : 0.0);
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
        const auto* lane = rowLane(i);
        if (channel == nullptr && lane == nullptr)
            continue;
        const double top = layout_.top(i), height = layout_.baseHeight(i); // the header's own height
        const bool visible = top + height > 0.0 && top < area.getHeight();
        auto at = [top](const model::HeaderRect& r) {
            return juce::Rectangle<double>(r.x, r.y + top, r.w, r.h).toNearestInt();
        };
        auto show = [visible](juce::Component& comp, const model::HeaderRect& r, juce::Rectangle<int> bounds) {
            comp.setVisible(visible && !r.isEmpty());
            if (comp.isVisible())
                comp.setBounds(bounds);
        };
        const model::HeaderRect none{};
        if (lane != nullptr)
        {
            const auto it = laneControls_.find(rows_[static_cast<size_t>(i)].key());
            if (it == laneControls_.end())
                continue;
            const auto slots = model::layoutTrackHeader(area.getWidth(), height, 1, false, {},
                                                        rows_[static_cast<size_t>(i)].depth);
            // The on switch only means something with points to play and parameters to play them;
            // the bar only with one parameter to move.
            const bool playable = !lane->points.empty() && !lane->assignments.empty();
            show(it->second->on, slots.buttons.empty() || !playable ? none : slots.buttons[0],
                 slots.buttons.empty() ? juce::Rectangle<int>() : at(slots.buttons[0]));
            show(it->second->bar, lane->assignments.size() == 1 ? slots.controls : none, at(slots.controls));
            if (it->second->bar.isVisible())
                it->second->bar.refresh(); // follows the played value
            continue;
        }
        auto it = headerControls_.find(channel->id);
        if (it == headerControls_.end())
            continue;
        auto& c = *it->second;
        const auto slots = model::layoutTrackHeader(area.getWidth(), height, buttonsOf(*channel), true, {},
                                                    rows_[static_cast<size_t>(i)].depth, channel->isGroup());
        show(c.mute, slots.buttons.size() > 0 ? slots.buttons[0] : none, slots.buttons.size() > 0 ? at(slots.buttons[0]) : juce::Rectangle<int>());
        show(c.solo, slots.buttons.size() > 1 ? slots.buttons[1] : none, slots.buttons.size() > 1 ? at(slots.buttons[1]) : juce::Rectangle<int>());
        if (channel->holdsClips())
            show(c.arm, slots.buttons.size() > 2 ? slots.buttons[2] : none,
                 slots.buttons.size() > 2 ? at(slots.buttons[2]) : juce::Rectangle<int>());
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

model::BraceZone ArrangementView::loopZoneAt(float x) const
{
    const auto [a, b] = loopPreview_.value_or(loop_);
    return model::braceZoneAt(x, laneX(a), laneX(b), ui::Theme::current().input.edgeGrab);
}

juce::Rectangle<int> ArrangementView::headerArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight() + songRowHeight()).removeFromLeft(headerWidth());
}

juce::Rectangle<int> ArrangementView::laneArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight() + songRowHeight()).withTrimmedLeft(headerWidth());
}

float ArrangementView::laneX(double seconds) const
{
    return static_cast<float>(laneArea().getX() + timeline_.xForTime(seconds));
}

double ArrangementView::snapped(double seconds, bool bypass) const
{
    return isSnapEnabled() && !bypass ? timeline_.snap(seconds, snapshot_->tempo) : std::max(0.0, seconds);
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
    placeSongControls();
    // Snap in the corner above the headers, the ruler's row high: its indicator where the
    // headers' text starts (a view option's own padding is half the shell's, as in a panel header).
    const auto& sh = ui::Theme::current().shell;
    const auto names = model::layoutTrackHeader(headerArea().getWidth(), sh.rulerHeight, 0, false).name;
    const int x = juce::roundToInt(names.x) - sh.padding / 2;
    snapToggle_.setBounds(x, loopStripHeight(), snapToggle_.preferredWidth(), sh.rulerHeight);
    syncGrid(); // the step follows the width too, and places its chip
}

// ---- per frame -----------------------------------------------------------------------------

void ArrangementView::frame(double dt, double playheadSeconds)
{
    placeHeaderControls(); // follows scrolling and row heights (moves only what changed)
    if (timeline_.advance(dt) || timeline_.isAnimating())
        repaint();
    syncGrid(); // set by an assistant, a project opened; the step follows the zoom

    // Lane headers show their parameter's value now (it moves while automation plays).
    const auto header = headerArea();
    for (int i = 0; i < layout_.trackCount(); ++i)
        if (const auto* lane = rowLane(i))
        {
            const auto bounds = rowBounds(i, header);
            if (bounds.getBottom() < static_cast<float>(header.getY()) || bounds.getY() > static_cast<float>(header.getBottom()))
                continue;
            const auto value = session_.laneValue(lane->id);
            auto text = value ? session_.laneText(lane->id, *value) : std::string();
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

    // Bounces in progress draw their progress on their clips (bounce.md §8).
    auto bouncing = session_.bouncing();
    if (!bouncing.empty() || !bouncing_.empty())
        repaint(laneArea());
    bouncing_ = std::move(bouncing);

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
        if (!scrollTarget_ && (px > (timeline_.width() - coveredRight_) * 0.88 || px < 0.0))
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
    playheadTracker_.update(*this, timeline_, laneArea().getUnion(songLaneArea()), playhead_);
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
        heights_[rows_[static_cast<size_t>(i)].key()] = layout_.baseHeight(i);
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
    // Into what's visible: the covered end stretches the span so the song ends before it.
    const double end = std::max(snapshot_->contentEnd(), minimum);
    const double visible = std::max(1.0, timeline_.width() - coveredRight_);
    timeline_.animateZoomTo(0.0, end * timeline_.width() / visible);
    repaint();
}

void ArrangementView::setCoveredRight(int pixels)
{
    coveredRight_ = std::max(0, pixels);
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

bool ArrangementView::chooseTrack(std::uint64_t channel, juce::ModifierKeys mods,
                                  const std::vector<std::uint64_t>& shown)
{
    if (mods.isCommandDown() && !mods.isPopupMenu())
    {
        setSelection(model::withTrackToggled(*snapshot_, selection_, channel));
        return false;
    }
    if (mods.isShiftDown() && !mods.isPopupMenu())
    {
        setSelection(model::withTrackRun(*snapshot_, selection_, channel, shown));
        return false;
    }
    // One of several: kept, so a drag or a right-click acts on them all.
    if (!selection_.hasTime() && selection_.rowTracks.contains(channel))
    {
        setSelection({.channel = channel, .rowTracks = selection_.rowTracks});
        return true;
    }
    selectTrack(channel);
    return false;
}

std::vector<std::uint64_t> ArrangementView::shownTracks() const
{
    std::vector<std::uint64_t> out;
    for (const auto& r : rows_)
        if (r.holdsClips() || r.isGroup())
            out.push_back(r.channel);
    return out;
}

void ArrangementView::selectLane(std::uint64_t lane)
{
    setSelection(model::withLane(*snapshot_, lane));
    laneToReveal_ = lane;
    if (const int row = model::rowOfLane(rows_, lane); row >= 0)
    {
        revealRow(row);
        laneToReveal_.reset();
    }
}

void ArrangementView::useTakeAt(std::uint64_t trackId, std::uint64_t take, double seconds)
{
    const auto* track = snapshot_->channel(trackId);
    if (track == nullptr)
        return;
    for (const auto& stack : track->stacks)
        if (stack.take(take) != nullptr)
        {
            if (model::sections(stack, track->clips).size() <= 1)
                session_.useTake(trackId, take);
            else if (const auto section = model::sectionAt(stack, track->clips, seconds))
                session_.compTake(trackId, take, section->first, section->second, "Use take");
            return;
        }
}

void ArrangementView::selectTake(std::uint64_t track, std::uint64_t take, bool add)
{
    auto takes = add && selection_.channel == track && selection_.clips.empty() ? selection_.takes : std::set<std::uint64_t>{};
    if (add && takes.contains(take))
        takes.erase(take);
    else
        takes.insert(take);
    if (takes.empty())
        selectTrack(track);
    else
        setSelection({.channel = track, .takes = std::move(takes)});
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
    if (fadeDrag_ &&
        (fadeDrag_->edit ||
         fadeDrag_->handle.kind == model::FadeHandle::Kind::Gain)) // what it's changing (fades.md §3)
        ui::paintValueTag(g, fadeDrag_->pointer, laneArea(), fadeDragText());
    if (const auto text = clipDragText(); text.isNotEmpty()) // what a drag makes of a loop (clip-loops.md §5)
        ui::paintValueTag(g, lastDrag_, laneArea(), text);
    paintHeaders(g);
    paintSongRow(g);
    paintRuler(g);

    // Corner above the headers.
    g.setColour(t.surface1);
    g.fillRect(getLocalBounds().removeFromTop(rulerHeight()).removeFromLeft(headerWidth()));
    // Where a dragged header lands (tracks-and-clips.md §6): a line across the rows.
    if (rowDrag_ && rowDrag_->moving)
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(getLocalBounds().withTrimmedTop(rulerHeight()));
        const auto& drop = rowDrag_->drop;
        // Into a group (groups.md §5.3): its row lit as a drop target, whether the header was
        // dropped on it or between its rows.
        for (int r = 0; r < static_cast<int>(rows_.size()) && drop.group != 0; ++r)
            if (rows_[static_cast<size_t>(r)].isGroup() &&
                rows_[static_cast<size_t>(r)].channel == drop.group)
            {
                g.setColour(t.roles.dropTarget);
                g.fillRect(rowBounds(r, getLocalBounds().withTop(laneArea().getY())).withTrimmedBottom(1.0f));
            }
        // Where it lands: a line from the depth it lands at, indented as its header would be.
        const auto y = boundaryY(drop.boundary);
        const auto x =
            static_cast<float>(model::layoutTrackHeader(headerWidth(), model::TrackLayout::defaultHeight, 0,
                                                        false, {}, drop.depth)
                                   .colour.x);
        g.setColour(t.active);
        g.fillRect(juce::Rectangle<float>(x, y - static_cast<float>(t.shell.dropLine) * 0.5f,
                                          static_cast<float>(getWidth()) - x,
                                          static_cast<float>(t.shell.dropLine)));
    }
    paintSectionDrop(g);
    lastPaintMs_ = juce::Time::getMillisecondCounterHiRes() - start;
}

void ArrangementView::paintRuler(juce::Graphics& g)
{
    auto area = rulerArea();
    time::paintLoopStrip(g, timeline_, area.removeFromTop(loopStripHeight()), loopPreview_.value_or(loop_), looping_);
    time::RulerOptions options{.playhead = playhead_};
    if (selection_.hasTime()) // its stretch, a band (time-selection.md §7)
        options.selection = std::pair{selection_.time->start, selection_.time->end};
    time::paintRuler(g, grid_, timeline_, area, options);
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
        auto row = rowBounds(i, area);
        if (row.getBottom() < static_cast<float>(area.getY()) || row.getY() > static_cast<float>(area.getBottom()))
            continue;
        const bool selected = rowSelected(rows_[static_cast<size_t>(i)]);
        auto at = [&row](const model::HeaderRect& r) {
            return juce::Rectangle<float>(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
                                          static_cast<float>(r.h))
                .translated(row.getX(), row.getY());
        };
        if (const auto* lane = rowLane(i))
        {
            // An automation lane (automation.md §3): the track header kit, with its own colour, its
            // name, what it drives and its value now.
            g.setColour(selected ? t.surface2 : t.roles.subLane);
            g.fillRect(row);
            const int depth = rows_[static_cast<size_t>(i)].depth;
            const auto slots = model::layoutTrackHeader(row.getWidth(), row.getHeight(), 1, false, {}, depth);
            g.setColour(t.clipColour(lane->colour));
            g.fillRect(at(slots.colour));
            paintGroupBars(g, i, row, slots);
            g.setColour(t.line);
            g.fillRect(juce::Rectangle<float>(row).removeFromBottom(1.0f));
            g.setColour(t.textPrimary);
            g.setFont(ui::font(ui::TextStyle::Name));
            g.drawText(juce::String::fromUTF8(lane->displayName().c_str()), at(slots.name), juce::Justification::centredLeft,
                       true);
            if (slots.subtitle.isEmpty())
                continue;
            auto subtitle = at(slots.subtitle);
            if (const auto it = laneTexts_.find(rows_[static_cast<size_t>(i)].key()); it != laneTexts_.end() && !it->second.empty())
            {
                g.setColour(t.textSecondary);
                g.setFont(ui::font(ui::TextStyle::Value));
                g.drawText(juce::String::fromUTF8(it->second.c_str()), subtitle, juce::Justification::centredRight, false);
            }
            const auto& as = lane->assignments;
            auto drives = as.empty()      ? juce::String("Not assigned")
                          : as.size() == 1 ? juce::String::fromUTF8((as[0].channelName + " \xC2\xB7 " + as[0].module).c_str())
                                           : juce::String(static_cast<int>(as.size())) + " parameters";
            if (lane->rules().stepped()) // it holds each value, it never curves
                drives += juce::String::fromUTF8(" \xC2\xB7 Steps");
            g.setColour(as.empty() ? t.textTertiary : t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(drives, subtitle.withTrimmedRight(60.0f), juce::Justification::centredLeft, true);
            continue;
        }
        const auto* channel = rowChannel(i);
        if (channel == nullptr)
            continue;
        const auto& track = *channel;
        const bool master = track.role == model::TrackInfo::Role::Master;
        if (selected)
        {
            g.setColour(t.surface2);
            g.fillRect(row);
        }
        // The slots of the header (docs/design/ui-foundations.md, "Track headers"); the mixer
        // (C3) and recording (C5) add their buttons, controls and meter to it.
        // Its own height: open takes grow the row below it (ADR 0032), and the colour runs down it.
        const int depth = rows_[static_cast<size_t>(i)].depth;
        const auto slots = model::layoutTrackHeader(row.getWidth(), layout_.baseHeight(i), buttonsOf(track),
                                                    true, {}, depth, track.isGroup());
        const auto bars =
            model::layoutTrackHeader(row.getWidth(), row.getHeight(), 0, false, {}, depth, track.isGroup());
        g.setColour(master ? t.textTertiary : t.clipColour(track.colour));
        g.fillRect(at(bars.colour));
        paintGroupBars(g, i, row, bars); // the groups it's in; a group's fold triangle, beside its name
        g.setColour(t.line);
        if (i == mixRow)
            g.fillRect(row.withHeight(2.0f)); // the divider above the returns and the master
        g.fillRect(juce::Rectangle<float>(row).removeFromBottom(1.0f)); // the row separator, continued across the lanes

        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::Name));
        g.drawText(juce::String::fromUTF8(track.name.c_str()), at(slots.name), juce::Justification::centredLeft, true);
        if (slots.subtitle.isEmpty())
            continue;
        if (track.isGroup()) // how many tracks it holds (groups.md §6.2)
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(juce::String(model::tracksText(model::tracksIn(*snapshot_, track.id))),
                       at(slots.subtitle), juce::Justification::centredLeft, true);
            continue;
        }
        if (!track.holdsClips())
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(master ? "Master" : "Return", at(slots.subtitle), juce::Justification::centredLeft, true);
            continue;
        }
        // One kind of track (ADR 0019): show the instrument, or that MIDI has nothing to play it.
        const bool hasMidi = std::any_of(track.clips.begin(), track.clips.end(),
                                         [](const auto& c) { return c.kind == model::ClipInfo::Kind::Midi; });
        auto label = !track.instrument.empty() ? juce::String::fromUTF8(track.instrument.c_str())
                     : hasMidi                 ? juce::String("No instrument")
                                               : juce::String();
        // Armed by its button: what it records from (recording.md §3).
        if (track.input.armed)
        {
            const auto input = juce::String::fromUTF8(model::inputSummary(track.input, !track.instrument.empty()).c_str());
            label = label.isEmpty() ? input : input + juce::String::fromUTF8(" \xC2\xB7 ") + label;
        }
        g.setColour(track.instrument.empty() && hasMidi ? t.roles.warning : t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(label, at(slots.subtitle), juce::Justification::centredLeft, true);
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
        g.setColour(rowSelected(r)                  ? t.roles.laneSelected
                    : r.holdsClips() || r.isGroup() ? t.roles.lane
                                                    : t.roles.subLane);
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
        if (r.isGroup() && rowVisible(i)) // its members in miniature (groups.md §6.2)
            paintGroupLane(g, i, area);
        if (!r.holdsClips() || !rowVisible(i))
            continue;
        const auto& track = shownTrack(snapshot_->tracks[static_cast<size_t>(r.track)]);
        const float y = static_cast<float>(area.getY() + layout_.top(i));
        const float h = static_cast<float>(layout_.baseHeight(i)); // open takes below (ADR 0032)
        for (const auto& group : model::clipGroups(track)) // a clip's parts draw as one clip
            if (std::none_of(group.parts.begin(), group.parts.end(), [&](const auto* c) { return moving.contains(c->id); }))
                paintGroup(g, group, track, y, h, visible);
        if (joinDrag_ && joinDrag_->moved && joinDrag_->track == track.id)
        {
            // Where the line between two takes is going: in the body, as the line it moves.
            const auto& theme = ui::Theme::current();
            const auto box = clipBox(joinDrag_->join.from, joinDrag_->join.to, y, h);
            const auto title = titleOf(box);
            g.setColour(theme.textPrimary);
            g.fillRect(std::round(laneX(joinDrag_->to)) - static_cast<float>(theme.shell.clipGap), title.getBottom(),
                       static_cast<float>(theme.shell.clipGap), box.getBottom() - title.getBottom());
        }
        if (preview_.empty()) // fades and their handles stay put while clips are dragged
        {
            paintCrossfades(g, track, y, h);
            paintFadeHandles(g, track, y, h);
        }
        paintStrips(g, i, track, area, visible);
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
        if (e.loop) // a loop switched on or changed by the drag (ADR 0048)
            clip.loop = *e.loop;
        paintClip(g, clip, static_cast<float>(area.getY() + layout_.top(row)), static_cast<float>(layout_.baseHeight(row)), visible,
                  {.selected = true});
    }

    // Recordings in progress (recording.md §5): a clip growing to the playhead in the record colour,
    // its waveform drawn as it arrives.
    for (const auto& rec : session_.liveRecordings())
    {
        int index = -1;
        for (size_t i = 0; i < snapshot_->tracks.size(); ++i)
            if (snapshot_->tracks[i].id == rec.track)
                index = static_cast<int>(i);
        const int row = model::rowOfTrack(rows_, index);
        if (row < 0 || !rowVisible(row) || playhead_ <= rec.start)
            continue;
        const auto box = clipBox(rec.start, playhead_, static_cast<float>(area.getY() + layout_.top(row)),
                                 static_cast<float>(layout_.baseHeight(row)));
        if (box.getWidth() <= 1.0f)
            continue;
        g.setColour(t.clipBody(t.record, false));
        g.fillRect(box);
        const auto title = titleOf(box);
        const float strip = title.getHeight();
        g.setColour(t.record);
        g.fillRect(title);
        if (rec.audio)
        {
            g.setColour(t.clipContent(t.record));
            session_.drawLiveWaveform(g, rec.track,
                                      juce::Rectangle<float>(laneX(rec.start), box.getY() + strip, laneX(playhead_) - laneX(rec.start),
                                                             box.getHeight() - strip)
                                          .toNearestInt(),
                                      playhead_ - rec.start);
        }
        if (titleShowsText(title))
        {
            g.setColour(t.surface0);
            g.setFont(ui::font(ui::TextStyle::Control));
            g.drawText("Recording", title.reduced(static_cast<float>(t.shell.clipTextInset), 0.0f), juce::Justification::centredLeft,
                       true);
        }
    }

    // Automation lanes (C4): the curve the engine plays, over the reserved gradient.
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto* lane = rowLane(i);
        if (lane == nullptr || !rowVisible(i))
            continue;
        const auto key = rows_[static_cast<size_t>(i)].key();
        const bool previewing = lanePreview_ && lanePreview_->first == key;
        // Faded when every assignment is off (a lane without any draws as it is).
        const bool on = lane->assignments.empty() ||
                        std::any_of(lane->assignments.begin(), lane->assignments.end(), [](const auto& a) { return a.on; });
        LanePaint paint{.points = previewing ? &lanePreview_->second.points : &lane->points,
                        .on = on,
                        .colour = t.clipColour(lane->colour)};
        if (previewing)
            paint.selected = lanePreview_->second.selection;
        else if (selection_.lane == lane->id)
            paint.selected = selection_.points;
        if (hoveredPoint_ && hoveredPoint_->first == i)
            paint.hovered = hoveredPoint_->second;
        if (lane->points.empty())
            paint.current = session_.laneValue(lane->id);
        paint.assigned = !lane->assignments.empty();
        paint.hint = selection_.lane == lane->id;
        paintAutomationLane(g, laneGeometry(i), paint, visible);
    }

    // A time selection (time-selection.md §7), over the clips and curves on its rows.
    paintTimeSelection(g, area, visible);

    // A lane's box of points.
    if (drag_ == Drag::Lane && laneGesture_.boxing() && dragMoved_)
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
        const float h = row >= 0 ? static_cast<float>(layout_.baseHeight(row)) : static_cast<float>(model::TrackLayout::defaultHeight);
        const float x = laneX(seconds);
        g.setColour(t.roles.dropTarget);
        g.fillRect(juce::Rectangle<float>(x, y, static_cast<float>(area.getRight()) - x, h - 1.0f));
        g.setColour(t.active);
        g.fillRect(x - 1.0f, y, 2.0f, h - 1.0f);
    }

    time::paintPlayhead(g, timeline_, area, playhead_);
}

juce::Rectangle<float> ArrangementView::clipBox(double start, double end, float y, float h, bool seam) const
{
    // Cut just outside the lanes, so an edge out of sight never draws.
    constexpr float offscreen = 2.0f;
    const auto area = laneArea();
    const auto& s = ui::Theme::current().shell;
    const float x0 = std::max(std::round(laneX(start)), static_cast<float>(area.getX()) - offscreen);
    const float x1 = std::min(std::round(laneX(end)) - (seam ? static_cast<float>(s.clipGap) : 0.0f),
                              static_cast<float>(area.getRight()) + offscreen);
    // Above it the clip gap; below it the gap and the row's separator line.
    return {x0, y + static_cast<float>(s.clipGap), std::max(1.0f, x1 - x0), h - static_cast<float>(2 * s.clipGap + 1)};
}

juce::Rectangle<float> ArrangementView::titleOf(juce::Rectangle<float> box)
{
    const auto& s = ui::Theme::current().shell;
    return box.withHeight(std::min(static_cast<float>(s.clipTitleHeight), box.getHeight() * s.clipTitleShare));
}

bool ArrangementView::titleShowsText(juce::Rectangle<float> title)
{
    // Tall enough for text, and wide enough for more than its insets.
    const auto& s = ui::Theme::current().shell;
    return title.getHeight() >= static_cast<float>(s.clipTitleMinText) && title.getWidth() > static_cast<float>(4 * s.clipTextInset);
}

std::optional<std::pair<std::uint64_t, model::CompJoin>> ArrangementView::joinAt(juce::Point<float> pos) const
{
    const auto lanes = laneArea();
    const auto row = layout_.hit(pos.y - static_cast<float>(lanes.getY()));
    if (!lanes.contains(pos.toInt()) || !row || !rows_[static_cast<size_t>(row->track)].holdsClips() ||
        row->offset >= layout_.baseHeight(row->track))
        return std::nullopt;
    const auto& track = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row->track)].track)];
    const float x = pos.x - static_cast<float>(lanes.getX()), grab = ui::Theme::current().input.edgeGrab;
    const double at = timeline_.timeForX(x);
    const auto join = model::compJoinAt(track, at, timeline_.timeForX(x + grab) - at);
    if (!join)
        return std::nullopt;
    // In the body: the title strip stays one, for selecting and moving the clip.
    const float y = static_cast<float>(lanes.getY() + layout_.top(row->track));
    const auto title = titleOf(clipBox(join->from, join->to, y, static_cast<float>(layout_.baseHeight(row->track))));
    if (pos.y < title.getBottom())
        return std::nullopt;
    return std::pair{track.id, *join};
}

bool ArrangementView::seamAfter(const model::TrackInfo& track, double end, std::uint64_t stack) const
{
    const float x = std::round(laneX(end));
    return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& c) {
        return c.start >= end - 1e-9 && std::round(laneX(c.start)) <= x && (stack == 0 || c.stack != stack);
    });
}

juce::Rectangle<float> ArrangementView::groupBox(const model::ClipGroup& group, const model::TrackInfo& track, float y,
                                                 float h) const
{
    return clipBox(group.start(), group.end(), y, h, seamAfter(track, group.end(), group.parts.front()->stack));
}

std::optional<std::pair<model::Badge, juce::Rectangle<float>>> ArrangementView::badgeOf(const model::ClipGroup& group,
                                                                                         const model::TrackInfo& track,
                                                                                         juce::Rectangle<float> box) const
{
    if (group.stack == nullptr)
        return std::nullopt;
    const auto& s = ui::Theme::current().shell;
    const auto title = titleOf(box);
    if (!titleShowsText(title))
        return std::nullopt;
    // [inset][text][item gap][chevron][inset]; the chevron as tall as the title less an inset.
    const float inset = static_cast<float>(s.clipTextInset), chevron = title.getHeight() - inset;
    auto widthOf = [&](const model::Badge& b) {
        float width = inset + chevron + inset;
        if (!b.text.empty())
            width += juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Control), juce::String(b.text)) +
                     static_cast<float>(s.itemGap);
        return width;
    };
    auto badge = model::badge(*group.stack, track.clips, false);
    if (!badge)
        return std::nullopt;
    // A badge takes at most half of its clip's title: narrower, just the chevron.
    if (widthOf(*badge) * 2.0f > title.getWidth())
        badge = model::badge(*group.stack, track.clips, true);
    const float width = widthOf(*badge);
    // At the title's right end, kept inside the lanes so it stays reachable.
    const float right = std::min(title.getRight(), static_cast<float>(laneArea().getRight()) - inset);
    const auto area = juce::Rectangle<float>(right - width, title.getY(), width, title.getHeight());
    if (area.getX() < title.getX())
        return std::nullopt;
    return std::pair{*badge, area};
}

void ArrangementView::paintGroup(juce::Graphics& g, const model::ClipGroup& group, const model::TrackInfo& track, float y,
                                 float h, juce::Rectangle<int> visible)
{
    const auto& t = ui::Theme::current();
    const bool selected = drawnSelected(track, *group.parts.front());
    const bool seam = seamAfter(track, group.end(), group.parts.front()->stack);
    // While one of its takes is auditioned, the clip isn't what you hear (ADR 0038): dimmed.
    const bool auditioned = group.stack != nullptr && track.audition != 0 && group.stack->take(track.audition) != nullptr;
    // A one-part clip's badge: its loop mark goes left of it.
    const auto ownBadge = group.parts.size() == 1 ? badgeOf(group, track, groupBox(group, track, y, h)) : std::nullopt;
    for (const auto* part : group.parts) // lighter together; one outline, at the end; a seam after the last
        paintClip(g, *part, y, h, visible,
                  {.selected = selected,
                   .dimmed = auditioned,
                   .outline = false,
                   .seam = seam && part == group.parts.back(),
                   .badge = ownBadge ? std::optional(ownBadge->second) : std::nullopt});
    if (group.end() < timeline_.start() || group.start() > timeline_.end())
        return;
    const auto box = groupBox(group, track, y, h);
    if (!box.intersects(visible.toFloat()))
        return;
    const bool muted = std::all_of(group.parts.begin(), group.parts.end(), [](const auto* p) { return p->muted; });
    const bool silent = muted || auditioned; // not what you hear
    const auto colour = silent ? t.clipDimmed(t.clipColour(group.parts.front()->colour)) : t.clipColour(group.parts.front()->colour);
    const auto title = titleOf(box);
    const float inset = static_cast<float>(t.shell.clipTextInset);
    if (group.parts.size() > 1)
    {
        // One clip playing several takes: one title, a line where the take changes, one outline.
        g.setColour(colour);
        g.fillRect(title);
        if (titleShowsText(title))
            paintTitleText(g, title, group.parts.front()->name, muted);
        g.setColour(t.surface0);
        for (size_t i = 1; i < group.parts.size(); ++i) // where a seam would be, but in the body only
            g.fillRect(std::round(laneX(group.parts[i]->start)) - static_cast<float>(t.shell.clipGap), title.getBottom(),
                       static_cast<float>(t.shell.clipGap), box.getBottom() - title.getBottom());
    }
    // The badge: the take it plays and how many, with a chevron (a click shows or hides its
    // takes; right-click, the take menu).
    if (const auto badge = badgeOf(group, track, box))
    {
        const auto& [b, rect] = *badge;
        g.setColour(colour); // over the end of the clip's name
        g.fillRect(rect);
        auto r = rect.reduced(inset, 0.0f);
        const float chevron = rect.getHeight() - inset;
        ui::drawIcon(g, ui::Icon::ChevronDown, r.removeFromRight(chevron).withSizeKeepingCentre(chevron, chevron), t.surface0);
        if (!b.text.empty())
        {
            g.setColour(t.surface0);
            g.setFont(ui::font(ui::TextStyle::Control));
            g.drawText(juce::String(b.text), r.withTrimmedRight(static_cast<float>(t.shell.itemGap)), juce::Justification::centredRight,
                       false);
        }
    }
    if (const auto progress = bounceProgress(group.parts.front()->id))
    {
        ui::paintProgress(g, box, *progress);
        // Cancel, where the progress shows (menu-bar.md §15), on a chip so it reads over the audio;
        // at the right end of what's in view of the clip, so a long one's is never off screen.
        if (const auto inView = box.getIntersection(laneArea().toFloat()), button = cancelButton(inView); inView.contains(button))
        {
            g.setColour(t.surface2);
            g.fillRoundedRectangle(button, t.radiusControl);
            const bool hot = button.contains(getMouseXYRelative().toFloat());
            ui::drawIcon(g, ui::Icon::Close, button, hot ? t.textPrimary : t.textSecondary);
        }
    }
    if (selected) // the outline again, over the badge and the progress
    {
        g.setColour(t.textPrimary);
        g.drawRect(box, static_cast<float>(t.shell.clipOutline));
    }
}

void ArrangementView::paintTitleText(juce::Graphics& g, juce::Rectangle<float> title, const std::string& name, bool muted)
{
    const auto& t = ui::Theme::current();
    auto text = title.reduced(static_cast<float>(t.shell.clipTextInset), 0.0f);
    g.setColour(t.surface0);
    g.setFont(ui::font(ui::TextStyle::Control));
    if (muted)
    {
        // "M", as on a track's mute button, then the name.
        const juce::String mark("M");
        const float width = juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Control), mark);
        g.drawText(mark, text.removeFromLeft(width), juce::Justification::centredLeft, false);
        text.removeFromLeft(static_cast<float>(t.shell.itemGap));
    }
    g.drawText(juce::String::fromUTF8(name.c_str()), text, juce::Justification::centredLeft, true);
}

juce::Rectangle<float> ArrangementView::cancelButton(juce::Rectangle<float> box)
{
    const auto& sh = ui::Theme::current().shell;
    const auto size = static_cast<float>(sh.clipButtonSize);
    const auto inset = static_cast<float>(sh.clipTextInset);
    return {box.getRight() - inset - size, box.getBottom() - static_cast<float>(sh.progressHeight) - inset - size, size, size};
}

bool ArrangementView::cancelAt(juce::Point<float> p) const
{
    if (bouncing_.empty())
        return false;
    const auto lanes = laneArea();
    const auto row = layout_.hit(p.y - static_cast<float>(lanes.getY()));
    if (!row || !rows_[static_cast<size_t>(row->track)].holdsClips() || row->offset >= layout_.baseHeight(row->track))
        return false;
    const auto& track = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row->track)].track)];
    const float y = static_cast<float>(lanes.getY() + layout_.top(row->track));
    const float h = static_cast<float>(layout_.baseHeight(row->track));
    for (const auto& group : model::clipGroups(track))
        if (bounceProgress(group.parts.front()->id))
        {
            const auto inView = groupBox(group, track, y, h).getIntersection(lanes.toFloat());
            if (const auto button = cancelButton(inView); inView.contains(button) && button.contains(p))
                return true;
        }
    return false;
}

std::optional<float> ArrangementView::bounceProgress(std::uint64_t clip) const
{
    for (const auto& b : bouncing_)
        if (b.clip == clip && b.seconds >= ui::Theme::current().motion.progressDelay)
            return b.progress;
    return std::nullopt;
}

void ArrangementView::paintStrips(juce::Graphics& g, int row, const model::TrackInfo& track, juce::Rectangle<int> area,
                                  juce::Rectangle<int> visible)
{
    const auto& t = ui::Theme::current();
    const float strip = static_cast<float>(t.shell.takeStripHeight);
    const float top = static_cast<float>(area.getY() + layout_.top(row) + layout_.baseHeight(row));
    for (const auto& stack : track.stacks)
    {
        if (!stack.open || !model::hasTakesToShow(stack) || stack.end < timeline_.start() || stack.start > timeline_.end())
            continue;
        const float x0 = laneX(stack.start), x1 = laneX(stack.end);
        const auto n = stack.takes.size();
        for (size_t k = 0; k < n; ++k)
        {
            const auto& take = stack.takes[n - 1 - k]; // newest on top
            const auto rect = juce::Rectangle<float>(x0, top + static_cast<float>(k) * strip, x1 - x0, strip);
            if (!rect.intersects(visible.toFloat()))
                continue;
            g.setColour(t.roles.subLane);
            g.fillRect(rect);
            {
                // Its clips: where it has audio, neutral and dim; the parts the clip plays as the clip
                // looks, each outlined in the clip's colour (recording.md §6).
                juce::Graphics::ScopedSaveState saved(g);
                g.reduceClipRegion(rect.getSmallestIntegerContainer());
                auto body = [&](const model::ClipInfo& c, juce::Colour fill, juce::Colour content, juce::Rectangle<int> within) {
                    const auto box = clipBox(c.start, c.end(), rect.getY(), rect.getHeight());
                    if (!box.intersects(within.toFloat()))
                        return;
                    g.setColour(fill);
                    g.fillRect(box);
                    if (c.kind == model::ClipInfo::Kind::Midi)
                        paintNotes(g, c, box, content);
                    else
                        paintWaveform(g, c, box, within, content);
                };
                for (const auto& c : take.clips)
                {
                    const auto colour = t.clipColour(c.colour);
                    body(c, t.roles.takeUnused, t.roles.takeUnusedContent, visible);
                    for (const auto& [from, to] : model::inUse(c, track.clips))
                    {
                        const auto used = clipBox(from, to, rect.getY(), rect.getHeight());
                        {
                            juce::Graphics::ScopedSaveState inner(g);
                            g.reduceClipRegion(used.getSmallestIntegerContainer());
                            body(c, t.clipBody(colour, false), t.clipContent(colour), visible.getIntersection(used.getSmallestIntegerContainer()));
                        }
                        g.setColour(colour);
                        g.drawRect(used, static_cast<float>(t.shell.clipOutline));
                    }
                }
                // Auditioned (ADR 0038): all of its audio is what you hear.
                if (track.audition == take.id)
                    for (const auto& c : take.clips)
                        body(c, t.clipBody(t.clipColour(c.colour), false), t.clipContent(t.clipColour(c.colour)), visible);
                // The stretch being swiped (recording.md §7): what it will use, in full colour and
                // outlined, until the release comps it in.
                if (swipe_ && swipe_->swiping && swipe_->take == take.id)
                    for (const auto& piece : model::swipePieces(stack, take.id, swipe_->from, swipe_->to))
                        for (const auto& c : take.clips)
                            if (c.id == piece.takeClip)
                            {
                                const auto box = clipBox(piece.from, piece.to, rect.getY(), rect.getHeight());
                                {
                                    juce::Graphics::ScopedSaveState inner(g);
                                    g.reduceClipRegion(box.getSmallestIntegerContainer());
                                    body(c, t.clipBody(t.clipColour(c.colour), false), t.clipContent(t.clipColour(c.colour)),
                                         visible.getIntersection(box.getSmallestIntegerContainer()));
                                }
                                g.setColour(t.textPrimary);
                                g.drawRect(box, static_cast<float>(t.shell.clipOutline));
                            }
            }
            // Its audition button and name, on a backing that keeps them legible.
            const auto name = juce::String::fromUTF8(take.name.c_str());
            const auto chip = stripChip(rect, stack, name);
            const bool auditioned = track.audition == take.id;
            g.setColour(t.roles.subLane);
            g.fillRoundedRectangle(chip.backing, t.radiusControl);
            const auto text = model::isUsed(take, track.clips) ? t.textPrimary : t.textSecondary; // bright while the clip plays some of it
            ui::drawIcon(g, ui::Icon::Audition, chip.button, auditioned ? t.active : text);
            g.setColour(text);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(name, chip.name, juce::Justification::centredLeft, true);
            g.setColour(t.line);
            g.fillRect(rect.withTop(rect.getBottom() - 1.0f));
            if (auditioned || selection_.takes.contains(take.id))
            {
                g.setColour(auditioned ? t.active : t.textPrimary); // auditioned: what you hear
                g.drawRect(rect, static_cast<float>(t.shell.clipOutline));
            }
        }
    }
}

juce::Rectangle<float> ArrangementView::stripLabel(juce::Rectangle<float> strip) const
{
    // The lane's text inset from the left of what's in sight, the strip's full height.
    auto label = strip;
    label.setLeft(std::max(strip.getX(), static_cast<float>(laneArea().getX())) +
                  static_cast<float>(ui::Theme::current().shell.laneTextInset));
    return label;
}

ArrangementView::StripChip ArrangementView::stripChip(juce::Rectangle<float> strip, const model::TakeStackInfo& stack,
                                                     const juce::String& name) const
{
    const auto& s = ui::Theme::current().shell;
    const auto label = stripLabel(strip);
    const auto clips = clipBox(stack.start, stack.end, strip.getY(), strip.getHeight());
    const float icon = static_cast<float>(s.takeIconSize), inset = static_cast<float>(s.clipTextInset);
    StripChip chip;
    chip.button = juce::Rectangle<float>(label.getX(), clips.getCentreY() - icon * 0.5f, icon, icon);
    chip.name = label.withTrimmedLeft(icon + static_cast<float>(s.itemGap));
    const float nameWidth = std::min(juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Caption), name),
                                     std::max(0.0f, chip.name.getWidth()));
    // Never past the strip.
    const float right = std::min(chip.name.getX() + nameWidth, label.getRight());
    chip.backing = juce::Rectangle<float>(label.getX(), clips.getY(), std::max(icon, right - label.getX()), clips.getHeight())
                       .expanded(inset, 0.0f);
    chip.hit = chip.backing.withWidth(inset + icon + static_cast<float>(s.itemGap) * 0.5f);
    return chip;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> ArrangementView::auditionButtonAt(juce::Point<float> p) const
{
    const auto at = stripAt(p);
    if (!at)
        return std::nullopt;
    const auto lanes = laneArea();
    const auto row = layout_.hit(p.y - static_cast<float>(lanes.getY()));
    const auto* track = snapshot_->channel(at->track);
    const auto* stack = track != nullptr ? track->stack(at->hit.stack) : nullptr;
    if (!row || stack == nullptr)
        return std::nullopt;
    const float strip = static_cast<float>(ui::Theme::current().shell.takeStripHeight);
    const float top = static_cast<float>(lanes.getY() + layout_.top(row->track) + layout_.baseHeight(row->track));
    const auto n = stack->takes.size();
    for (size_t k = 0; k < n; ++k)
        if (const auto& take = stack->takes[n - 1 - k]; take.id == at->hit.take)
        {
            const auto rect = juce::Rectangle<float>(laneX(stack->start), top + static_cast<float>(k) * strip,
                                                     laneX(stack->end) - laneX(stack->start), strip);
            if (stripChip(rect, *stack, juce::String::fromUTF8(take.name.c_str())).hit.contains(p))
                return std::pair{at->track, take.id};
        }
    return std::nullopt;
}

juce::String ArrangementView::getTooltip()
{
    if (cancelAt(getMouseXYRelative().toFloat()))
        return cancelTooltip;
    if (snapshot_ != nullptr && laneArea().contains(hover_.toInt()))
    {
        const auto lanes = laneArea();
        const auto pos = hover_;
        std::vector<int> rowTracks;
        for (const auto& r : rows_)
            rowTracks.push_back(r.holdsClips() ? r.track : -1);
        if (const auto hit = model::hitClip(*snapshot_, layout_, rowTracks, timeline_, pos.x - static_cast<float>(lanes.getX()),
                                            pos.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab))
            if (const auto loop = loopGestureAt(pos, *hit))
                return loop->kind == model::DragKind::LoopLength ? "Drag to change the loop's length"
                                                                  : "Drag to loop the clip";
    }
    const auto button = auditionButtonAt(getMouseXYRelative().toFloat());
    if (!button)
        return {};
    const auto* track = snapshot_->channel(button->first);
    return track != nullptr && track->audition == button->second ? "Stop auditioning"
                                                                  : "Audition: hear this take in place of the clip";
}

std::optional<ArrangementView::StripAt> ArrangementView::stripAt(juce::Point<float> p) const
{
    const auto lanes = laneArea();
    const auto row = layout_.hit(p.y - static_cast<float>(lanes.getY()));
    if (!row || !rows_[static_cast<size_t>(row->track)].holdsClips())
        return std::nullopt;
    const double below = row->offset - layout_.baseHeight(row->track);
    if (below < 0.0)
        return std::nullopt;
    const auto& track = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row->track)].track)];
    const double strip = ui::Theme::current().shell.takeStripHeight;
    const auto hit = model::hitStrip(track, timeline_.timeForX(p.x - static_cast<float>(lanes.getX())), below, strip);
    if (!hit)
        return std::nullopt;
    return StripAt{.track = track.id, .hit = *hit};
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> ArrangementView::badgeAt(juce::Point<float> p) const
{
    const auto lanes = laneArea();
    const auto row = layout_.hit(p.y - static_cast<float>(lanes.getY()));
    if (!row || !rows_[static_cast<size_t>(row->track)].holdsClips() || row->offset >= layout_.baseHeight(row->track))
        return std::nullopt;
    const auto& track = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row->track)].track)];
    const float y = static_cast<float>(lanes.getY() + layout_.top(row->track));
    const float h = static_cast<float>(layout_.baseHeight(row->track));
    for (const auto& group : model::clipGroups(track))
        if (const auto badge = badgeOf(group, track, groupBox(group, track, y, h)); badge && badge->second.contains(p))
            return std::pair{track.id, group.stack->id};
    return std::nullopt;
}

void ArrangementView::takeMenu(std::uint64_t trackId, std::uint64_t stackId, std::optional<std::uint64_t> clicked)
{
    const auto* track = snapshot_->channel(trackId);
    const auto* stack = track != nullptr ? track->stack(stackId) : nullptr;
    if (stack == nullptr || stack->takes.empty())
        return;
    const auto playing = model::playingTake(*stack, track->clips);
    // The take the commands act on: the strip clicked, else the one playing (else the newest).
    const auto target = clicked ? *clicked : playing ? stack->takes[*playing].id : stack->takes.back().id;
    const auto* take = stack->take(target);
    if (take == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addItem(stack->open ? "Hide takes" : "Show takes",
                 [this, stackId, open = stack->open] { session_.setTakesOpen(stackId, !open); });
    menu.addSectionHeader("Use for the whole clip"); // a click on a strip uses one section (ADR 0038)
    for (size_t k = stack->takes.size(); k-- > 0;) // newest first
    {
        const auto& option = stack->takes[k];
        menu.addItem(juce::String::fromUTF8(option.name.c_str()), true, playing == k, [this, trackId, id = option.id] { session_.useTake(trackId, id); });
    }
    menu.addSeparator();
    const auto takeName = juce::String::fromUTF8(take->name.c_str());
    menu.addItem("Rename " + takeName + juce::String::fromUTF8("\xE2\x80\xA6"), [this, trackId, target] {
        if (onRenameTake)
            onRenameTake(trackId, target);
    });
    menu.addSeparator();
    auto toTracks = [this, trackId](std::vector<std::uint64_t> takes, bool move) {
        const auto made = session_.takesToTracks(trackId, takes, move);
        if (!made.empty())
        {
            session_.updateSnapshotNow();
            selectTrack(made.front());
        }
    };
    // The selected takes when the clicked one is among several of them; else just it.
    std::vector<std::uint64_t> chosen{target};
    if (selection_.takes.size() > 1 && selection_.takes.contains(target) && selection_.channel == trackId)
        chosen.assign(selection_.takes.begin(), selection_.takes.end());
    const auto what = chosen.size() > 1 ? juce::String(static_cast<int>(chosen.size())) + " takes" : takeName;
    const auto where = chosen.size() > 1 ? juce::String(" to new tracks") : juce::String(" to new track");
    menu.addItem("Copy " + what + where, [toTracks, chosen] { toTracks(chosen, false); });
    menu.addItem("Move " + what + where, [toTracks, chosen] { toTracks(chosen, true); });
    std::vector<std::uint64_t> all;
    for (const auto& k : stack->takes)
        all.push_back(k.id);
    menu.addItem("Spread takes to new tracks", [toTracks, all] { toTracks(all, false); });
    menu.addSeparator();
    menu.addItem("Delete " + what, [this, trackId, chosen] {
        session_.deleteTakes(trackId, chosen);
        if (std::any_of(chosen.begin(), chosen.end(), [this](auto id) { return selection_.takes.contains(id); }))
            selectTrack(trackId);
    });
    menu.addItem("Keep only " + takeName, [this, trackId, target] { session_.keepOnlyTake(trackId, target); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

bool ArrangementView::drawnSelected(const model::TrackInfo& track, const model::ClipInfo& clip) const
{
    if (selection_.clips.contains(clip.id))
        return true;
    return clip.stack != 0 && std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& c) {
               return c.stack == clip.stack && selection_.clips.contains(c.id);
           });
}

void ArrangementView::paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h, juce::Rectangle<int> visible,
                                ClipLook look)
{
    const auto& t = ui::Theme::current();
    if (clip.end() < timeline_.start() || clip.start > timeline_.end())
        return;
    const auto box = clipBox(clip.start, clip.end(), y, h, look.seam);
    if (!box.intersects(visible.toFloat()))
        return;
    const bool selected = look.selected;
    if (look.seam) // the canvas between it and the clip it touches, through the title and the body
    {
        g.setColour(t.surface0);
        g.fillRect(box.withX(box.getRight()).withWidth(static_cast<float>(t.shell.clipGap)));
    }
    const auto colour = look.dimmed || clip.muted ? t.clipDimmed(t.clipColour(clip.colour)) : t.clipColour(clip.colour);
    g.setColour(t.clipBody(colour, selected));
    g.fillRect(box);
    const auto title = titleOf(box);
    const auto body = box.withTrimmedTop(title.getHeight());
    g.setColour(colour);
    g.fillRect(title);
    // Every lap drawn the same (ADR 0048).
    forEachLap(g, clip, body, [&](const model::ClipInfo& lap, juce::Rectangle<float> lapBody) {
        if (clip.kind == model::ClipInfo::Kind::Midi)
            paintNotes(g, lap, lapBody, t.clipContent(colour));
        else
            paintWaveform(g, lap, lapBody, visible.getIntersection(lapBody.getSmallestIntegerContainer()), t.clipContent(colour));
    });
    if (clip.kind == model::ClipInfo::Kind::Audio)
        paintFades(g, clip, body, colour, selected);
    if (titleShowsText(title))
        paintTitleText(g, title, clip.name, clip.muted);
    paintSeams(g, clip, box, colour);
    // The loop mark (ADR 0048): it plays its loop again and again; before its badge, if any.
    if (const auto mark = clip.loop.on() ? loopMarkOf(box, look.badge) : std::nullopt)
    {
        g.setColour(colour); // over the end of the clip's name
        g.fillRect(*mark);
        const float size = mark->getHeight() - static_cast<float>(t.shell.clipTextInset);
        ui::drawIcon(g, ui::Icon::Loop, mark->withSizeKeepingCentre(size, size), t.surface0);
    }
    if (selected && look.outline)
    {
        g.setColour(t.textPrimary);
        g.drawRect(box, static_cast<float>(t.shell.clipOutline));
    }
}

std::optional<ArrangementView::LoopGesture> ArrangementView::loopGestureAt(juce::Point<float> pos, const model::ClipHit& hit) const
{
    const auto found = model::findClip(*snapshot_, hit.clip);
    const int row = found ? model::rowOfTrack(rows_, found->second) : -1;
    if (!found || row < 0)
        return std::nullopt;
    const auto& clip = *found->first;
    const auto lanes = laneArea();
    const auto box = clipBox(clip.start, clip.end(), static_cast<float>(lanes.getY() + layout_.top(row)),
                             static_cast<float>(layout_.baseHeight(row)));
    const auto title = titleOf(box);
    // A notch: in the title strip, where it's drawn.
    if (clip.loop.on() && pos.y < title.getBottom())
    {
        const double at = timeline_.timeForX(pos.x - static_cast<float>(lanes.getX()));
        if (const auto seam = model::seamAt(clip, at, ui::Theme::current().input.edgeGrab / timeline_.pixelsPerSecond()))
            return LoopGesture{.clip = clip.id, .kind = model::DragKind::LoopLength, .seam = seam->number};
    }
    // The upper part of the right edge of a clip that could loop: the title strip or the edge's
    // upper half, whichever is taller.
    if (hit.zone == model::ClipHit::Zone::EndEdge && !clip.loop.on() && pos.y < std::max(title.getBottom(), box.getCentreY()) &&
        model::whyCantLoop(clip, snapshot_->tracks[static_cast<size_t>(found->second)]).empty())
        return LoopGesture{.clip = clip.id, .kind = model::DragKind::LoopExtend};
    return std::nullopt;
}

juce::String ArrangementView::clipDragText() const
{
    if (drag_ != Drag::Clip || !dragMoved_ || clipDrag_.kind == model::DragKind::Move)
        return {};
    const auto edit = std::find_if(preview_.begin(), preview_.end(), [&](const auto& e) { return e.clip == clipDrag_.anchor; });
    const auto found = model::findClip(*snapshot_, clipDrag_.anchor);
    if (edit == preview_.end() || !found)
        return {};
    auto clip = *found->first;
    clip.start = edit->start;
    clip.length = edit->length;
    clip.offset = edit->offset;
    if (edit->loop)
        clip.loop = *edit->loop;
    if (!clip.loop.on())
        return {};
    const auto& tempo = snapshot_->tempo;
    const auto length = [&](double seconds) { return juce::String(model::lengthText(tempo.beatsForSeconds(seconds), tempo)); };
    if (clipDrag_.kind == model::DragKind::LoopLength)
        return "Loop " + length(clip.loop.length);
    const int laps = model::lapCount(clip);
    return length(clip.length) + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(laps) + (laps == 1 ? " loop" : " loops");
}

void ArrangementView::forEachLap(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                                 const std::function<void(const model::ClipInfo&, juce::Rectangle<float>)>& draw) const
{
    if (!clip.loop.on())
    {
        draw(clip, body);
        return;
    }
    if (clip.loop.length * timeline_.pixelsPerSecond() < 2.0)
        return;
    for (const auto& lap : model::laps(clip, timeline_.start(), timeline_.end()))
    {
        auto plays = clip;
        plays.start = lap.from;
        plays.length = lap.to - lap.from;
        plays.offset = lap.content;
        plays.loop = {};
        const auto lapBody = body.withLeft(std::max(body.getX(), std::round(laneX(lap.from))))
                                 .withRight(std::min(body.getRight(), std::round(laneX(lap.to))));
        if (lapBody.getWidth() <= 0.0f)
            continue;
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(lapBody.getSmallestIntegerContainer());
        draw(plays, lapBody);
    }
}

void ArrangementView::paintSeams(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> box, juce::Colour colour) const
{
    if (!clip.loop.on())
        return;
    const auto& t = ui::Theme::current();
    const auto notch = static_cast<float>(t.shell.loopNotch);
    // Laps closer than two notches' width: too close to tell apart (the loop mark still says it loops).
    if (clip.loop.length * timeline_.pixelsPerSecond() < 4.0 * notch)
        return;
    const auto title = titleOf(box);
    // The name, where it's drawn: no notch cuts into it.
    float nameRight = title.getX();
    if (titleShowsText(title))
    {
        const auto inset = static_cast<float>(t.shell.clipTextInset);
        const auto font = ui::font(ui::TextStyle::Control);
        float width = juce::GlyphArrangement::getStringWidth(font, juce::String::fromUTF8(clip.name.c_str()));
        if (clip.muted)
            width += juce::GlyphArrangement::getStringWidth(font, "M") + static_cast<float>(t.shell.itemGap);
        nameRight = title.getX() + inset + std::min(width, title.getWidth() - 2.0f * inset);
    }
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(box.getSmallestIntegerContainer());
    const auto line = static_cast<float>(t.shell.clipGap);
    for (const auto& seam : model::seams(clip))
    {
        if (seam.at < timeline_.start() || seam.at > timeline_.end())
            continue;
        const float x = std::round(laneX(seam.at));
        // A line through the body in the clip's colour: the clip starting again.
        g.setColour(colour);
        g.fillRect(x, title.getBottom(), line, box.getBottom() - title.getBottom());
        // A notch cut into the title strip, as the canvas between clips (Logic's notches).
        if (x - notch < nameRight + notch)
            continue;
        juce::Path cut;
        cut.addTriangle(x - notch, title.getY(), x + notch, title.getY(), x, title.getY() + notch);
        g.setColour(t.surface0);
        g.fillPath(cut);
    }
}

std::optional<juce::Rectangle<float>> ArrangementView::loopMarkOf(juce::Rectangle<float> box,
                                                                  std::optional<juce::Rectangle<float>> before) const
{
    const auto& s = ui::Theme::current().shell;
    const auto title = titleOf(box);
    if (!titleShowsText(title))
        return std::nullopt;
    // [inset][mark][inset], the mark as tall as the title less an inset (as the badge's chevron).
    const float inset = static_cast<float>(s.clipTextInset), mark = title.getHeight() - inset;
    const float width = inset + mark + inset;
    const float right = before ? before->getX() : std::min(title.getRight(), static_cast<float>(laneArea().getRight()) - inset);
    const auto area = juce::Rectangle<float>(right - width, title.getY(), width, title.getHeight());
    if (width * 2.0f > title.getWidth() || area.getX() < title.getX())
        return std::nullopt;
    return area;
}

void ArrangementView::paintNotes(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                                 juce::Colour content)
{
    // A preview of the notes across the clip's own pitch range (the piano roll edits them), clear of
    // the body's edges by twice the clip gap.
    body = body.reduced(0.0f, static_cast<float>(2 * ui::Theme::current().shell.clipGap));
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
    g.setColour(content);
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
                                    juce::Rectangle<int> visible, juce::Colour content)
{
    const auto* peaks = waveforms_.find(clip.sourcePath);
    if (peaks == nullptr)
        return;
    const auto tint = content;
    // Drawn as heard (audio-editor.md §10): through the clip's gain line, column by column, where it
    // shapes the level; else through its gain, the whole waveform taller or shorter.
    const auto& sound = clip.sound;
    const float zero = model::lineValueForDb(0.0);
    if (std::any_of(sound.line.begin(), sound.line.end(),
                    [zero](const auto& p) { return std::abs(p.value - zero) > 1e-6f; }))
    {
        const auto map = model::contentMap(clip);
        WaveformTiles::drawDirect(
            g, *peaks, clip, body, visible, timeline_, laneArea().getX(), tint, [&](double seconds)
            { return model::soundGainAt(sound, map.fileAt(model::contentAt(clip, seconds))); });
        return;
    }
    const float gain = model::soundGainAt({.gainDb = sound.gainDb}, 0.0);
    if (gain <= 0.0f)
        return;
    // Past four times full scale it's cut at the row anyway: no taller tiles than that.
    const auto drawn = body.withSizeKeepingCentre(body.getWidth(), body.getHeight() * std::min(gain, 4.0f));
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(body.getSmallestIntegerContainer());
    const auto shown = visible.getIntersection(body.getSmallestIntegerContainer());
    // Tiles pay off on the GPU; the software renderer scales images slowly (measured).
    if (juce::OpenGLContext::getCurrentContext() != nullptr)
        tiles_.draw(g, *peaks, clip, drawn,
                    shown.withY(static_cast<int>(drawn.getY()))
                        .withHeight(static_cast<int>(std::ceil(drawn.getHeight()))),
                    timeline_, laneArea().getX(), tint, zoomSettled_);
    else
        WaveformTiles::drawDirect(g, *peaks, clip, drawn,
                                  shown.withY(static_cast<int>(drawn.getY()))
                                      .withHeight(static_cast<int>(std::ceil(drawn.getHeight()))),
                                  timeline_, laneArea().getX(), tint);
}

// ---- input ---------------------------------------------------------------------------------

std::function<double(double)> ArrangementView::snapFunction(bool bypass) const
{
    if (!isSnapEnabled() || bypass)
        return [](double t) { return std::max(0.0, t); };
    return [this](double t) { return timeline_.snap(t, snapshot_->tempo); };
}

void ArrangementView::mouseMove(const juce::MouseEvent& e)
{
    const auto lanes = laneArea();
    hover_ = e.position;
    if (songArea().contains(e.getPosition()))
    {
        songMouseMove(e);
        return;
    }
    juce::MouseCursor cursor = juce::MouseCursor::NormalCursor;
    std::optional<std::pair<int, size_t>> hovered;
    std::optional<std::uint64_t> hoveredClip;
    if (loopStripArea().contains(e.getPosition()))
    {
        const auto zone = loopZoneAt(e.position.x);
        cursor = zone == model::BraceZone::Start || zone == model::BraceZone::End ? juce::MouseCursor::LeftRightResizeCursor
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
        else if (const auto strip = stripAt(e.position))
        {
            if (strip->hit.onClip || auditionButtonAt(e.position)) // a click uses the take, or auditions it
                cursor = juce::MouseCursor::PointingHandCursor;
        }
        else if (badgeAt(e.position) || cancelAt(e.position)) // the clip's take menu; cancelling a bounce
            cursor = juce::MouseCursor::PointingHandCursor;
        else
        {
            std::vector<int> rowTracks;
            for (const auto& r : rows_)
                rowTracks.push_back(r.holdsClips() ? r.track : -1);
            const auto hit = model::hitClip(*snapshot_, layout_, rowTracks, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                                            e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab);
            if (hit)
                cursor = hit->zone == model::ClipHit::Zone::Body ? juce::MouseCursor::NormalCursor
                                                                  : juce::MouseCursor::LeftRightResizeCursor;
            if (const auto loop = hit ? loopGestureAt(e.position, *hit) : std::nullopt) // a notch; looping by the edge
                cursor = loop->kind == model::DragKind::LoopLength ? juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor)
                                                                   : loopCursor_;
            if (joinAt(e.position)) // the line between two takes of a comp moves
                cursor = juce::MouseCursor::LeftRightResizeCursor;
            hoveredClip = hit ? std::optional(hit->clip) : std::nullopt;
            if (hoveredClip != hoveredClip_) // its fade handles show (fades.md §3)
            {
                hoveredClip_ = hoveredClip;
                repaint(lanes);
            }
            using K = model::FadeHandle::Kind;
            if (const auto grip = fadeGripAt(e.position))
                cursor = grip->handle.kind == K::BendIn || grip->handle.kind == K::BendOut ? juce::MouseCursor::UpDownResizeCursor
                                                                                             : juce::MouseCursor::LeftRightResizeCursor;
        }
    }
    if (!hoveredClip && hoveredClip_) // off the clips
    {
        hoveredClip_.reset();
        repaint(lanes);
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
    headerActive_ = false;
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
    if (songArea().contains(pos))
    {
        songMouseDown(e);
        return;
    }
    if (e.mods.isLeftButtonDown() && cancelAt(e.position))
    {
        session_.cancelBounces();
        return;
    }
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();
    if (headerArea().contains(pos))
    {
        // A row's bottom edge: resize it (Alt: every row of its kind).
        if (const auto edge = layout_.hit(e.position.y - static_cast<float>(lanes.getY()) + kResizeGrab);
            edge && edge->offset < 2.0f * kResizeGrab && edge->track > 0 && !e.mods.isPopupMenu())
        {
            drag_ = Drag::Resize;
            resizeRow_ = edge->track - 1;
            resizeFrom_ = layout_.baseHeight(*resizeRow_); // its own height: open takes stay below
            return;
        }
        const auto row = layout_.hit(pos.y - lanes.getY());
        if (!row)
        {
            selectTrack(std::nullopt);
            if (e.mods.isPopupMenu())
                rowMenu(-1);
            return;
        }
        // A group's fold triangle: it folds or unfolds; Alt, every group (groups.md §6.4).
        if (const auto fold = foldArea(row->track); fold && fold->contains(pos) && e.mods.isLeftButtonDown())
        {
            toggleFold(rows_[static_cast<size_t>(row->track)].channel, e.mods.isAltDown());
            return;
        }
        // Ctrl+click with a time selection: the row in or out of it (time-selection.md §6).
        if (e.mods.isCommandDown() && !e.mods.isPopupMenu() && toggleTimeRow(row->track))
            return;
        pendingAlone_.reset();
        const auto* channel = rowChannel(row->track);
        // A header selects its channel alone, or its lane or take (selection.md §2, automation.md
        // §9, recording.md §10).
        const auto* lane = rowLane(row->track);
        if (lane != nullptr)
        {
            selectLane(lane->id);
            if (onLaneActivated)
                onLaneActivated();
        }
        else
        {
            // Alone, or with others: Ctrl adds or takes out, Shift the run (groups.md §8).
            std::optional<std::uint64_t> alone;
            if (channel == nullptr)
                selectTrack(std::nullopt);
            else if (chooseTrack(channel->id, e.mods, shownTracks()))
                alone = channel->id;
            headerActive_ = channel != nullptr; // Delete and Duplicate act on the track (tracks-and-clips.md §5)
            if ((e.mods.isCommandDown() || e.mods.isShiftDown()) && !e.mods.isPopupMenu())
                return; // a choice, not a drag
            pendingAlone_ = alone;
        }
        if (e.mods.isPopupMenu())
        {
            rowMenu(row->track);
            return;
        }
        // A double-click on its name renames it there (§4); a press may start a drag (§6).
        if (e.getNumberOfClicks() >= 2)
        {
            if (const auto name = nameArea(row->track); name && name->contains(pos))
                renameRow(row->track);
            return;
        }
        const bool movable = lane != nullptr || (channel != nullptr && channel->role != model::TrackInfo::Role::Master);
        if (e.mods.isLeftButtonDown() && movable)
        {
            drag_ = Drag::Row;
            rowDrag_ = RowDrag{.row = row->track, .alone = std::exchange(pendingAlone_, std::nullopt)};
        }
        return;
    }
    if (!lanes.contains(pos))
        return;

    auto row = layout_.hit(pos.y - lanes.getY());
    const auto* rowChannelInfo = row ? rowChannel(row->track) : nullptr;
    const auto rowTrack = rowChannelInfo != nullptr ? std::optional(rowChannelInfo->id) : std::nullopt;
    // Inside the time selection, on one of its rows (time-selection.md §6, §8, §10).
    const double pressTime = timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX()));
    const bool inTime = row && selection_.hasTime() && rowInTime(rows_[static_cast<size_t>(row->track)]) &&
                        pressTime >= selection_.time->start && pressTime <= selection_.time->end;
    if (e.mods.isPopupMenu() && inTime && onTimeMenu)
    {
        // Its commands (§8).
        juce::PopupMenu menu;
        onTimeMenu(menu);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
        return;
    }
    if (e.mods.isPopupMenu())
    {
        // A right-click: the row's menu, as on its header (a clip keeps its own handling); below
        // the rows, the items that make rows.
        if (!row)
        {
            rowMenu(-1, true);
            return;
        }
        if (const auto* lane = rowLane(row->track))
        {
            selectLane(lane->id);
            if (onLaneActivated)
                onLaneActivated();
            rowMenu(row->track, true);
            return;
        }
        // A take's strip, or a clip's badge: the clip's take menu (for the selected takes, if it's
        // one of them).
        if (const auto strip = stripAt(e.position))
        {
            if (!selection_.takes.contains(strip->hit.take))
                selectTake(strip->track, strip->hit.take);
            takeMenu(strip->track, strip->hit.stack, strip->hit.take);
            return;
        }
        if (const auto badge = badgeAt(e.position))
        {
            takeMenu(badge->first, badge->second, std::nullopt);
            return;
        }
        std::vector<int> rowTracks;
        for (const auto& r : rows_)
            rowTracks.push_back(r.holdsClips() ? r.track : -1);
        const auto hit = model::hitClip(*snapshot_, layout_, rowTracks, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                                        e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab);
        if (!hit)
        {
            selectTrack(rowTrack);
            rowMenu(row->track, true);
            return;
        }
        // A fade or crossfade: its menu (fades.md §6).
        if (const auto region = fadeRegionAt(e.position))
        {
            fadeMenu(*region);
            return;
        }
        // A clip: its menu, for the selection it's in (or just it).
        if (!selection_.clips.contains(hit->clip))
            setSelection(model::withClips(*snapshot_, {hit->clip}, rowTrack, rowTrack));
        clipMenu();
        return;
    }
    if (row && rowLane(row->track) != nullptr)
    {
        const auto* lane = rowLane(row->track);
        if (e.mods.isCommandDown() && inTime && toggleTimeRow(row->track))
            return;
        if (e.mods.isShiftDown() && selection_.hasTime())
        {
            beginTimeDrag(e, row->track, false, false); // extends it (§6)
            return;
        }
        const auto hover = LaneGesture::hover(gestureLane(row->track), e.position);
        if (e.getNumberOfClicks() == 1 && !hover.point && !hover.segment && !e.mods.isCommandDown())
        {
            // An empty spot: a drag selects time across the rows (§11); a click selects the lane.
            setQuietly(model::withLane(*snapshot_, lane->id));
            beginTimeDrag(e, row->track, false, false);
            return;
        }
        // A point inside the time selection: the lane's points in its stretch are selected with
        // it, so a drag moves them all (§11).
        if (hover.point && inTime)
            if (const auto inside =
                    model::pointsIn(lane->points, selection_.time->start, selection_.time->end);
                inside.contains(*hover.point))
                setQuietly(model::withLane(*snapshot_, lane->id, inside));
        laneMouseDown(e, row->track);
        return;
    }
    if (const auto button = auditionButtonAt(e.position))
    {
        // A take's audition button (ADR 0038): hear it in place of its clip, or stop.
        const auto* track = snapshot_->channel(button->first);
        if (track != nullptr && track->audition == button->second)
            session_.stopAudition();
        else
            session_.auditionTake(button->first, button->second);
        return;
    }
    if (const auto strip = stripAt(e.position))
    {
        // A take's strip (recording.md §6, §7): it selects the take (Ctrl: adds it to the selected
        // takes, and nothing more); on release, a click on the take's clip plays it, and a drag
        // across the strip comps that stretch of it in.
        if (e.mods.isCommandDown())
        {
            selectTake(strip->track, strip->hit.take, true);
            return;
        }
        selectTake(strip->track, strip->hit.take);
        const double time = timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX()));
        const double at = snapped(time, e.mods.isAltDown());
        swipe_ = TakeSwipe{.track = strip->track, .take = strip->hit.take, .onClip = strip->hit.onClip, .at = time, .from = at, .to = at};
        drag_ = Drag::Swipe;
        return;
    }
    if (fadeMouseDown(e)) // a fade's or crossfade's handle
        return;
    if (const auto join = joinAt(e.position); join && e.getNumberOfClicks() == 1)
    {
        // The line between two takes of a comp (recording.md §7): drag it to move where one hands
        // over to the other.
        joinDrag_ = JoinDrag{.track = join->first, .join = join->second, .to = join->second.at};
        drag_ = Drag::Join;
        return;
    }
    if (const auto badge = badgeAt(e.position))
    {
        // A click shows or hides the clip's takes (right-click: its take menu).
        if (const auto* track = snapshot_->channel(badge->first))
            if (const auto* stack = track->stack(badge->second))
                session_.setTakesOpen(stack->id, !stack->open);
        return;
    }
    if (row && layout_.baseHeight(row->track) <= row->offset)
    {
        selectTrack(rowTrack); // the space under a track's clips, beside its open takes
        return;
    }
    if (row && !rows_[static_cast<size_t>(row->track)].holdsClips() &&
        !rows_[static_cast<size_t>(row->track)].isGroup())
    {
        selectTrack(
            rowTrack); // returns and the master hold no clips (a group's lane selects time, groups.md §6.5)
        return;
    }
    std::vector<int> rowTracks;
    for (const auto& r : rows_)
        rowTracks.push_back(r.holdsClips() ? r.track : -1);
    const auto hit = model::hitClip(*snapshot_, layout_, rowTracks, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                                    e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab);
    const auto title = hit ? clipTitleArea(hit->clip) : std::nullopt;
    const bool onTitle = title && title->contains(pos);
    if (row && e.getNumberOfClicks() == 1 && selection_.hasTime())
    {
        // A title strip inside the stretch drags the stretch (§10; Ctrl copies it, and a Ctrl+click
        // without a drag takes the row out); elsewhere Ctrl+click toggles the row, Shift extends.
        if (onTitle && inTime && hit->zone == model::ClipHit::Zone::Body)
        {
            stretchDrag_ = StretchDrag{.stretch = timeSelection(), .track = hit->track, .clip = hit->clip};
            drag_ = Drag::Stretch;
            return;
        }
        if (e.mods.isCommandDown() && inTime && toggleTimeRow(row->track))
            return;
        if (e.mods.isShiftDown())
        {
            beginTimeDrag(e, row->track, false, false);
            return;
        }
    }
    if (e.getNumberOfClicks() >= 2 && row)
    {
        // Double-click: open a clip in the clip view, or make a MIDI clip on an empty spot.
        drag_ = Drag::None;
        if (onTitle)
        {
            renameClips(hit->clip, {}); // on its title strip: its name (§4)
            return;
        }
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
        const auto loop = loopGestureAt(e.position, *hit);
        if (!onTitle && !additive && hit->zone == model::ClipHit::Zone::Body && !loop)
        {
            // Its body: a click selects it, a drag selects time from here (time-selection.md §5).
            clickedInSelection_.reset();
            setQuietly(model::withClips(*snapshot_, {hit->clip}, rowTrack, rowTrack));
            beginTimeDrag(e, row->track, false, true);
            return;
        }
        drag_ = Drag::Clip;
        dragStartTrack_ = hit->track;
        clipDrag_ = {.kind = hit->zone == model::ClipHit::Zone::StartEdge ? model::DragKind::TrimStart
                             : hit->zone == model::ClipHit::Zone::EndEdge  ? model::DragKind::TrimEnd
                                                                           : model::DragKind::Move,
                     .anchor = hit->clip};
        if (loop)
            clipDrag_ = {.kind = loop->kind, .anchor = loop->clip, .seam = loop->seam};
        return;
    }
    // Empty space: a drag selects time across the rows it crosses (time-selection.md §5); a click
    // selects the track. Told at mouseUp, once it's one or the other.
    setQuietly({.channel = rowTrack ? rowTrack : selection_.channel});
    beginTimeDrag(e, row ? row->track : rowAtY(e.position.y), false, false);
}

void ArrangementView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ == Drag::Section || drag_ == Drag::SongResize)
    {
        songMouseDrag(e);
        return;
    }
    if (drag_ == Drag::Row && rowDrag_)
    {
        if (!rowDrag_->moving && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
            return;
        rowDrag_->moving = true;
        rowDrag_->drop =
            model::dropRow(rows_, static_cast<std::size_t>(rowDrag_->row), boundaryAt(e.position.y),
                           dropDepthAt(e.position.x), groupHeaderAt(e.position.y));
        repaint();
        return;
    }
    const auto lanes = laneArea();
    if (drag_ == Drag::Fade)
    {
        fadeMouseDrag(e);
        return;
    }
    if (drag_ == Drag::Join && joinDrag_)
    {
        const double at = snapped(timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX())), e.mods.isAltDown());
        joinDrag_->to = std::clamp(at, joinDrag_->join.from, joinDrag_->join.to);
        joinDrag_->moved = joinDrag_->moved || e.getDistanceFromDragStart() >= ui::Theme::current().input.dragThreshold;
        repaint();
        return;
    }
    if (drag_ == Drag::Swipe && swipe_)
    {
        swipe_->swiping = swipe_->swiping || e.getDistanceFromDragStart() >= ui::Theme::current().input.dragThreshold;
        swipe_->to = snapped(timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX())), e.mods.isAltDown());
        repaint();
        return;
    }
    if (drag_ == Drag::Ruler)
    {
        rulerGesture_.drag(timeline_, e.position - lastDrag_, e.position.x - static_cast<float>(lanes.getX()));
        lastDrag_ = e.position;
        repaint();
        return;
    }
    if (drag_ == Drag::Resize && resizeRow_)
    {
        // Alt: every row of the same kind (channels, or lanes) takes the height.
        const double height = resizeFrom_ + (e.position.y - dragStart_.y);
        auto kindOf = [this](int i) { return rows_[static_cast<size_t>(i)].isLane() ? 1 : 0; }; // channels, lanes
        const int kind = kindOf(*resizeRow_);
        for (int i = 0; i < layout_.trackCount(); ++i)
            if (i == *resizeRow_ || (e.mods.isAltDown() && kindOf(i) == kind))
            {
                layout_.setHeight(i, height);
                heights_[rows_[static_cast<size_t>(i)].key()] = layout_.baseHeight(i);
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
    if (drag_ == Drag::Time)
    {
        timeDragTo(e);
        return;
    }
    if (drag_ == Drag::Stretch)
    {
        stretchDragTo(e);
        return;
    }
    if (drag_ == Drag::Loop)
    {
        const auto snapFn = snapFunction(e.mods.isAltDown());
        const auto lx = static_cast<float>(lanes.getX());
        const double now = timeline_.timeForX(e.position.x - lx);
        const double then = timeline_.timeForX(dragStart_.x - lx);
        loopPreview_ = model::dragBrace(loopDrag_, loopAtDragStart_, then, now, snapFn, model::minClipLength);
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
        // Over a group's row the clips stay on the tracks they were last over: it holds none.
        if (const int under = model::clipTrackRank(*snapshot_, trackAt(e.position.y));
            clipDrag_.kind == model::DragKind::Move && under >= 0)
            clipDrag_.deltaTracks = under - model::clipTrackRank(*snapshot_, dragStartTrack_);
        preview_ = model::dragClips(*snapshot_, selection_.clips, clipDrag_, snapFunction(e.mods.isAltDown()));
        repaint();
    }
}

void ArrangementView::mouseExit(const juce::MouseEvent&)
{
    if (hoveredClip_)
    {
        hoveredClip_.reset();
        repaint(laneArea());
    }
}

void ArrangementView::mouseUp(const juce::MouseEvent& e)
{
    if (drag_ == Drag::Section || drag_ == Drag::SongResize)
    {
        songMouseUp(e);
        return;
    }
    if (drag_ == Drag::Row)
    {
        // A header dropped somewhere new: that row alone moves there (one undo step, ADR 0042).
        drag_ = Drag::None;
        const auto dragged = std::exchange(rowDrag_, std::nullopt);
        repaint();
        if (dragged && !dragged->moving && dragged->alone) // a click on one of several: it alone
            selectTrack(dragged->alone);
        if (!dragged || !dragged->moving || !dragged->drop.moves || dragged->row >= static_cast<int>(rows_.size()))
            return;
        const auto& r = rows_[static_cast<std::size_t>(dragged->row)];
        const auto into = dragged->drop.group; // the group it lands in (groups.md §5.3)
        // One inside a group that's chosen too goes with it.
        const auto several = model::outermost(*snapshot_, model::selectedTracks(*snapshot_, selection_));
        if (r.kind == model::ArrangementRow::Kind::Lane)
            session_.moveLane(r.lane, dragged->drop.before, into);
        else if (several.size() > 1 && std::find(several.begin(), several.end(), r.channel) != several.end())
        {
            // Several chosen together move together, in their order, landing where it's dropped
            // (groups.md §8): one step.
            const auto among = [&](std::uint64_t id)
            { return std::find(several.begin(), several.end(), id) != several.end(); };
            auto before = dragged->drop.before;
            if (before != 0 && among(before))
            {
                // Dropped among them: before the first row after the drop that isn't one of them.
                before = 0;
                for (auto b = dragged->drop.boundary;
                     b < rows_.size() && rows_[b].kind != model::ArrangementRow::Kind::Return; ++b)
                    if (const auto id = rows_[b].isLane() ? rows_[b].lane : rows_[b].channel; !among(id))
                    {
                        before = id;
                        break;
                    }
            }
            const engine::Session::EditGroup step(session_, "Move tracks");
            for (const auto id : several)
                session_.moveTrack(id, before, into);
        }
        else
            session_.moveTrack(r.channel, dragged->drop.before, into);
        return;
    }
    if (drag_ == Drag::Ruler && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
    {
        // A click in the ruler moves the playhead (snapped; Alt for free).
        session_.setPosition(time::rulerTime(timeline_, snapshot_->tempo,
                                             e.position.x - static_cast<float>(laneArea().getX()),
                                             !isSnapEnabled() || e.mods.isAltDown()));
    }
    if (drag_ == Drag::Lane)
        laneMouseUp(e);
    if (drag_ == Drag::Time)
        endTimeDrag(e);
    if (drag_ == Drag::Stretch)
    {
        if (!dragMoved_ && e.mods.isCommandDown() && stretchDrag_) // a Ctrl+click: the row out (§6)
        {
            const auto track = stretchDrag_->track;
            stretchDrag_.reset();
            stretchMove_.reset();
            toggleTimeRow(model::rowOfTrack(rows_, track));
        }
        else
            endStretchDrag();
    }
    if (drag_ == Drag::Fade)
        fadeMouseUp();
    if (drag_ == Drag::Join && joinDrag_)
    {
        // The take on the side the line moved away from plays up to its new place (one undo step).
        const auto& j = joinDrag_->join;
        if (joinDrag_->moved && joinDrag_->to > j.at + 1e-9)
            session_.compTake(joinDrag_->track, j.leftTake, j.at, joinDrag_->to);
        else if (joinDrag_->moved && joinDrag_->to < j.at - 1e-9)
            session_.compTake(joinDrag_->track, j.rightTake, joinDrag_->to, j.at);
        joinDrag_.reset();
    }
    if (drag_ == Drag::Swipe && swipe_)
    {
        if (swipe_->swiping) // comps that stretch in (one undo step)
            session_.compTake(swipe_->track, swipe_->take, swipe_->from, swipe_->to);
        else if (swipe_->onClip) // the take for the section there (ADR 0038)
            useTakeAt(swipe_->track, swipe_->take, swipe_->at);
        swipe_.reset();
    }
    if (drag_ == Drag::Resize)
        resizeRow_.reset();
    if (drag_ == Drag::Loop)
    {
        const auto release = model::releaseBrace(loopDrag_, dragMoved_, loopPreview_);
        if (release.range)
            session_.setLoopRange(release.range->first, release.range->second);
        if (release.switchOn) // drawing a new brace turns looping on
            session_.setLooping(true);
        if (release.toggle) // a click on the brace toggles it
            session_.setLooping(!session_.isLooping());
        loopPreview_.reset();
    }
    if (drag_ == Drag::Clip && dragMoved_ && !preview_.empty())
    {
        // One undo step; the snapshot follows. A loop switched on, or its length, names it.
        const bool looped = std::any_of(preview_.begin(), preview_.end(), [](const auto& edit) { return edit.loop.has_value(); });
        session_.applyClipEdits(preview_, clipDrag_.kind == model::DragKind::LoopLength ? "Loop length"
                                          : looped                                       ? "Loop clips"
                                                                                         : "Move clips");
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
    const auto id = rows_[static_cast<size_t>(row)].lane;
    lanePreview_ = std::pair{rows_[static_cast<size_t>(row)].key(), edit}; // until the snapshot has it
    session_.setLanePoints(id, edit.points, std::nullopt, undoName);
    setQuietly(model::withLane(*snapshot_, id, edit.selection));
    repaint();
}

LaneGesture::Lane ArrangementView::gestureLane(int row) const
{
    const auto* lane = rowLane(row);
    return {.points = &lane->points,
            .rules = lane->rules(),
            .geometry = laneGeometry(row),
            .selection = selection_.lane == lane->id ? selection_.points : std::set<size_t>{},
            .snap = [this](double seconds, bool free) { return snapped(seconds, free); }};
}

void ArrangementView::laneMouseDown(const juce::MouseEvent& e, int row)
{
    const auto* lane = rowLane(row);
    if (lane == nullptr)
        return;
    if (onLaneActivated)
        onLaneActivated();
    const auto id = lane->id;
    drag_ = Drag::Lane;
    laneRow_ = row;
    const auto out = laneGesture_.press(e, gestureLane(row));
    if (out.steps)
        explainSteps(*lane); // nothing to bend: say why, rather than do nothing
    if (e.getNumberOfClicks() >= 2)
    {
        drag_ = Drag::None;
        if (out.keep)
            applyLaneEdit(row, *out.keep, out.undoName);
        notifySelection();
        return;
    }
    if (out.steps)
    {
        drag_ = Drag::None;
        return;
    }
    if (out.select)
        setQuietly(model::withLane(*snapshot_, id, *out.select));
}

void ArrangementView::explainSteps(const model::AutomationLaneInfo& lane)
{
    if (onMessage && !lane.assignments.empty())
        onMessage(juce::String::fromUTF8(lane.assignments.front().name.c_str()) +
                  " has steps: it holds each value until the next point, so its lane can't curve");
}

void ArrangementView::laneMouseDrag(const juce::MouseEvent& e)
{
    const int row = laneRow_;
    if (rowLane(row) == nullptr)
        return;
    const auto out = laneGesture_.drag(e, dragStart_, gestureLane(row));
    if (out.preview)
        lanePreview_ = std::pair{rows_[static_cast<size_t>(row)].key(), *out.preview};
    if (out.select)
        setQuietly({.channel = selection_.channel, .lane = selection_.lane, .points = *out.select});
    repaint();
}

void ArrangementView::laneMouseUp(const juce::MouseEvent&)
{
    const int row = std::exchange(laneRow_, -1);
    if (rowLane(row) == nullptr)
    {
        laneGesture_ = {};
        return;
    }
    const auto out = laneGesture_.release(dragMoved_, gestureLane(row));
    if (out.keep)
        applyLaneEdit(row, *out.keep, out.undoName);
    else if (out.select)
        setQuietly({.channel = selection_.channel, .lane = selection_.lane, .points = *out.select});
}

void ArrangementView::deletePoints()
{
    const auto* lane = selectedLane();
    if (!hasPointSelection() || lane == nullptr)
        return;
    session_.setLanePoints(lane->id, model::deletePoints(lane->points, selection_.points).points, std::nullopt,
                           "Delete points");
    setSelection(model::withLane(*snapshot_, lane->id));
}

void ArrangementView::duplicatePoints()
{
    const auto* lane = selectedLane();
    if (!hasPointSelection() || lane == nullptr)
        return;
    const auto edit = model::duplicatePoints(lane->points, selection_.points, gridStepSeconds(), lane->rules());
    session_.setLanePoints(lane->id, edit.points, std::nullopt, "Duplicate points");
    setSelection(model::withLane(*snapshot_, lane->id, edit.selection));
}

model::AutomationPoints ArrangementView::copyPoints() const
{
    const auto* lane = selectedLane();
    return lane == nullptr ? model::AutomationPoints{} : model::copyPoints(lane->points, selection_.points);
}

bool ArrangementView::pastePoints(const model::AutomationPoints& points, double seconds)
{
    const auto* lane = selectedLane();
    if (lane == nullptr || points.empty())
        return false;
    auto edit = model::pastePoints(lane->points, points, seconds, lane->rules());
    const auto id = lane->id;
    session_.setLanePoints(id, edit.points, std::nullopt, "Paste");
    session_.updateSnapshotNow();
    setSnapshot(session_.snapshot());
    setSelection(model::withLane(*snapshot_, id, std::move(edit.selection)));
    return true;
}

void ArrangementView::selectAllPoints()
{
    const auto* lane = selectedLane();
    if (lane == nullptr)
        return;
    std::set<size_t> all;
    for (size_t i = 0; i < lane->points.size(); ++i)
        all.insert(i);
    setSelection(model::withLane(*snapshot_, lane->id, std::move(all)));
}

void ArrangementView::nudgePoints(int gridSteps, float value)
{
    const auto* lane = selectedLane();
    if (!hasPointSelection() || lane == nullptr)
        return;
    const auto edit =
        model::movePoints(lane->points, selection_.points, gridSteps * gridStepSeconds(), value, lane->rules());
    session_.setLanePoints(lane->id, edit.points, std::nullopt, "Nudge points");
    setSelection(model::withLane(*snapshot_, lane->id, edit.selection));
}

void ArrangementView::removeSelectedLane()
{
    if (!selection_.lane)
        return;
    session_.removeAutomationLane(*selection_.lane);
    selectTrack(selection_.channel);
}

void ArrangementView::clipMenu()
{
    juce::PopupMenu menu;
    if (onClipMenu)
        onClipMenu(menu);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

void ArrangementView::rowMenu(int row, bool fromLanes)
{
    juce::PopupMenu menu;
    if (const auto* lane = rowLane(row))
    {
        const auto id = lane->id;
        const bool on = lane->allOn();
        // As a track's header (tracks-and-clips.md §10): its own commands, then its colour.
        const auto ellipsis = juce::String::fromUTF8("\xE2\x80\xA6");
        menu.addItem("Rename" + ellipsis, [this, id] { renameLane(id); });
        menu.addItem("Assign parameter" + ellipsis, [this, id] {
            if (onAssignLane)
                onAssignLane(id);
        });
        menu.addItem(on ? "Turn lane off" : "Turn lane on", !lane->points.empty() && !lane->assignments.empty(), false,
                     [this, id, on] { session_.setLaneOn(id, !on); });
        menu.addItem("Delete automation lane", [this, id] {
            session_.removeAutomationLane(id);
            if (selection_.lane == id)
                selectTrack(selection_.channel);
        });
        juce::PopupMenu colours;
        ui::addColourGrid(colours, lane->colour, [this, id](int c) { session_.setLaneColour(id, c); });
        menu.addSubMenu("Colour", colours);
    }
    // A track's or return's own items (tracks-and-clips.md §10): the shell's.
    if (const auto* channel = row >= 0 ? rowChannel(row) : nullptr;
        channel != nullptr && channel->role != model::TrackInfo::Role::Master && onHeaderMenu)
        onHeaderMenu(menu, channel->id);
    // Rows are made from any row, or the empty space (automation.md §3, daw-shell.md).
    if (onAddMenu)
    {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        onAddMenu(menu);
    }
    // The grid, where you edit (ADR 0047).
    if (fromLanes)
    {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addSubMenu("Grid", gridMenu());
    }
    if (menu.getNumItems() > 0)
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

void ArrangementView::revealRow(int row)
{
    if (row < 0 || row >= layout_.trackCount())
        return;
    const double top = layout_.top(row), bottom = top + layout_.height(row), view = laneArea().getHeight();
    if (top < 0.0)
        layout_.scrollBy(top);
    else if (bottom > view)
        layout_.scrollBy(std::min(top, bottom - view));
    placeHeaderControls();
    repaint();
}

// ---- edit commands -------------------------------------------------------------------------

void ArrangementView::deleteSelection()
{
    if (!selection_.takes.empty() && selection_.channel && selection_.clips.empty())
    {
        session_.deleteTakes(*selection_.channel, {selection_.takes.begin(), selection_.takes.end()});
        selectTrack(selection_.channel);
        return;
    }
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
    if (isSnapEnabled() && !bypassSnap)
        start = timeline_.snapFloor(start + 1e-9, tempo);
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
    const auto id = session_.createMidiClip(track.id, start, end - start); // no instrument by itself (ADR 0033)
    if (id == 0)
        return 0;
    setSelection({.channel = track.id, .clips = {id}});
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

    // Several files go onto consecutive tracks, groups skipped, creating tracks below as needed;
    // on a group's row, each on a new track at its end (groups.md §11).
    const auto holders = model::clipTracks(*snapshot_);
    const bool onGroup = index >= 0 && index < static_cast<int>(snapshot_->tracks.size()) &&
                         snapshot_->tracks[static_cast<size_t>(index)].isGroup();
    int target = onGroup ? -1 : model::clipTrackRank(*snapshot_, index);
    std::optional<std::uint64_t> first;
    for (const auto& path : files)
    {
        const bool midi = isMidiFile(path);
        if (!isAudioFile(path) && !midi)
            continue;
        std::uint64_t trackId = 0;
        if (target >= 0 && target < static_cast<int>(holders.size()))
            trackId = snapshot_->tracks[static_cast<size_t>(holders[static_cast<size_t>(target)])].id;
        else
            trackId = session_.addTrack({}, onGroup ? snapshot_->tracks[static_cast<size_t>(index)].id : 0);
        const auto clip = midi ? session_.insertMidiFile(trackId, juce::File(path), seconds) // no instrument (ADR 0033)
                               : session_.insertAudioFile(trackId, juce::File(path), seconds);
        if (clip != 0 && !first)
            first = trackId;
        if (target >= 0)
            ++target;
    }
    if (first)
        selectTrack(first);
    repaint();
}

// ---- drops from the browser (browser.md §10) -----------------------------------------------

bool ArrangementView::isInterestedInDragSource(const SourceDetails& details)
{
    return browser::itemOf(details.description).has_value();
}

void ArrangementView::itemDragMove(const SourceDetails& details)
{
    fileDragMove({}, details.localPosition.x, details.localPosition.y);
}

void ArrangementView::itemDragExit(const SourceDetails&)
{
    fileDragExit({});
}

void ArrangementView::itemDropped(const SourceDetails& details)
{
    itemDragMove(details);
    const auto [index, seconds] = *dropTarget_;
    dropTarget_.reset();
    repaint();
    const auto item = browser::itemOf(details.description);
    std::uint64_t track = 0;
    if (index >= 0 && index < static_cast<int>(snapshot_->tracks.size()))
        track = snapshot_->tracks[static_cast<size_t>(index)].id;
    if (item && onBrowserDrop)
        onBrowserDrop(*item, track, seconds);
}
} // namespace fd::app

namespace fd::app
{
// ---- tracks and clips (C11, ADR 0041, docs/design/tracks-and-clips.md) ------------------------

bool ArrangementView::headerActive() const noexcept
{
    return headerActive_ && selection_.channel && selection_.clips.empty() && !selection_.lane && selection_.takes.empty();
}

std::optional<juce::Rectangle<int>> ArrangementView::nameArea(int row) const
{
    return headerSlot(row, [](const model::TrackHeaderSlots& slots) { return slots.name; });
}

std::optional<juce::Rectangle<int>> ArrangementView::foldArea(int row) const
{
    if (row < 0 || row >= layout_.trackCount() || !rows_[static_cast<size_t>(row)].isGroup())
        return std::nullopt;
    return headerSlot(row, [](const model::TrackHeaderSlots& slots) { return slots.fold; });
}

std::optional<juce::Rectangle<int>> ArrangementView::headerSlot(
    int row, const std::function<model::HeaderRect(const model::TrackHeaderSlots&)>& slot) const
{
    if (row < 0 || row >= layout_.trackCount())
        return std::nullopt;
    const auto bounds = rowBounds(row, headerArea());
    const int depth = rows_[static_cast<size_t>(row)].depth;
    model::TrackHeaderSlots slots;
    if (rowLane(row) != nullptr)
        slots = model::layoutTrackHeader(bounds.getWidth(), bounds.getHeight(), 1, false, {}, depth);
    else if (const auto* channel = rowChannel(row))
        slots = model::layoutTrackHeader(bounds.getWidth(), layout_.baseHeight(row), buttonsOf(*channel),
                                         true, {}, depth, channel->isGroup());
    else
        return std::nullopt;
    const auto r = slot(slots);
    if (r.isEmpty())
        return std::nullopt;
    return juce::Rectangle<double>(r.x, r.y, r.w, r.h).translated(bounds.getX(), bounds.getY()).toNearestInt();
}

std::optional<juce::Rectangle<int>> ArrangementView::clipTitleArea(std::uint64_t clip) const
{
    const auto lanes = laneArea();
    for (int i = 0; i < layout_.trackCount(); ++i)
    {
        const auto* track = rows_[static_cast<std::size_t>(i)].holdsClips() ? rowChannel(i) : nullptr;
        if (track == nullptr)
            continue;
        for (const auto& group : model::clipGroups(*track))
            if (std::any_of(group.parts.begin(), group.parts.end(), [clip](const auto* p) { return p->id == clip; }))
            {
                const float y = static_cast<float>(lanes.getY() + layout_.top(i));
                const auto title = titleOf(groupBox(group, *track, y, static_cast<float>(layout_.baseHeight(i))));
                const auto shown = title.getIntersection(lanes.toFloat()).toNearestInt();
                return shown.isEmpty() ? std::nullopt : std::optional(shown);
            }
    }
    return std::nullopt;
}

bool ArrangementView::renameRow(int row)
{
    const auto area = nameArea(row);
    if (!area)
        return false;
    const auto& sh = ui::Theme::current().shell;
    // Over the name, its text where it was drawn (the field insets its text), a row high.
    const auto field = area->withLeft(area->getX() - sh.fieldTextInset).withSizeKeepingCentre(area->getWidth() + sh.fieldTextInset, sh.rowHeight);
    // Tab keeps it and goes on to the next row's name (Shift+Tab: the one before), as in Live.
    auto next = [this, row](int direction) {
        for (int i = row + direction; i >= 0 && i < layout_.trackCount(); i += direction)
            if (const auto* c = rowChannel(i); rowLane(i) != nullptr || (c != nullptr && c->role != model::TrackInfo::Role::Master))
            {
                revealRow(i);
                renameRow(i);
                return;
            }
    };
    if (const auto* lane = rowLane(row))
    {
        const auto id = lane->id;
        names_.show(field, juce::String::fromUTF8(lane->displayName().c_str()), ui::TextStyle::Name,
                    [this, id](const juce::String& name) { session_.renameAutomationLane(id, name.toStdString()); }, next);
        return true;
    }
    const auto* channel = rowChannel(row);
    if (channel == nullptr || channel->role == model::TrackInfo::Role::Master)
        return false;
    const auto id = channel->id;
    names_.show(field, juce::String::fromUTF8(channel->name.c_str()), ui::TextStyle::Name,
                [this, id](const juce::String& name) { session_.renameTrack(id, name.toStdString()); }, next);
    return true;
}

bool ArrangementView::renameChannel(std::uint64_t channel)
{
    for (int i = 0; i < layout_.trackCount(); ++i)
        if (rowLane(i) == nullptr)
            if (const auto* c = rowChannel(i); c != nullptr && c->id == channel)
            {
                revealRow(i);
                return renameRow(i);
            }
    return false;
}

bool ArrangementView::renameLane(std::uint64_t lane)
{
    const int row = model::rowOfLane(rows_, lane);
    if (row < 0)
        return false;
    revealRow(row);
    return renameRow(row);
}

bool ArrangementView::renameClips(std::uint64_t clip, const std::vector<std::uint64_t>& with)
{
    const auto title = clipTitleArea(clip);
    const auto found = model::findClip(*snapshot_, clip);
    if (!title || !found)
        return false;
    // The clip with every part of it (a comp), and the others asked for.
    std::vector<std::uint64_t> ids{clip};
    ids.insert(ids.end(), with.begin(), with.end());
    for (const auto& t : snapshot_->tracks)
        for (const auto& c : t.clips)
            for (const auto id : std::vector<std::uint64_t>(ids))
                if (const auto other = model::findClip(*snapshot_, id);
                    other && other->first->stack != 0 && c.stack == other->first->stack && c.id != id &&
                    std::find(ids.begin(), ids.end(), c.id) == ids.end())
                    ids.push_back(c.id);
    const auto& sh = ui::Theme::current().shell;
    names_.show(title->withHeight(std::max(title->getHeight(), sh.rowHeight)), juce::String::fromUTF8(found->first->name.c_str()),
                ui::TextStyle::Control,
                [this, ids](const juce::String& name) { session_.renameClips(ids, name.toStdString()); });
    return true;
}

bool ArrangementView::renameSelection()
{
    if (!selection_.clips.empty())
    {
        // The earliest selected clip's title, all of them renamed.
        std::vector<std::uint64_t> ids(selection_.clips.begin(), selection_.clips.end());
        std::sort(ids.begin(), ids.end(), [this](auto a, auto b) {
            const auto x = model::findClip(*snapshot_, a), y = model::findClip(*snapshot_, b);
            return x && y ? x->first->start < y->first->start : a < b;
        });
        for (const auto id : ids)
            if (renameClips(id, ids))
                return true;
        return false;
    }
    if (selection_.lane)
        return renameLane(*selection_.lane);
    if (selection_.channel)
        return renameChannel(*selection_.channel);
    return false;
}

float ArrangementView::boundaryY(std::size_t boundary) const
{
    const auto lanes = laneArea();
    const int n = layout_.trackCount();
    if (n == 0)
        return static_cast<float>(lanes.getY());
    if (boundary < static_cast<std::size_t>(n))
        return static_cast<float>(lanes.getY() + layout_.top(static_cast<int>(boundary)));
    return static_cast<float>(lanes.getY() + layout_.top(n - 1) + layout_.height(n - 1));
}

std::size_t ArrangementView::boundaryAt(float y) const
{
    std::size_t best = 0;
    float distance = std::numeric_limits<float>::max();
    for (std::size_t b = 0; b <= static_cast<std::size_t>(layout_.trackCount()); ++b)
        if (const auto d = std::abs(boundaryY(b) - y); d < distance)
        {
            distance = d;
            best = b;
        }
    return best;
}

bool ArrangementView::cancelRowDrag()
{
    if (!rowDrag_)
        return false;
    rowDrag_.reset();
    drag_ = Drag::None;
    repaint();
    return true;
}

std::optional<model::RowDrop> ArrangementView::rowDrop() const
{
    return rowDrag_ && rowDrag_->moving ? std::optional(rowDrag_->drop) : std::nullopt;
}
} // namespace fd::app
