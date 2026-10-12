#include "ui/AudioClipView.h"

#include "model/ClipLoops.h"
#include "ui/WaveformTiles.h"

namespace fd::app
{
AudioClipView::AudioClipView(engine::Session& session) : session_(session)
{
    syncGrid();
    setWantsKeyboardFocus(false);
    waveforms_.onReady = [this] { repaint(); };
}

juce::String AudioClipView::gridStepName() const
{
    return timeline_.gridStepName(tempo_);
}

// ---- geometry ------------------------------------------------------------------------------

juce::Rectangle<int> AudioClipView::loopStripArea() const
{
    return getLocalBounds().removeFromTop(ui::Theme::current().shell.loopStripHeight);
}

juce::Rectangle<int> AudioClipView::rulerArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedTop(s.loopStripHeight).removeFromTop(s.rulerHeight);
}

juce::Rectangle<int> AudioClipView::waveformArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedTop(s.loopStripHeight + s.rulerHeight);
}

double AudioClipView::origin() const
{
    return clip_ ? model::clipViewOrigin(*clip_) : 0.0;
}

std::pair<double, double> AudioClipView::playedRange() const
{
    return clip_ ? ClipLoopBrace::rangeOf(*clip_, origin()) : std::pair{0.0, 0.0};
}

void AudioClipView::fitToClip()
{
    const auto area = waveformArea();
    if (!clip_ || area.getWidth() <= 1)
        return; // once laid out
    needsFit_ = false;
    timeline_.setWidth(area.getWidth());
    const auto [from, to] = playedRange();
    const double length = std::max(to - from, tempo_.secondsForBeats(tempo_.beatsPerBar()));
    const double margin = length * 0.03;
    timeline_.setView(std::max(0.0, from - margin), area.getWidth() / (length + 2.0 * margin));
}

void AudioClipView::resized()
{
    timeline_.setWidth(waveformArea().getWidth());
    if (needsFit_)
        fitToClip();
}

// ---- state ---------------------------------------------------------------------------------

void AudioClipView::syncGrid()
{
    // The clip view's grid: the piano roll's (ADR 0047), which the Session keeps.
    if (const auto g = session_.grid(engine::Session::GridEditor::PianoRoll); g != timeline_.grid())
    {
        timeline_.setGrid(g);
        repaint();
    }
}

void AudioClipView::setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo)
{
    tempo_ = tempo;
    if (!clip || clip->kind != model::ClipInfo::Kind::Audio)
    {
        clip_.reset();
        gesture_ = Gesture::None;
        repaint();
        return;
    }
    const bool another = !clip_ || clip_->id != clip->id;
    clip_ = std::move(clip);
    waveforms_.request(clip_->sourcePath);
    if (another)
    {
        needsFit_ = true;
        fitToClip();
    }
    repaint();
}

void AudioClipView::frame(double playheadSeconds)
{
    syncGrid();
    // Over a looped clip's later laps, the playhead shows where in the loop the song is (§6).
    const bool overLoop = clip_ && clip_->loop.on() && playheadSeconds >= clip_->start && playheadSeconds < clip_->end();
    playhead_ = overLoop ? model::clipViewTime(*clip_, playheadSeconds) : playheadSeconds;
    playheadTracker_.update(*this, timeline_, waveformArea(), playhead_);
}

// ---- painting ------------------------------------------------------------------------------

void AudioClipView::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (!clip_)
        return; // the detail panel says what to select
    timeline_.gridLines(tempo_, lines_);
    const auto area = waveformArea();
    const auto colour = t.clipColour(clip_->colour);
    const auto xOf = [&](double seconds) { return static_cast<float>(area.getX() + timeline_.xForTime(seconds)); };

    // What the clip plays, on its body's colour; the rest of its file shaded and faded (§6).
    const auto [from, to] = playedRange();
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(area);
        g.setColour(t.roles.shade);
        g.fillRect(area);
        const auto played = juce::Rectangle<float>(xOf(from), static_cast<float>(area.getY()), xOf(to) - xOf(from),
                                                   static_cast<float>(area.getHeight()));
        g.setColour(t.clipBody(colour, false));
        g.fillRect(played);
        time::paintGrid(g, lines_, timeline_, area);
        if (const auto* peaks = waveforms_.find(clip_->sourcePath))
        {
            // The whole file from the content's zero, faded; again in full where it plays.
            const model::ClipInfo file{.kind = model::ClipInfo::Kind::Audio, .start = origin(), .length = clip_->sourceLength};
            WaveformTiles::drawDirect(g, *peaks, file, area.toFloat(), area, timeline_, area.getX(), t.faded(t.clipContent(colour)));
            g.reduceClipRegion(played.getSmallestIntegerContainer());
            g.setColour(t.clipBody(colour, false));
            g.fillRect(played);
            time::paintGrid(g, lines_, timeline_, area);
            WaveformTiles::drawDirect(g, *peaks, file, area.toFloat(), area, timeline_, area.getX(), t.clipContent(colour));
        }
    }

    // The loop strip with the clip's brace, then the ruler with what it plays in its colour.
    g.setColour(t.surface1);
    g.fillRect(getLocalBounds().removeFromTop(area.getY()));
    time::paintLoopStrip(g, timeline_, loopStripArea(), brace_.preview().value_or(std::pair{from, to}), clip_->loop.on());
    time::paintRuler(g, lines_, timeline_, rulerArea(), {.range = std::pair{from, to}, .rangeColour = colour});

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    time::paintPlayhead(g, timeline_, area, playhead_);
}

// ---- input ---------------------------------------------------------------------------------

void AudioClipView::mouseMove(const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;
    if (clip_ && loopStripArea().contains(e.getPosition()))
    {
        const auto zone = ClipLoopBrace::zoneAt(*clip_, origin(), timeline_, e.position.x - static_cast<float>(waveformArea().getX()));
        if (zone == model::BraceZone::Start || zone == model::BraceZone::End)
            cursor = juce::MouseCursor::LeftRightResizeCursor;
    }
    setMouseCursor(cursor);
}

void AudioClipView::mouseDown(const juce::MouseEvent& e)
{
    gesture_ = Gesture::None;
    if (!clip_)
        return;
    dragStart_ = lastDrag_ = e.position;
    moved_ = false;
    const float x = e.position.x - static_cast<float>(waveformArea().getX());
    if (loopStripArea().contains(e.getPosition()))
    {
        // The clip's loop brace, as the piano roll's and the transport's (clip-loops.md §6).
        gesture_ = Gesture::Brace;
        brace_.press(*clip_, origin(), timeline_, x);
        return;
    }
    if (rulerArea().contains(e.getPosition()))
    {
        gesture_ = Gesture::Ruler;
        rulerGesture_.begin(x);
    }
}

void AudioClipView::mouseDrag(const juce::MouseEvent& e)
{
    if (!clip_)
        return;
    const auto previous = lastDrag_;
    lastDrag_ = e.position;
    const float x = e.position.x - static_cast<float>(waveformArea().getX());
    if (gesture_ == Gesture::Ruler)
    {
        rulerGesture_.drag(timeline_, e.position - previous, x);
        repaint();
        return;
    }
    if (gesture_ != Gesture::Brace || (!moved_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold))
        return;
    moved_ = true;
    const bool free = !timeline_.grid().snap || e.mods.isAltDown();
    brace_.drag(*clip_, timeline_, dragStart_.x - static_cast<float>(waveformArea().getX()), x,
                [this, free](double t) { return free ? std::max(0.0, t) : timeline_.snap(t, tempo_); });
    repaint(loopStripArea());
}

void AudioClipView::mouseUp(const juce::MouseEvent& e)
{
    if (clip_ && gesture_ == Gesture::Brace)
        brace_.release(session_, *clip_, origin(), moved_);
    else if (clip_ && gesture_ == Gesture::Ruler && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        // A click in the ruler moves the playhead (snapped; Alt for free).
        session_.setPosition(time::rulerTime(timeline_, tempo_, e.position.x - static_cast<float>(waveformArea().getX()),
                                             !timeline_.grid().snap || e.mods.isAltDown()));
    gesture_ = Gesture::None;
    repaint();
}

void AudioClipView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!clip_)
        return;
    time::applyWheel(timeline_, e.position.x - static_cast<float>(waveformArea().getX()), e.mods, wheel, [](double) {});
    repaint();
}

void AudioClipView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    time::applyMagnify(timeline_, e.position.x - static_cast<float>(waveformArea().getX()), scaleFactor);
    repaint();
}
} // namespace fd::app
