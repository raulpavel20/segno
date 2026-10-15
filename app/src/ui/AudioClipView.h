#pragma once

#include "engine/Session.h"
#include "model/AudioSound.h"
#include "model/Timeline.h"
#include "ui/AutomationLane.h"
#include "ui/ClipLoopBrace.h"
#include "ui/ToolHost.h"
#include "ui/WaveformCache.h"
#include "ui/time/TimeView.h"

#include <fd_ui/fd_ui.h>
#include <map>
#include <optional>

namespace fd::app
{
/// The audio editor (M3 step P6, ADR 0063, docs/design/audio-editor.md §4–§6): an audio clip's file
/// as a waveform where the clip plays it in the song, a row for each signal it plays, drawn through
/// its gain, gain line and fades as heard; the level column beside the rows (dragged, their height
/// zooms); the gain line in the lane under them, edited as automation is; a range dragged across
/// the waveform, with a gain handle of its own, that the bar's and the Audio menu's commands act
/// on; the loop brace, the ruler, the playhead, zoom and scroll, as the piano roll has them. Its
/// grid is the clip view's (ADR 0047). It is the Tools pane's host for the audio's tools (§11).
class AudioClipView final : public juce::Component, public ToolHost
{
public:
    explicit AudioClipView(engine::Session& session);
    ~AudioClipView() override;

    /// The clip to show (nullopt or a MIDI clip: nothing). Another clip fits the view to it (its
    /// loop, while it loops) and clears the range.
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

    // ---- the range (§5) ------------------------------------------------------------------------
    /// The range selected in the waveform: seconds on the timeline (the lap the view shows), within
    /// the clip.
    [[nodiscard]] const std::optional<std::pair<double, double>>& range() const noexcept { return range_; }
    void setRange(std::optional<std::pair<double, double>> range);
    /// Edit > Select all: the whole clip.
    void selectAll();
    /// What acts on the range (the bar, the Audio menu, the right-click menu, keys): one undo step;
    /// false, saying why through onMessage, when it can't.
    bool editRange(engine::Session::RangeAction action, double db = 0.0);
    /// Why `action` can't act on the range now (empty: it can).
    [[nodiscard]] std::string rangeRefusal(engine::Session::RangeAction action) const;
    /// The gain line's selected points (Edit > Delete removes them: one undo step).
    [[nodiscard]] bool hasPointSelection() const noexcept { return !points_.empty(); }
    void deletePoints();
    /// The range changed (the bar shows its commands while there is one).
    std::function<void()> onRangeChanged;
    /// After a range command: the clips to select (Split: the range's own), so the editor shows it.
    std::function<void(std::vector<std::uint64_t>)> onSelectClips;
    /// What to tell the person: a command refused, and why.
    std::function<void(const juce::String&)> onMessage;
    /// The waveform's right-click menu: the Audio menu (its owner builds it, ADR 0040).
    std::function<juce::PopupMenu()> menu;
    /// The arrangement's selected audio clips: what Normalise acts on (the shown clip without it).
    std::function<std::vector<std::uint64_t>()> selectedClips;
    /// It was pressed: Edit's commands act on it until something else is used.
    std::function<void()> onUsed;
    /// The clip view's remembered tool settings (view state; the piano roll keeps them).
    std::function<model::ToolValues(const model::ToolSpec&)> rememberedValues;
    std::function<void(const model::ToolSpec&, const model::ToolValues&)> rememberValues;

    // ---- the rows' height (§4) -----------------------------------------------------------------
    /// How much taller than full scale the waveform is drawn: 1 (full scale fills a row) to 64.
    [[nodiscard]] float levelZoom() const noexcept { return zoom_; }
    void setLevelZoom(float zoom);

    // ---- geometry, in component coordinates (tests drive the mouse with it) ---------------------
    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    /// Every row of the waveform.
    [[nodiscard]] juce::Rectangle<int> waveformArea() const;
    [[nodiscard]] juce::Rectangle<int> rowArea(int row) const;
    /// How many rows it shows: one for each signal the clip plays (§6.3).
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] juce::Rectangle<int> laneArea() const;
    /// The level column beside the rows.
    [[nodiscard]] juce::Rectangle<int> levelArea() const;
    /// The range's gain handle (empty without a range).
    [[nodiscard]] juce::Rectangle<float> rangeHandle() const;
    [[nodiscard]] const model::Timeline& timeline() const noexcept { return timeline_; }
    [[nodiscard]] ui::LaneChoice& laneHead() noexcept { return laneHead_; }
    /// What the lane can show, the shown one ticked: its head's menu and View > Clip view lane.
    [[nodiscard]] juce::PopupMenu laneMenu() const;

    // ---- ToolHost: the audio's tools (§11) ------------------------------------------------------
    [[nodiscard]] model::ToolValues toolValues(const model::ToolSpec& tool) const override;
    void setToolValues(const model::ToolSpec& tool, const model::ToolValues& values) override;
    void preview(const model::ToolSpec& tool, const model::ToolValues& values) override;
    void cancel() override;
    void apply(const model::ToolSpec& tool, const model::ToolValues& values) override;
    [[nodiscard]] juce::String scope(const model::ToolSpec& tool) const override;
    [[nodiscard]] juce::String widestScope() const override { return "999 clips"; }
    [[nodiscard]] const music::Key* key() const override { return nullptr; }
    [[nodiscard]] juce::String unavailable(const model::ToolSpec& tool) const override;
    [[nodiscard]] std::uint64_t version() const override;
    [[nodiscard]] juce::String what() const override { return "the clips"; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

private:
    enum class Gesture
    {
        None,
        Ruler,
        Brace,
        Range,     // a range dragged across the waveform
        RangeGain, // the range's gain handle
        Line,      // the gain line's points (LaneGesture)
        Level      // the level column: the rows' height
    };
    /// The view's time of the clip's content zero (model::clipViewOrigin).
    [[nodiscard]] double origin() const;
    /// What the clip plays, in the view's time: its loop, or the clip.
    [[nodiscard]] std::pair<double, double> playedRange() const;
    void fitToClip();
    void syncGrid();
    /// The clips the audio's tools act on.
    [[nodiscard]] std::vector<std::uint64_t> toolClips() const;
    /// The sound the view draws: the clip's, its line as a gesture or a tool previews it.
    [[nodiscard]] model::AudioSound shownSound() const;
    /// The gain line in the view's time, as shown now.
    [[nodiscard]] const model::AutomationPoints& shownLine() const;
    [[nodiscard]] LaneGeometry laneGeometry() const;
    [[nodiscard]] LaneGesture::Lane gestureLane() const;
    void keepLine(const model::PointEdit& edit, const std::string& undoName);
    /// The peaks a row draws: a channel's, or both channels' mean (Mono).
    [[nodiscard]] const model::Peaks* rowPeaks(int row);
    [[nodiscard]] double snapped(double seconds, bool free) const;
    void paintRows(juce::Graphics& g);
    void paintLevels(juce::Graphics& g) const;
    void paintLane(juce::Graphics& g);
    void paintRange(juce::Graphics& g) const;

    engine::Session& session_;
    WaveformCache waveforms_{true}; // its file's peaks, a channel each (one file at a time)
    std::map<std::string, std::shared_ptr<const model::Peaks>> mean_; // Mono's rows, by file
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
    float zoom_ = 1.0f, zoomBase_ = 1.0f;

    std::optional<std::pair<double, double>> range_;
    double rangeAnchor_ = 0.0;
    double rangeGainDb_ = 0.0;                           // the range's gain handle, while it's dragged
    std::optional<model::AutomationPoints> linePreview_; // in the file's time, while something previews it

    model::AutomationPoints lineView_;          // the clip's gain line in the view's time
    mutable model::AutomationPoints lineShown_; // a preview of it in the view's time (shownLine)
    LaneGesture laneGesture_;
    std::set<size_t> points_;                     // the line's selected points
    std::optional<model::PointEdit> lanePreview_; // a line gesture, while it lasts (view time)
    std::optional<model::PointEdit> laneKept_;    // the last edit kept, until the snapshot has it
    std::optional<size_t> hoveredPoint_;
    ui::LaneChoice laneHead_;

    std::map<std::uint64_t, float> previewGains_; // a tool's gains, shown while it's chosen
};
} // namespace fd::app
