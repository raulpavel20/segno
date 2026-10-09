#include "ui/ArrangementView.h"

#include "model/Takes.h"
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

ArrangementView::TakesButton::TakesButton() : juce::Button("Takes")
{
    setTooltip("Show or hide the track's take lanes (Ctrl+Alt+K)");
}

void ArrangementView::TakesButton::update(const model::TrackInfo& track)
{
    const auto text = juce::String::fromUTF8("Takes \xC2\xB7 ") + juce::String(static_cast<int>(track.takes.size()));
    if (text != text_ || getToggleState() != track.showTakes)
    {
        text_ = text;
        setToggleState(track.showTakes, juce::dontSendNotification);
        repaint();
    }
}

int ArrangementView::TakesButton::preferredWidth() const
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Caption), text_)));
}

void ArrangementView::TakesButton::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    const auto& t = ui::Theme::current();
    g.setColour(getToggleState() ? t.active : highlighted ? t.textPrimary : t.textSecondary);
    g.setFont(ui::font(ui::TextStyle::Caption));
    g.drawText(text_, getLocalBounds(), juce::Justification::centredRight, false);
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
    {
        for (const auto& c : t.clips)
            waveforms_.request(c.sourcePath);
        for (const auto& take : t.takes)
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
    std::set<std::string> lanes, stars;
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
        if (const auto* take = rowTake(i))
        {
            // A take lane's star (its favourites, recording.md §6).
            const auto key = rows_[static_cast<size_t>(i)].key();
            stars.insert(key);
            auto& star = takeStars_[key];
            if (star == nullptr)
            {
                star = std::make_unique<ui::IconButton>(ui::Icon::Star, "Favourite");
                star->setIconShowsState(true);
                star->setTooltip("Star: mark the take as a favourite");
                headerLayer_.addChildComponent(*star);
            }
            star->setToggleState(take->star, juce::dontSendNotification);
            star->onClick = [this, track = rowTakeTrack(i).id, id = take->id, on = take->star] {
                session_.setTakeStar(track, id, !on);
            };
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
            {
                headerLayer_.addChildComponent(controls->arm);
                headerLayer_.addChildComponent(controls->takes);
            }
            headerLayer_.addChildComponent(controls->volume);
            headerLayer_.addChildComponent(controls->pan);
            headerLayer_.addChildComponent(controls->meter);
        }
        controls->mute.update(channel->mix.mute);
        controls->solo.update(channel->mix.solo);
        controls->arm.updateArm(channel->input);
        controls->takes.update(*channel);
        controls->takes.onClick = [this, id = channel->id, show = !channel->showTakes] { session_.showTakes(id, show); };
        controls->volume.update(*channel);
        controls->pan.update(*channel);
    }
    for (auto it = headerControls_.begin(); it != headerControls_.end();)
        it = shown.contains(it->first) ? std::next(it) : headerControls_.erase(it);
    for (auto it = laneControls_.begin(); it != laneControls_.end();)
        it = lanes.contains(it->first) ? std::next(it) : laneControls_.erase(it);
    for (auto it = takeStars_.begin(); it != takeStars_.end();)
        it = stars.contains(it->first) ? std::next(it) : takeStars_.erase(it);
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
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(row)].isLane() ||
        rows_[static_cast<size_t>(row)].isTake())
        return nullptr;
    return snapshot_->channel(rows_[static_cast<size_t>(row)].channel);
}

const model::TakeInfo* ArrangementView::rowTake(int row) const noexcept
{
    if (row < 0 || row >= static_cast<int>(rows_.size()) || !rows_[static_cast<size_t>(row)].isTake())
        return nullptr;
    for (const auto& take : rowTakeTrack(row).takes)
        if (take.id == rows_[static_cast<size_t>(row)].take)
            return &take;
    return nullptr;
}

const model::TrackInfo& ArrangementView::rowTakeTrack(int row) const noexcept
{
    return snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row)].track)];
}

const model::ClipInfo* ArrangementView::takeClipAt(int row, float x) const
{
    const auto* take = rowTake(row);
    if (take == nullptr)
        return nullptr;
    const double seconds = timeline_.timeForX(x - static_cast<float>(laneArea().getX()));
    for (const auto& c : take->clips)
        if (seconds >= c.start && seconds < c.end())
            return &c;
    return nullptr;
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

bool ArrangementView::rowSelected(const model::ArrangementRow& row) const noexcept
{
    if (row.isLane())
        return selection_.lane == row.lane;
    if (row.isTake())
        return selection_.take == row.take;
    return !selection_.lane && !selection_.take && selection_.channel == row.channel;
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
    if (!visible && selection_.channel)
        if (const auto* channel = snapshot_->channel(*selection_.channel); channel != nullptr && !channel->holdsClips())
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
    rows_ = model::arrangementRows(*snapshot_, showMixRows_, showLanes_);
    layout_.setTrackCount(static_cast<int>(rows_.size()));
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        const auto& row = rows_[static_cast<size_t>(i)];
        const auto it = heights_.find(row.key());
        layout_.setHeight(i, it != heights_.end() ? it->second
                             : row.isLane()    ? model::kLaneDefaultHeight
                             : row.isTake()    ? model::kTakeDefaultHeight
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
        const auto* lane = rowLane(i);
        const auto* take = rowTake(i);
        if (channel == nullptr && lane == nullptr && take == nullptr)
            continue;
        const double top = layout_.top(i), height = layout_.height(i);
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
        if (take != nullptr)
        {
            if (const auto it = takeStars_.find(rows_[static_cast<size_t>(i)].key()); it != takeStars_.end())
            {
                const auto slots = model::layoutTrackHeader(area.getWidth(), height, 1, false);
                show(*it->second, slots.buttons.empty() ? none : slots.buttons[0],
                     slots.buttons.empty() ? juce::Rectangle<int>() : at(slots.buttons[0]));
            }
            continue;
        }
        if (lane != nullptr)
        {
            const auto it = laneControls_.find(rows_[static_cast<size_t>(i)].key());
            if (it == laneControls_.end())
                continue;
            const auto slots = model::layoutTrackHeader(area.getWidth(), height, 1, false);
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
        const auto slots = model::layoutTrackHeader(area.getWidth(), height, buttonsOf(*channel), true);
        show(c.mute, slots.buttons.size() > 0 ? slots.buttons[0] : none, slots.buttons.size() > 0 ? at(slots.buttons[0]) : juce::Rectangle<int>());
        show(c.solo, slots.buttons.size() > 1 ? slots.buttons[1] : none, slots.buttons.size() > 1 ? at(slots.buttons[1]) : juce::Rectangle<int>());
        if (channel->holdsClips())
            show(c.arm, slots.buttons.size() > 2 ? slots.buttons[2] : none,
                 slots.buttons.size() > 2 ? at(slots.buttons[2]) : juce::Rectangle<int>());
        // "Takes · N" at the right of the subtitle, once the track has takes.
        if (channel->holdsClips())
        {
            const auto subtitle = at(slots.subtitle);
            show(c.takes, channel->takes.empty() ? none : slots.subtitle,
                 subtitle.withLeft(subtitle.getRight() - std::min(subtitle.getWidth() / 2, c.takes.preferredWidth())));
        }
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

void ArrangementView::selectTake(std::uint64_t track, std::uint64_t take)
{
    setSelection({.channel = track, .take = take});
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (rows_[static_cast<size_t>(i)].isTake() && rows_[static_cast<size_t>(i)].take == take)
            revealRow(i);
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
            const auto slots = model::layoutTrackHeader(row.getWidth(), row.getHeight(), 1, false);
            g.setColour(t.clipColour(lane->colour));
            g.fillRect(at(slots.colour));
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
        if (const auto* take = rowTake(i))
        {
            // A take lane (recording.md §6): a thin row in its track's colour, dimmed; its name
            // brighter while the main lane plays some of it.
            const auto& track = rowTakeTrack(i);
            g.setColour(selected ? t.surface2 : t.roles.subLane);
            g.fillRect(row);
            const auto slots = model::layoutTrackHeader(row.getWidth(), row.getHeight(), 1, false);
            g.setColour(t.clipDimmed(t.clipColour(track.colour)));
            g.fillRect(at(slots.colour));
            g.setColour(t.line);
            g.fillRect(juce::Rectangle<float>(row).removeFromBottom(1.0f));
            g.setColour(model::isUsed(*take, track.clips) ? t.textPrimary : t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::Body));
            g.drawText(juce::String::fromUTF8(take->name.c_str()), at(slots.name), juce::Justification::centredLeft, true);
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
        const auto slots = model::layoutTrackHeader(row.getWidth(), row.getHeight(), buttonsOf(track), true);
        g.setColour(master ? t.textTertiary : t.clipColour(track.colour));
        g.fillRect(at(slots.colour));
        g.setColour(t.line);
        if (i == mixRow)
            g.fillRect(row.withHeight(2.0f)); // the divider above the returns and the master
        g.fillRect(juce::Rectangle<float>(row).removeFromBottom(1.0f)); // the row separator, continued across the lanes

        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::Name));
        g.drawText(juce::String::fromUTF8(track.name.c_str()), at(slots.name), juce::Justification::centredLeft, true);
        if (slots.subtitle.isEmpty())
            continue;
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
        auto subtitle = at(slots.subtitle);
        if (const auto it = headerControls_.find(track.id); it != headerControls_.end() && it->second->takes.isVisible())
            subtitle.setRight(static_cast<float>(it->second->takes.getX() - t.shell.itemGap)); // "Takes · N" is there
        g.setColour(track.instrument.empty() && hasMidi ? t.roles.warning : t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(label, subtitle, juce::Justification::centredLeft, true);
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
        g.setColour(rowSelected(r) ? t.roles.laneSelected : r.holdsClips() ? t.roles.lane : t.roles.subLane);
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
    // Take lanes: every take, dimmed where the main lane doesn't play it.
    for (int i = 0; i < layout_.trackCount(); ++i)
        if (const auto* take = rowTake(i); take != nullptr && rowVisible(i))
            for (const auto& clip : take->clips)
                paintTakeClip(g, clip, rowTakeTrack(i), static_cast<float>(area.getY() + layout_.top(i)),
                              static_cast<float>(layout_.height(i)), area, visible);
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
        const float y = static_cast<float>(area.getY() + layout_.top(row));
        const float h = static_cast<float>(layout_.height(row));
        const float x0 = std::max(laneX(rec.start), static_cast<float>(area.getX()) - 2.0f);
        const float x1 = std::min(laneX(playhead_), static_cast<float>(area.getRight()) + 2.0f);
        if (x1 <= x0)
            continue;
        const auto box = juce::Rectangle<float>(x0, y + 1.0f, x1 - x0, h - 3.0f);
        g.setColour(t.clipBody(t.record, false));
        g.fillRect(box);
        const float strip = std::min(15.0f, box.getHeight() * 0.35f);
        g.setColour(t.record);
        g.fillRect(box.withHeight(strip));
        if (rec.audio)
        {
            g.setColour(t.clipContent(t.record));
            session_.drawLiveWaveform(g, rec.track,
                                      juce::Rectangle<float>(laneX(rec.start), box.getY() + strip, laneX(playhead_) - laneX(rec.start),
                                                             box.getHeight() - strip)
                                          .toNearestInt(),
                                      playhead_ - rec.start);
        }
        if (box.getWidth() > 16.0f && strip >= 10.0f)
        {
            g.setColour(t.surface0);
            g.setFont(ui::font(ui::TextStyle::Control));
            g.drawText("Recording", box.withHeight(strip).reduced(4.0f, 0.0f), juce::Justification::centredLeft, true);
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

void ArrangementView::paintTakeClip(juce::Graphics& g, const model::ClipInfo& clip, const model::TrackInfo& track, float y,
                                    float h, juce::Rectangle<int> area, juce::Rectangle<int> visible)
{
    paintClip(g, clip, y, h, area, visible, false, true);
    for (const auto& [from, to] : model::inUse(clip, track.clips))
    {
        const auto used = juce::Rectangle<float>(laneX(from), y, laneX(to) - laneX(from), h).getSmallestIntegerContainer();
        if (!used.intersects(visible))
            continue;
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(used);
        paintClip(g, clip, y, h, area, visible.getIntersection(used), false);
    }
}

void ArrangementView::paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h,
                                juce::Rectangle<int> area, juce::Rectangle<int> visible, bool selected, bool dimmed)
{
    const auto& t = ui::Theme::current();
    if (clip.end() < timeline_.start() || clip.start > timeline_.end())
        return;
    const float x0 = std::max(laneX(clip.start), static_cast<float>(area.getX()) - 2.0f);
    const float x1 = std::min(laneX(clip.end()), static_cast<float>(area.getRight()) + 2.0f);
    const auto box = juce::Rectangle<float>(x0, y + 1.0f, std::max(1.0f, x1 - x0), h - 3.0f);
    if (!box.intersects(visible.toFloat()))
        return;
    const auto colour = dimmed ? t.clipDimmed(t.clipColour(clip.colour)) : t.clipColour(clip.colour);
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
        else if (row && rowTake(row->track) != nullptr)
        {
            if (takeClipAt(row->track, e.position.x) != nullptr) // a click uses the take
                cursor = juce::MouseCursor::PointingHandCursor;
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
            if (e.mods.isPopupMenu())
                rowMenu(-1);
            return;
        }
        const auto* channel = rowChannel(row->track);
        // A header selects its channel alone, or its lane or take (selection.md §2, automation.md
        // §9, recording.md §10).
        if (const auto* lane = rowLane(row->track))
        {
            selectLane(lane->id);
            if (onLaneActivated)
                onLaneActivated();
        }
        else if (const auto* take = rowTake(row->track))
            selectTake(rowTakeTrack(row->track).id, take->id);
        else
            selectTrack(channel != nullptr ? std::optional(channel->id) : std::nullopt);
        if (e.mods.isPopupMenu())
            rowMenu(row->track);
        return;
    }
    if (!lanes.contains(pos))
        return;

    auto row = layout_.hit(pos.y - lanes.getY());
    const auto* rowChannelInfo = row ? rowChannel(row->track) : nullptr;
    const auto rowTrack = rowChannelInfo != nullptr ? std::optional(rowChannelInfo->id) : std::nullopt;
    if (e.mods.isPopupMenu())
    {
        // A right-click: the row's menu, as on its header (a clip keeps its own handling); below
        // the rows, the items that make rows.
        if (!row)
        {
            rowMenu(-1);
            return;
        }
        if (const auto* lane = rowLane(row->track))
        {
            selectLane(lane->id);
            if (onLaneActivated)
                onLaneActivated();
            rowMenu(row->track);
            return;
        }
        if (const auto* take = rowTake(row->track))
        {
            selectTake(rowTakeTrack(row->track).id, take->id);
            rowMenu(row->track);
            return;
        }
        std::vector<int> rowTracks;
        for (const auto& r : rows_)
            rowTracks.push_back(r.holdsClips() ? r.track : -1);
        if (!model::hitClip(*snapshot_, layout_, rowTracks, timeline_, e.position.x - static_cast<float>(lanes.getX()),
                            e.position.y - static_cast<float>(lanes.getY()), ui::Theme::current().input.edgeGrab))
        {
            selectTrack(rowTrack);
            rowMenu(row->track);
            return;
        }
    }
    if (row && rowLane(row->track) != nullptr)
    {
        laneMouseDown(e, row->track);
        return;
    }
    if (const auto* take = row ? rowTake(row->track) : nullptr)
    {
        // A take lane: selects the take; a click on its clip uses it (recording.md §6).
        const auto track = rowTakeTrack(row->track).id;
        const auto id = take->id;
        const bool onClip = takeClipAt(row->track, e.position.x) != nullptr;
        selectTake(track, id);
        if (onClip)
            session_.useTake(track, id);
        return;
    }
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
        // Alt: every row of the same kind (channels, lanes or take lanes) takes the height.
        const double height = resizeFrom_ + (e.position.y - dragStart_.y);
        auto kindOf = [this](int i) {
            const auto& r = rows_[static_cast<size_t>(i)];
            return r.isLane() ? 1 : r.isTake() ? 2 : 0; // channels, lanes, take lanes
        };
        const int kind = kindOf(*resizeRow_);
        for (int i = 0; i < layout_.trackCount(); ++i)
            if (i == *resizeRow_ || (e.mods.isAltDown() && kindOf(i) == kind))
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
    const auto id = rows_[static_cast<size_t>(row)].lane;
    lanePreview_ = std::pair{rows_[static_cast<size_t>(row)].key(), edit}; // until the snapshot has it
    session_.setLanePoints(id, edit.points, std::nullopt, undoName);
    setQuietly(model::withLane(*snapshot_, id, edit.selection));
    repaint();
}

void ArrangementView::laneMouseDown(const juce::MouseEvent& e, int row)
{
    const auto* lane = rowLane(row);
    if (lane == nullptr)
        return;
    if (onLaneActivated)
        onLaneActivated();
    const auto id = lane->id;
    const auto geo = laneGeometry(row);
    const auto map = geo.mapping();
    const auto rules = lane->rules();
    const auto& points = lane->points;
    const auto grab = ui::Theme::current().input.edgeGrab;
    const auto hitPoint = model::hitPoint(points, map, e.position.x, e.position.y, grab);
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();
    const bool sameLane = selection_.lane == id;
    drag_ = Drag::Lane;
    laneDrag_ = {.row = row, .base = points};

    if (e.getNumberOfClicks() >= 2)
    {
        drag_ = Drag::None;
        if (hitPoint && e.mods.isAltDown() && rules.stepped())
            explainSteps(*lane);
        else if (hitPoint && e.mods.isAltDown()) // Alt + double-click straightens the segment after it
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
        setQuietly(model::withLane(*snapshot_, id, selection));
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
        setQuietly(model::withLane(*snapshot_, id));
        return;
    }
    if (const auto segment = model::hitSegment(points, map, e.position.x, e.position.y, 4.0))
    {
        if (e.mods.isAltDown() && rules.stepped())
        {
            explainSteps(*lane); // nothing to bend: say why, rather than do nothing
            drag_ = Drag::None;
            return;
        }
        laneDrag_.kind = e.mods.isAltDown() ? LaneDrag::Kind::Bend : LaneDrag::Kind::Segment;
        laneDrag_.index = *segment;
        laneDrag_.bend = model::bendOf(points[*segment]);
        setQuietly(model::withLane(*snapshot_, id, {*segment, *segment + 1}));
        return;
    }
    // An empty spot: box-select points (Shift adds).
    laneDrag_.kind = LaneDrag::Kind::Box;
    laneDrag_.boxBase = additive && sameLane ? selection_.points : std::set<size_t>{};
    setQuietly(model::withLane(*snapshot_, id, laneDrag_.boxBase));
}

void ArrangementView::explainSteps(const model::AutomationLaneInfo& lane)
{
    if (onMessage && !lane.assignments.empty())
        onMessage(juce::String::fromUTF8(lane.assignments.front().name.c_str()) +
                  " has steps: it holds each value until the next point, so its lane can't curve");
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

void ArrangementView::rowMenu(int row)
{
    juce::PopupMenu menu;
    if (const auto* lane = rowLane(row))
    {
        const auto id = lane->id;
        const bool on = lane->allOn();
        menu.addItem("Rename...", [this, id] {
            if (onRenameLane)
                onRenameLane(id);
        });
        menu.addItem("Assign parameter...", [this, id] {
            if (onAssignLane)
                onAssignLane(id);
        });
        menu.addItem(on ? "Turn lane off" : "Turn lane on", !lane->points.empty() && !lane->assignments.empty(), false,
                     [this, id, on] { session_.setLaneOn(id, !on); });
        menu.addItem("Remove automation lane", [this, id] {
            session_.removeAutomationLane(id);
            if (selection_.lane == id)
                selectTrack(selection_.channel);
        });
    }
    else if (const auto* take = rowTake(row))
    {
        // A take lane's menu (recording.md §6).
        const auto track = rowTakeTrack(row).id;
        const auto id = take->id;
        const bool star = take->star;
        menu.addItem("Use this take", [this, track, id] { session_.useTake(track, id); });
        menu.addItem(star ? "Remove star" : "Star", [this, track, id, star] { session_.setTakeStar(track, id, !star); });
        menu.addItem("Rename...", [this, track, id] {
            if (onRenameTake)
                onRenameTake(track, id);
        });
        menu.addSeparator();
        auto toTrack = [this, track, id](bool move) {
            const auto made = session_.takesToTracks(track, {id}, move);
            if (!made.empty())
            {
                session_.updateSnapshotNow();
                selectTrack(made.front());
            }
        };
        menu.addItem("Copy to new track", [toTrack] { toTrack(false); });
        menu.addItem("Move to new track", [toTrack] { toTrack(true); });
        menu.addSeparator();
        menu.addItem("Delete take", [this, track, id] {
            session_.deleteTakes(track, {id});
            if (selection_.take == id)
                selectTrack(track);
        });
    }
    else if (const auto* channel = rowChannel(row); channel != nullptr && channel->holdsClips() && onTrackMenu)
        onTrackMenu(menu);
    // Rows are made from any row, or the empty space (automation.md §3, daw-shell.md).
    if (onAddMenu)
    {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        onAddMenu(menu);
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
    if (selection_.take && selection_.channel && selection_.clips.empty())
    {
        session_.deleteTakes(*selection_.channel, {*selection_.take});
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
