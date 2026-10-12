#pragma once

#include "engine/Session.h"
#include "model/Timeline.h"
#include "ui/ClipLoopBrace.h"
#include "ui/WaveformCache.h"
#include "ui/time/TimeView.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
/// The clip view for one audio clip (M3 step C10, clip-loops.md §6): its file as a waveform where
/// the clip plays it in the song, with the clip's loop brace above the ruler (as the piano roll's),
/// the playhead, zoom and scroll. It edits nothing else yet: the home of M4's warping. Its grid is
/// the clip view's (the piano roll's, ADR 0047).
class AudioClipView final : public juce::Component
{
public:
    explicit AudioClipView(engine::Session& session);

    /// The clip to show (nullopt or a MIDI clip: nothing). Another clip fits the view to it (its
    /// loop, while it loops).
    void setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo);
    [[nodiscard]] const std::optional<model::ClipInfo>& clip() const noexcept { return clip_; }
    /// Once per display frame: the playhead, and the grid the Session keeps.
    void frame(double playheadSeconds);
    /// Where it shows the playhead: over a looped clip's later laps, in its loop.
    [[nodiscard]] double shownPlayhead() const noexcept { return playhead_; }
    /// The step it snaps to now ("1/16"), for the header's chip.
    [[nodiscard]] juce::String gridStepName() const;
    /// Blocks until its waveform is built (tests).
    void waitForWaveform() { waveforms_.waitUntilIdle(); }

    // Geometry, in component coordinates (tests drive the mouse with it).
    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    [[nodiscard]] juce::Rectangle<int> waveformArea() const;
    [[nodiscard]] const model::Timeline& timeline() const noexcept { return timeline_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

private:
    enum class Gesture { None, Ruler, Brace };
    /// The view's time of the clip's content zero (model::clipViewOrigin).
    [[nodiscard]] double origin() const;
    /// What the clip plays, in the view's time: its loop, or the clip.
    [[nodiscard]] std::pair<double, double> playedRange() const;
    void fitToClip();
    void syncGrid();

    engine::Session& session_;
    WaveformCache waveforms_; // its file's peaks (one file at a time)
    std::optional<model::ClipInfo> clip_;
    model::TempoMap tempo_;
    model::Timeline timeline_;
    std::vector<model::Timeline::GridLine> lines_; // paint scratch
    double playhead_ = 0.0;
    time::PlayheadTracker playheadTracker_;
    bool needsFit_ = false;
    Gesture gesture_ = Gesture::None;
    juce::Point<float> dragStart_, lastDrag_;
    bool moved_ = false;
    time::RulerGesture rulerGesture_;
    ClipLoopBrace brace_;
};
} // namespace fd::app
