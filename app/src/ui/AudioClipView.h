#pragma once

#include "engine/Session.h"
#include "model/AudioSound.h"
#include "model/AudioToolkit.h"
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
    /// A new range lets go of the markers and points selected: one selection at a time.
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
    /// A tool's setting changed here (the Tempo tool's downbeat dragged on the waveform): the
    /// Tools pane shows it.
    std::function<void(std::string_view setting, double value)> onToolValue;
    /// The clip view's remembered tool settings (view state; the piano roll keeps them).
    std::function<model::ToolValues(const model::ToolSpec&)> rememberedValues;
    std::function<void(const model::ToolSpec&, const model::ToolValues&)> rememberValues;

    // ---- warp markers and transients (§7.4–§7.7) -----------------------------------------------
    /// The clip's transients, as shown: seconds of its file as recorded (none while it's analysed).
    [[nodiscard]] std::vector<double> transients() const;
    /// The warp markers selected (indices into the clip's markers).
    [[nodiscard]] const std::set<size_t>& selectedMarkers() const noexcept { return markers_; }
    void selectMarkers(std::set<size_t> markers);
    [[nodiscard]] bool hasMarkerSelection() const noexcept { return !markers_.empty(); }
    /// Where a marker or a transient shows: its x in component coordinates (tests drive the mouse
    /// with it); nullopt for one not shown.
    [[nodiscard]] std::optional<float> markerX(size_t index) const;
    [[nodiscard]] std::optional<float> transientX(size_t index) const;
    /// Every transient's x, in order, worked out once (painting, the mouse).
    [[nodiscard]] std::vector<float> transientXs() const;
    /// The marker commands (§7.5, the Audio menu's Warp markers): each one undo step; false,
    /// saying why through onMessage, when it can't.
    bool markersAtTransients(); // in the range, or the whole clip
    bool warpFromHere();        // from the first selected marker
    bool setDownbeat();         // at the first selected marker, else the range's start
    bool removeMarkers();       // the selected ones; else those in the range; else all
    /// Quantise (§7.7): the transients in the range, or the clip, towards the song's grid through
    /// markers, Warp turned on with it.
    bool quantise(const model::WarpQuantise& settings);
    /// The Tempo tool's Apply (§7.8): the clip warps at the tempo, starting at the downbeat; or the
    /// song's tempo is set from it. One undo step.
    void applyTempo(const model::TempoChoice& choice);
    void nudgeMarkers(int steps); // ← →: a grid step each
    [[nodiscard]] std::string markersRefusal() const;
    /// Transients by hand (§7.4, the Audio menu's Warp markers): one undo step; false, saying why
    /// through onMessage, when it can't. Added at the range's start, else the playhead.
    bool addTransient();
    bool removeTransients(); // those in the range
    [[nodiscard]] std::string addTransientRefusal() const;
    [[nodiscard]] std::string removeTransientsRefusal() const;
    /// The range's time handles in the ruler (§7.6): empty without a range.
    [[nodiscard]] std::pair<juce::Rectangle<int>, juce::Rectangle<int>> timeHandles() const;

    // ---- the rows' height (§4) -----------------------------------------------------------------
    /// How much taller than full scale the waveform is drawn: 1 (full scale fills a row) to 64.
    [[nodiscard]] float levelZoom() const noexcept { return zoom_; }
    void setLevelZoom(float zoom);

    // ---- geometry, in component coordinates (tests drive the mouse with it) ---------------------
    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    /// The marker strip under the ruler: transients' ticks, warp markers' handles (§4).
    [[nodiscard]] juce::Rectangle<int> markerStripArea() const;
    /// Where Slice would cut while it's set (timeline seconds; tests).
    [[nodiscard]] const std::vector<double>& slicePreview() const noexcept { return slicePreview_; }
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
    ui::HandCursor hand_; // over a lane's point, which moves where it's dragged
    enum class Gesture
    {
        None,
        Ruler,
        Brace,
        Range,     // a range dragged across the waveform
        RangeGain, // the range's gain handle
        Line,      // the gain line's points (LaneGesture)
        Level,     // the level column: the rows' height
        Downbeat,  // the Tempo tool's downbeat, dragged on the waveform (§7.8)
        Marker,    // a warp marker dragged (Shift: the audio slid under it)
        Transient, // a transient's tick dragged: made a marker and moved
        MarkerBox, // a box in the marker strip: markers selected
        TimeHandle // a range's time handle: its audio stretched (§7.6)
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
    void paintMarkers(juce::Graphics& g);
    /// The Tempo tool's beats over the waveform, its downbeat marked (§7.8).
    void paintTempo(juce::Graphics& g) const;
    /// Where Slice would cut, over the waveform, while it's set (§9).
    void paintSlices(juce::Graphics& g) const;
    /// Where the Tempo tool's downbeat shows (nullopt without the tool).
    [[nodiscard]] std::optional<float> downbeatX() const;
    void paintTimeHandles(juce::Graphics& g) const;

    // Warping in the view (AudioClipViewWarp.cpp).
    /// The clip's warp as shown: a gesture's preview, else the clip's.
    [[nodiscard]] const model::AudioWarp& shownWarp() const;
    /// The clip's warp quantised (§7.7), as recorded, at the sensitivity shown.
    [[nodiscard]] std::optional<model::AudioWarp> quantisedWarp(const model::WarpQuantise& settings) const;
    /// The shown warp as the file plays (a reversed clip's seen from its end): what's drawn.
    [[nodiscard]] model::AudioWarp playedWarp() const;
    /// A warp edited as the file plays, kept as recorded: one undo step.
    void keepWarp(const model::AudioWarp& played, const std::string& undoName);
    /// The view's time of a beat of the clip's content, and back.
    [[nodiscard]] double viewTimeOfBeat(double beat) const;
    [[nodiscard]] double beatOfViewTime(double time) const;
    /// Where in its file as recorded the view's `time` plays, as shown (through its warp and its
    /// reversal).
    [[nodiscard]] double fileAtViewTime(double time) const;
    /// Where Add transient puts one (the view's time): the range's start, else the playhead.
    [[nodiscard]] double transientPlace() const;
    /// The transients shown in the range (seconds of the file as recorded).
    [[nodiscard]] std::vector<double> transientsInRange() const;
    /// What's under the mouse in the marker strip.
    [[nodiscard]] std::optional<size_t> markerAt(juce::Point<float> p) const;
    [[nodiscard]] std::optional<size_t> transientAt(juce::Point<float> p) const;
    bool warpPress(const juce::MouseEvent& e);
    bool warpDrag(const juce::MouseEvent& e);
    bool warpRelease(const juce::MouseEvent& e);
    [[nodiscard]] juce::PopupMenu markerMenu(std::optional<size_t> marker, std::optional<double> at);
    /// The tag a marker drag shows: the piece before it, its stretch and tempo.
    [[nodiscard]] juce::String markerTag() const;

    engine::Session& session_;
    WaveformCache waveforms_{true}; // its file's peaks, a channel each (one file at a time)
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

    // Warping (§7): the markers selected, a gesture's preview of the clip's warp (as recorded),
    // what it drags, and the transient hovered.
    std::set<size_t> markers_;
    std::optional<model::AudioWarp> warpPreview_;
    std::optional<size_t> dragMarker_; // in the played warp
    model::AudioWarp dragFrom_;        // the played warp when the drag began
    std::optional<size_t> hoveredTransient_;
    std::pair<double, double> markerBox_;            // view times, while a box is dragged
    bool handleAtEnd_ = false;                       // which time handle is dragged
    std::pair<double, double> rangeFrom_;            // the range when its handle was pressed
    std::optional<double> sensitivityPreview_;       // Quantise's sensitivity, while it's set (§7.4)
    std::optional<model::TempoChoice> tempoPreview_; // the Tempo tool's, while it's chosen (§7.8)
    std::vector<double> slicePreview_;               // where Slice would cut, timeline seconds (§9)
};
} // namespace fd::app
