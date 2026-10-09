#pragma once

#include "engine/Session.h"
#include "ui/AutomationLane.h"
#include "ui/ChannelControls.h"
#include "ui/LaneControls.h"
#include "model/ArrangementRows.h"
#include "model/Editing.h"
#include "model/Selection.h"
#include "model/Timeline.h"
#include "model/TrackLayout.h"
#include "ui/WaveformCache.h"
#include "ui/WaveformTiles.h"
#include "ui/time/TimeView.h"

#include <fd_ui/fd_ui.h>

#include <map>
#include <optional>
#include <set>

namespace fd::app
{
/// The arrangement (docs/design/daw-shell.md, C2): ruler, track headers and lanes with the grid,
/// clips and playhead. Draws only from the Session's snapshot and its own view models.
class ArrangementView final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    /// The track header column's width (shell metrics, docs/design/ui-foundations.md).
    [[nodiscard]] static int headerWidth() { return ui::Theme::current().shell.trackHeaderWidth; }
    /// The ruler zone: the loop strip on top of the ruler row.
    [[nodiscard]] static int rulerHeight()
    {
        const auto& s = ui::Theme::current().shell;
        return s.loopStripHeight + s.rulerHeight;
    }
    [[nodiscard]] static int loopStripHeight() { return ui::Theme::current().shell.loopStripHeight; }

    explicit ArrangementView(engine::Session& session);

    void setSnapshot(std::shared_ptr<const model::Snapshot> snapshot);

    /// Once per display frame (message thread): animations and the playhead.
    void frame(double dt, double playheadSeconds);

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    /// Follow the playhead while playing (pages the view smoothly).
    void setFollowEnabled(bool on) noexcept { follow_ = on; }
    [[nodiscard]] bool isFollowEnabled() const noexcept { return follow_; }
    void setSnapEnabled(bool on) noexcept { snap_ = on; }
    [[nodiscard]] bool isSnapEnabled() const noexcept { return snap_; }

    /// The window's one selection (docs/design/selection.md, ADR 0027): a channel (a track, a
    /// return track or the master) and clips, the channel always the one of what was selected
    /// last. The mixer view shares it.
    [[nodiscard]] const model::Selection& selection() const noexcept { return selection_; }
    void setSelection(model::Selection selection);
    /// Selects a channel alone (clearing the clips), as a click on its header does.
    void selectTrack(std::optional<std::uint64_t> channel) { setSelection({.channel = channel}); }
    [[nodiscard]] std::optional<std::uint64_t> selectedTrack() const noexcept { return selection_.channel; }
    [[nodiscard]] const std::set<std::uint64_t>& selectedClips() const noexcept { return selection_.clips; }
    /// The selection changed: once per action (a drag tells when it ends). A new snapshot prunes
    /// it without telling; the owner reads it after setSnapshot().
    std::function<void()> onSelectionChanged;
    /// Called when the arrangement is clicked (the shell routes Delete to the area used last).
    std::function<void()> onActivated;
    /// Called when an automation lane is clicked (Delete, Ctrl+D, Ctrl+A and arrows act on it).
    std::function<void()> onLaneActivated;
    /// "Add automation lane..." on a channel (its header's menu).
    std::function<void(std::uint64_t channel)> onAddLane;
    /// Something worth telling the user (shown in the shell, never modal).
    std::function<void(const juce::String&)> onMessage;
    /// A clip was double-clicked or created: show it in the clip view.
    std::function<void(std::uint64_t clip)> onOpenClip;

    // Edit commands on the selection (C4), each one undo step.
    void deleteSelection();
    void duplicateSelection();
    /// Splits the selected clips at the playhead (or, with none selected, the clips under the
    /// playhead on the selected track).
    void splitAtPlayhead();
    void selectAllClips();
    /// A new MIDI clip (C2) on track `index` at `seconds`: from the grid line at or before it,
    /// one bar long, or shorter to fit before the next clip. Selects it. Returns its id (0 if
    /// there's no room).
    std::uint64_t createMidiClip(int trackIndex, double seconds, bool bypassSnap = false);
    /// The same at the playhead on the selected track.
    std::uint64_t createMidiClipAtPlayhead();
    [[nodiscard]] bool hasClipSelection() const noexcept { return !selection_.clips.empty(); }

    // Automation lanes (C4, docs/design/automation.md): rows under their channel.
    /// Selects a lane alone, as a click on its header does.
    void selectLane(std::uint64_t channel, const std::string& lane);
    [[nodiscard]] bool hasLaneSelection() const noexcept { return selection_.lane.has_value(); }
    [[nodiscard]] bool hasPointSelection() const noexcept { return selection_.lane && !selection_.points.empty(); }
    /// Point commands on the selected lane, each one undo step.
    void deletePoints();
    void duplicatePoints();
    void selectAllPoints();
    /// Moves the selected points by grid steps and by a value (lane units).
    void nudgePoints(int gridSteps, float value);
    /// Removes the selected lane (its points: one undo step).
    void removeSelectedLane();
    /// The rows as drawn.
    [[nodiscard]] const std::vector<model::ArrangementRow>& rows() const noexcept { return rows_; }
    /// Row heights set by hand, by row key (view state).
    [[nodiscard]] const std::map<std::string, double>& rowHeights() const noexcept { return heights_; }
    void setRowHeights(std::map<std::string, double> heights);

    [[nodiscard]] const model::Timeline& timeline() const noexcept { return timeline_; }
    [[nodiscard]] WaveformCache& waveforms() noexcept { return waveforms_; }
    /// Immediate zoom around a lane x (benchmarks; input uses the gesture handlers).
    void zoomAt(double laneX, double factor);
    /// Shows `startSeconds` at the left edge at `pixelsPerSecond` (restoring a saved view).
    void setView(double startSeconds, double pixelsPerSecond);
    /// Sets every track's height (clamped to the layout's limits).
    void setAllTrackHeights(double height);
    /// Return tracks and the master, below the tracks (C3, docs/design/mixer.md §2).
    void setMixRowsVisible(bool visible);
    /// Drops the mixer controls in the headers (before the project is replaced: they hold the
    /// engine's meters). setSnapshot() makes them again.
    void releaseChannelControls() { headerControls_.clear(); }
    [[nodiscard]] bool mixRowsVisible() const noexcept { return showMixRows_; }

    /// Wall time of the last paint, ms (frame statistics).
    [[nodiscard]] double lastPaintMs() const noexcept { return lastPaintMs_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    /// The channel a row belongs to (a lane row: its channel).
    [[nodiscard]] const model::TrackInfo* rowChannel(int row) const noexcept;
    /// A lane row's lane (null for other rows).
    [[nodiscard]] const model::LaneInfo* rowLane(int row) const noexcept;
    [[nodiscard]] juce::Rectangle<float> rowBounds(int row, juce::Rectangle<int> column) const;
    [[nodiscard]] LaneGeometry laneGeometry(int row) const;
    [[nodiscard]] engine::Session::LaneTarget laneTarget(int row) const;
    /// The fold chevron in a channel row's header (empty when it has no lanes).
    [[nodiscard]] juce::Rectangle<float> chevronArea(int row) const;
    void laneMouseDown(const juce::MouseEvent& e, int row);
    void laneMouseDrag(const juce::MouseEvent& e);
    void laneMouseUp(const juce::MouseEvent& e);
    void headerMenu(int row);
    void applyLaneEdit(int row, const model::PointEdit& edit, const std::string& undoName);
    [[nodiscard]] double gridStepSeconds() const;
    /// Places each visible row's mixer controls in its header's slots.
    void placeHeaderControls();

    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    [[nodiscard]] juce::Rectangle<int> headerArea() const;
    [[nodiscard]] juce::Rectangle<int> laneArea() const;
    [[nodiscard]] float laneX(double seconds) const;
    [[nodiscard]] double snapped(double seconds, bool bypass) const;
    /// Shows a selection without telling the owner yet (during a gesture); notifySelection()
    /// tells it, once, if anything changed since it was last told.
    void setQuietly(model::Selection selection);
    void notifySelection();
    /// The rows and their header controls, for the current snapshot.
    void syncRows();
    [[nodiscard]] std::function<double(double)> snapFunction(bool bypass) const;
    void paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h, juce::Rectangle<int> area,
                   juce::Rectangle<int> visible, bool selected);
    void syncLayout();

    void paintRuler(juce::Graphics& g);
    void paintHeaders(juce::Graphics& g);
    void paintLanes(juce::Graphics& g);
    void paintNotes(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body, juce::Colour colour);
    void paintWaveform(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                       juce::Rectangle<int> visible, juce::Colour colour);

    engine::Session& session_;
    std::shared_ptr<const model::Snapshot> snapshot_;
    bool showMixRows_ = true;

    /// One channel's controls in its header: mute, solo, volume, pan and the meter.
    struct HeaderControls
    {
        HeaderControls(engine::Session& session, const model::TrackInfo& channel);
        ChannelToggle mute, solo;
        VolumeBar volume;
        PanBar pan;
        ChannelMeter meter;
    };
    juce::Component headerLayer_; // clips the controls to the header column
    std::map<std::uint64_t, std::unique_ptr<HeaderControls>> headerControls_;
    std::vector<model::ArrangementRow> rows_;
    std::map<std::string, double> heights_; // by row key: set by hand (dragging a row's bottom edge)
    std::map<std::string, std::unique_ptr<LaneControls>> laneControls_; // by row key
    std::map<std::string, std::string> laneTexts_; // the value each lane header shows (repainted on change)

    /// A gesture on a lane's points.
    struct LaneDrag
    {
        enum class Kind { None, Points, Segment, Bend, Box, Draw };
        Kind kind = Kind::None;
        int row = -1;
        model::AutomationPoints base;
        std::set<size_t> selection, boxBase;
        size_t index = 0; // the grabbed point, or the segment's first point
        float bend = 0.0f;
        model::AutomationPoints drawn;
        std::optional<size_t> clickedInSelection;
    };
    LaneDrag laneDrag_;
    /// What a lane gesture shows until the snapshot has it: the row's key and the edit.
    std::optional<std::pair<std::string, model::PointEdit>> lanePreview_;
    std::optional<std::pair<int, size_t>> hoveredPoint_; // row, point
    std::optional<int> resizeRow_;
    double resizeFrom_ = 0.0;
    WaveformCache waveforms_;
    WaveformTiles tiles_;
    double lastPixelsPerSecond_ = 0.0, lastZoomChange_ = 0.0;
    bool zoomSettled_ = false;
    model::Timeline timeline_;
    model::TrackLayout layout_;
    std::vector<model::Timeline::GridLine> grid_; // paint scratch
    double playhead_ = 0.0;
    double lastPaintMs_ = 0.0;
    time::PlayheadTracker playheadTracker_;
    bool snap_ = true;
    model::Selection selection_, notified_;

    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    enum class LoopZone { None, Body, Start, End };
    [[nodiscard]] LoopZone loopZoneAt(float x) const;

    bool follow_ = true;
    std::optional<double> scrollTarget_; // follow-playhead paging, eased
    std::pair<double, double> loop_{0.0, 0.0};
    bool looping_ = false;
    std::optional<std::pair<double, double>> loopPreview_; // while dragging the brace
    LoopZone loopDrag_ = LoopZone::None;
    std::pair<double, double> loopAtDragStart_{0.0, 0.0};

    enum class Drag { None, Ruler, Clip, Box, Loop, Lane, Resize };
    Drag drag_ = Drag::None;
    juce::Point<float> lastDrag_, dragStart_;
    time::RulerGesture rulerGesture_;
    model::Drag clipDrag_;
    int dragStartTrack_ = 0;
    bool dragMoved_ = false;
    std::vector<model::ClipEdit> preview_; // where dragged clips will land
    std::set<std::uint64_t> boxBase_;      // selection before a Shift box drag
    std::optional<std::uint64_t> boxChannel_; // the channel while a box holds no clips
    std::optional<std::uint64_t> clickedInSelection_; // a click without a drag selects just it

    // File drag preview: target track index (or -1 = new track) and snapped time.
    std::optional<std::pair<int, double>> dropTarget_;
};
} // namespace fd::app
