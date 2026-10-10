#pragma once

#include "engine/Session.h"
#include "ui/AutomationLane.h"
#include "ui/ChannelControls.h"
#include "ui/LaneControls.h"
#include "model/ArrangementRows.h"
#include "model/Editing.h"
#include "model/FadeEditing.h"
#include "model/Selection.h"
#include "model/Takes.h"
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
    /// Adds the items that make rows (Add track, Add automation lane, Add return track) to a
    /// right-click menu: every row header's, and the empty space's below the rows.
    std::function<void(juce::PopupMenu&)> onAddMenu;
    /// Fills a clip's right-click menu (the clip is selected first; bounce.md §10).
    std::function<void(juce::PopupMenu&)> onClipMenu;
    /// A lane header's "Assign parameter..." and "Rename...".
    std::function<void(std::uint64_t lane)> onAssignLane, onRenameLane;
    /// A take's "Rename..." (its clip's take menu).
    std::function<void(std::uint64_t track, std::uint64_t take)> onRenameTake;
    /// Something worth telling the user (shown in the shell, never modal).
    std::function<void(const juce::String&)> onMessage;
    /// A clip was double-clicked or created: show it in the clip view.
    std::function<void(std::uint64_t clip)> onOpenClip;

    // Edit commands on the selection (C4), each one undo step.
    /// Deletes the selected clips, or the selected take lane.
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

    // Automation lanes (C4, ADR 0029, docs/design/automation.md): rows of their own.
    /// Selects a lane alone, as a click on its header does, and brings it into sight (once the
    /// snapshot has it, for a lane just made).
    void selectLane(std::uint64_t lane);
    /// Shows or hides every lane (view state).
    void setLanesVisible(bool visible);
    [[nodiscard]] bool lanesVisible() const noexcept { return showLanes_; }
    [[nodiscard]] bool hasLaneSelection() const noexcept { return selection_.lane.has_value(); }
    [[nodiscard]] bool hasPointSelection() const noexcept { return selection_.lane && !selection_.points.empty(); }
    /// Point commands on the selected lane, each one undo step.
    void deletePoints();
    void duplicatePoints();
    void selectAllPoints();
    /// Moves the selected points by grid steps and by a value (lane units).
    void nudgePoints(int gridSteps, float value);
    /// Removes the selected lane (one undo step).
    void removeSelectedLane();
    // Takes (C5b, ADR 0032, docs/design/recording.md §6): under their clip.
    /// Selects a take (with its track), as a click on its strip does.
    void selectTake(std::uint64_t track, std::uint64_t take);
    [[nodiscard]] bool hasTakeSelection() const noexcept { return selection_.take.has_value(); }

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
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    /// The channel of a track, return or master row (null for lane rows).
    [[nodiscard]] const model::TrackInfo* rowChannel(int row) const noexcept;
    /// A lane row's lane (null for other rows).
    [[nodiscard]] const model::AutomationLaneInfo* rowLane(int row) const noexcept;
    [[nodiscard]] juce::Rectangle<float> rowBounds(int row, juce::Rectangle<int> column) const;
    [[nodiscard]] LaneGeometry laneGeometry(int row) const;
    /// The selected lane, if the snapshot has it.
    [[nodiscard]] const model::AutomationLaneInfo* selectedLane() const noexcept;
    /// Whether a row shows as selected.
    [[nodiscard]] bool rowSelected(const model::ArrangementRow& row) const noexcept;
    void laneMouseDown(const juce::MouseEvent& e, int row);
    void laneMouseDrag(const juce::MouseEvent& e);
    void laneMouseUp(const juce::MouseEvent& e);
    /// A row's right-click menu (-1: the empty space below the rows): its own items, then the
    /// items that make rows.
    void rowMenu(int row);
    /// Scrolls a row fully into sight.
    void revealRow(int row);
    void applyLaneEdit(int row, const model::PointEdit& edit, const std::string& undoName);
    /// Bending a step lane: says why nothing bends.
    void explainSteps(const model::AutomationLaneInfo& lane);
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
    void paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h, juce::Rectangle<int> visible, bool selected,
                   bool dimmed = false);
    // Clip geometry, for every clip drawn (the shell's clip metrics, ui-foundations.md).
    /// A clip from `start` to `end` (seconds) in a row at `y`, `h` high.
    [[nodiscard]] juce::Rectangle<float> clipBox(double start, double end, float y, float h) const;
    /// A clip box's title strip.
    [[nodiscard]] static juce::Rectangle<float> titleOf(juce::Rectangle<float> box);
    /// Whether a title strip has room for its text.
    [[nodiscard]] static bool titleShowsText(juce::Rectangle<float> title);
    // A clip with takes (ADR 0032): its parts as one clip, its badge, and its takes' strips.
    [[nodiscard]] juce::Rectangle<float> groupBox(const model::ClipGroup& group, float y, float h) const;
    /// A clip's badge and where it sits in its title strip (none below two takes, or too small).
    [[nodiscard]] std::optional<std::pair<model::Badge, juce::Rectangle<float>>>
    badgeOf(const model::ClipGroup& group, const model::TrackInfo& track, juce::Rectangle<float> box) const;
    void paintGroup(juce::Graphics& g, const model::ClipGroup& group, const model::TrackInfo& track, float y, float h,
                    juce::Rectangle<int> visible);
    /// A track row's open takes, below its clips: a strip per take, newest on top.
    void paintStrips(juce::Graphics& g, int row, const model::TrackInfo& track, juce::Rectangle<int> area,
                     juce::Rectangle<int> visible);
    /// Where a strip's take name goes.
    [[nodiscard]] juce::Rectangle<float> stripLabel(juce::Rectangle<float> strip) const;
    /// The take strip under a point.
    struct StripAt
    {
        std::uint64_t track = 0;
        model::StripHit hit;
    };
    [[nodiscard]] std::optional<StripAt> stripAt(juce::Point<float> p) const;
    /// The badge under a point: its track and stack.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>> badgeAt(juce::Point<float> p) const;
    /// A clip's take menu; `take`: the strip right-clicked (else the take it plays).
    void takeMenu(std::uint64_t track, std::uint64_t stack, std::optional<std::uint64_t> take);
    void clipMenu();

    // Fades and crossfades (C6, fades.md §3, §4; ArrangementFades.cpp).
    struct FadeGrip
    {
        model::FadeHandle handle;
        std::uint64_t track = 0;
    };
    struct FadeRegion
    {
        enum class Kind { In, Out, Cross };
        Kind kind = Kind::In;
        std::uint64_t clip = 0, track = 0;
    };
    struct FadeDrag
    {
        model::FadeHandle handle;
        std::uint64_t track = 0;
        double grabbedAt = 0.0;
        float baseBend = 0.0f;
        std::optional<model::FadeEdit> edit;
    };
    /// The track as it's drawn: with a fade gesture's preview, if one is on it.
    [[nodiscard]] const model::TrackInfo& shownTrack(const model::TrackInfo& track) const;
    [[nodiscard]] juce::Rectangle<float> bodyOf(const model::ClipInfo& clip, float y, float h) const;
    void paintFades(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body);
    void paintCrossfades(juce::Graphics& g, const model::TrackInfo& track, float y, float h);
    void paintFadeHandles(juce::Graphics& g, const model::TrackInfo& track, float y, float h);
    [[nodiscard]] bool showsFadeHandles(const model::TrackInfo& track, const model::FadeHandle& handle) const;
    [[nodiscard]] std::optional<juce::Point<float>> handlePoint(const model::TrackInfo& track, const model::FadeHandle& handle,
                                                                float y, float h) const;
    [[nodiscard]] std::optional<FadeGrip> fadeGripAt(juce::Point<float> pos) const;
    [[nodiscard]] std::optional<FadeRegion> fadeRegionAt(juce::Point<float> pos) const;
    void fadeMenu(const FadeRegion& region);
    bool fadeMouseDown(const juce::MouseEvent& e);
    void fadeMouseDrag(const juce::MouseEvent& e);
    void fadeMouseUp();
    std::optional<std::uint64_t> hoveredClip_; // its fade handles show
    std::optional<FadeDrag> fadeDrag_;
    std::optional<model::TrackInfo> fadePreview_;
    /// A clip's name in its title strip, after "M" when it's muted (bounce.md §6).
    static void paintTitleText(juce::Graphics& g, juce::Rectangle<float> title, const std::string& name, bool muted);
    /// The progress of the bounce a clip is part of, once it has run for motion.progressDelay.
    [[nodiscard]] std::optional<float> bounceProgress(std::uint64_t clip) const;
    std::vector<engine::Session::Bouncing> bouncing_; // read every frame
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
    bool showLanes_ = true;

    /// One channel's controls in its header: mute, solo, volume, pan and the meter (and for a
    /// track its arm button).
    struct HeaderControls
    {
        HeaderControls(engine::Session& session, const model::TrackInfo& channel);
        ChannelToggle mute, solo, arm;
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
    std::optional<std::uint64_t> laneToReveal_; // a lane selected before its row exists
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

    enum class Drag { None, Ruler, Clip, Box, Loop, Lane, Resize, Fade };
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
