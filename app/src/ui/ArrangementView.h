#pragma once

#include "engine/Session.h"
#include "ui/AutomationLane.h"
#include "ui/ChannelControls.h"
#include "ui/LaneControls.h"
#include "model/BraceDrag.h"
#include "model/ArrangementRows.h"
#include "model/Editing.h"
#include "model/FadeEditing.h"
#include "model/Selection.h"
#include "model/Takes.h"
#include "model/Timeline.h"
#include "model/TrackEdits.h"
#include "model/TrackLayout.h"
#include "ui/CrossfadeSums.h"
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
class ArrangementView final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              public juce::DragAndDropTarget,
                              public juce::TooltipClient
{
public:
    /// What a take strip's audition button does (the rest of the view has no tooltips).
    juce::String getTooltip() override;
    /// The track header column's width (shell metrics, docs/design/ui-foundations.md).
    [[nodiscard]] static int headerWidth() { return ui::Theme::current().shell.trackHeaderWidth; }
    /// The ruler zone: the loop strip on top of the ruler row.
    [[nodiscard]] static int rulerHeight()
    {
        const auto& s = ui::Theme::current().shell;
        return s.loopStripHeight + s.rulerHeight;
    }
    [[nodiscard]] static int loopStripHeight() { return ui::Theme::current().shell.loopStripHeight; }
    /// Where the rows begin with the song row at its default height (ADR 0049): under the ruler and
    /// the song row.
    [[nodiscard]] static int defaultRowsTop()
    {
        return rulerHeight() + ui::Theme::current().shell.songRowHeight;
    }

    explicit ArrangementView(engine::Session& session);

    void setSnapshot(std::shared_ptr<const model::Snapshot> snapshot);

    /// Once per display frame (message thread): animations and the playhead.
    void frame(double dt, double playheadSeconds);

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    /// How much of its right end something covers (the inspector over it at compact widths,
    /// selection.md §8.1): follow-playhead pages turn before the playhead goes under it, and Zoom
    /// to fit fits the song into what's left.
    void setCoveredRight(int pixels);
    [[nodiscard]] int coveredRight() const noexcept { return coveredRight_; }
    /// Follow the playhead while playing (pages the view smoothly).
    void setFollowEnabled(bool on) noexcept { follow_ = on; }
    [[nodiscard]] bool isFollowEnabled() const noexcept { return follow_; }
    void setSnapEnabled(bool on);
    [[nodiscard]] bool isSnapEnabled() const noexcept { return timeline_.grid().snap; }
    /// The arrangement's grid (ADR 0047), kept by the Session: setting it here changes the
    /// Session's; the view follows the Session's every frame (assistants set it there too).
    void setGrid(const model::GridSettings& grid);
    [[nodiscard]] const model::GridSettings& grid() const noexcept { return timeline_.grid(); }
    /// The grid's menu (the step chip's; a Grid submenu on right-click).
    [[nodiscard]] juce::PopupMenu gridMenu();
    /// Snap and the grid's step, in the corner above the track headers (menu-bar.md §15); Snap's
    /// tooltip is the shell's.
    [[nodiscard]] ui::ViewOption& snapToggle() noexcept { return snapToggle_; }
    [[nodiscard]] ui::ViewChoice& gridChoice() noexcept { return gridChoice_; }
    /// A clip being bounced shows a cancel button at its progress (menu-bar.md §15): where it is
    /// for the clip box `box`, and whether `p` is on one (tests drive it).
    [[nodiscard]] static juce::Rectangle<float> cancelButton(juce::Rectangle<float> box);
    [[nodiscard]] bool cancelAt(juce::Point<float> p) const;
    /// The cancel button's tooltip (the shell's action, with its key).
    juce::String cancelTooltip;

    // Tracks and clips (C11, ADR 0041, docs/design/tracks-and-clips.md).
    /// A track's or return's header was clicked last (it alone selected): Delete and Duplicate
    /// act on the track (§5, §7).
    [[nodiscard]] bool headerActive() const noexcept;
    /// Renames in place (§4): a channel's or lane's name in its header, a clip's title (with the
    /// other clips `with` it, renamed together). False if it isn't shown or can't be renamed.
    bool renameChannel(std::uint64_t channel);
    bool renameLane(std::uint64_t lane);
    bool renameClips(std::uint64_t clip, const std::vector<std::uint64_t>& with);
    /// F2: renames what's selected (clips, a lane, a channel). False if nothing can be.
    bool renameSelection();
    [[nodiscard]] ui::NameEditor& nameEditor() noexcept { return names_; }
    /// Where a row's name is in its header, and a clip's title (view coordinates; tests).
    [[nodiscard]] std::optional<juce::Rectangle<int>> nameArea(int row) const;
    [[nodiscard]] std::optional<juce::Rectangle<int>> clipTitleArea(std::uint64_t clip) const;
    /// A header being dragged to a new place (§6): Escape cancels it. True if one was.
    bool cancelRowDrag();
    /// Where a dragged header would land now (tests), if one is being dragged.
    [[nodiscard]] std::optional<model::RowDrop> rowDrop() const;
    /// A channel header's right-click items (rename, duplicate, delete, colour): the shell's,
    /// before the items that make rows.
    std::function<void(juce::PopupMenu&, std::uint64_t channel)> onHeaderMenu;

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
    /// Something from the browser dropped (browser.md §10): on a track (0: below the tracks, a new
    /// one) at `seconds`.
    std::function<void(const std::string& item, std::uint64_t track, double seconds)> onBrowserDrop;

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
    /// The selected points, the earliest at time 0 (cut, copy: tracks-and-clips.md §9).
    [[nodiscard]] model::AutomationPoints copyPoints() const;
    /// Pastes points at `seconds` on the selected lane, replacing what was over their span;
    /// they're selected. False without a selected lane. One undo step, "Paste".
    bool pastePoints(const model::AutomationPoints& points, double seconds);
    /// Removes the selected lane (one undo step).
    void removeSelectedLane();
    // The song row (M3 C3, ADR 0049, docs/design/arranger.md): the master's row, pinned under the
    // ruler, with the song's sections over a miniature of every track.
    /// Its header and its lane (view coordinates).
    [[nodiscard]] juce::Rectangle<int> songHeaderArea() const;
    [[nodiscard]] juce::Rectangle<int> songLaneArea() const;
    /// Its height (view state, kept with the row heights as "song").
    [[nodiscard]] int songRowHeight() const;
    /// Selects sections alone, the master their channel, as a click on one does.
    void selectSections(std::set<std::uint64_t> sections);
    [[nodiscard]] bool hasSectionSelection() const noexcept { return !selection_.sections.empty(); }
    /// The selected sections, by start.
    [[nodiscard]] std::vector<std::uint64_t> selectedSections() const;
    /// Renames a section where its name is (F2, a double-click on it). False if it isn't in view.
    bool renameSection(std::uint64_t section);
    /// Where a section's name is, if it's in view (tests).
    [[nodiscard]] std::optional<juce::Rectangle<int>> sectionNameArea(std::uint64_t section) const;
    /// A section being made, moved, copied or resized: Escape cancels it. True if one was.
    bool cancelSectionDrag();
    /// Where a dragged section would land now (tests): a section boundary.
    [[nodiscard]] std::optional<double> sectionDrop() const;
    /// A section's right-click items (the shell's), or for 0 the empty song row's.
    std::function<void(juce::PopupMenu&, std::uint64_t section)> onSectionMenu;
    /// A section was double-clicked in its body (it's selected): loop it.
    std::function<void()> onLoopSection;
    /// A section was clicked, made or moved (Delete, Ctrl+D and F2 act on sections).
    std::function<void()> onSectionActivated;

    // Takes (C5b, ADR 0032, docs/design/recording.md §6): under their clip.
    /// Selects a take (with its track), as a click on its strip does.
    /// A click on a take's strip (ADR 0038): that take for the section of its clip at `seconds`;
    /// on a clip that isn't a comp, the whole take.
    void useTakeAt(std::uint64_t track, std::uint64_t take, double seconds);
    /// Selects a take of a track's clips; `add`: toggles it in the selected takes of that track.
    void selectTake(std::uint64_t track, std::uint64_t take, bool add = false);
    [[nodiscard]] bool hasTakeSelection() const noexcept { return !selection_.takes.empty(); }

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
    /// Return tracks, below the tracks (C3, docs/design/mixer.md §2). The master's row is the
    /// song row, always shown (ADR 0049).
    void setMixRowsVisible(bool visible);
    /// Drops the mixer controls in the headers (before the project is replaced: they hold the
    /// engine's meters). setSnapshot() makes them again.
    void releaseChannelControls()
    {
        headerControls_.clear();
        masterControls_.reset();
    }
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
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

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
    /// A row's lane as its gestures see it.
    [[nodiscard]] LaneGesture::Lane gestureLane(int row) const;
    /// A row's right-click menu (-1: the empty space below the rows): its own items, then the
    /// items that make rows.
    /// A row's menu (or, for -1, the empty space's); from the lanes it ends with the Grid.
    void rowMenu(int row, bool fromLanes = false);
    /// The timeline's grid as the Session has it, and the chip's step.
    void syncGrid();
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
    /// How a clip draws: lighter when selected, dimmed, its outline when selected (a clip with takes
    /// draws one outline around all its parts), and a seam after it where the next clip touches it.
    struct ClipLook
    {
        bool selected = false, dimmed = false, outline = true, seam = false;
        std::optional<juce::Rectangle<float>> badge; // its clip's takes badge: the loop mark goes left of it
    };
    /// A clip: its body, title, contents and fades, as `look` says.
    void paintClip(juce::Graphics& g, const model::ClipInfo& clip, float y, float h, juce::Rectangle<int> visible, ClipLook look);
    /// Calls `draw` for each lap of `clip` in view (ADR 0048): the lap as a clip that doesn't loop,
    /// and `body` cut to it, clipped there. A clip that doesn't loop is its one lap. Laps narrower
    /// than two pixels draw nothing.
    void forEachLap(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                    const std::function<void(const model::ClipInfo& lap, juce::Rectangle<float> lapBody)>& draw) const;
    /// A looped clip's seams (clip-loops.md §5): a notch in the title strip, clear of the name, and a
    /// line through the body, each in view; none while the laps are too close to tell apart.
    void paintSeams(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> box, juce::Colour colour) const;
    /// Where a looped clip's loop mark sits: at its title strip's end, left of its badge (`before`,
    /// if any); none where the title shows no text or the mark would take more than half of it.
    [[nodiscard]] std::optional<juce::Rectangle<float>> loopMarkOf(juce::Rectangle<float> box,
                                                                   std::optional<juce::Rectangle<float>> before) const;
    /// Whether a clip draws as selected: it, or another part of the same clip (ADR 0032), is.
    [[nodiscard]] bool drawnSelected(const model::TrackInfo& track, const model::ClipInfo& clip) const;
    // Clip geometry, for every clip drawn (the shell's clip metrics, ui-foundations.md).
    /// A clip from `start` to `end` (seconds) in a row at `y`, `h` high.
    /// Its edges on whole pixels; with `seam`, it stops a clip gap short of its end.
    [[nodiscard]] juce::Rectangle<float> clipBox(double start, double end, float y, float h, bool seam = false) const;
    /// Whether another clip starts where one ending at `end` does, on screen: a seam (a clip gap of
    /// the canvas) separates them. Parts of one clip (`stack`) join without one.
    [[nodiscard]] bool seamAfter(const model::TrackInfo& track, double end, std::uint64_t stack) const;
    /// A clip box's title strip.
    [[nodiscard]] static juce::Rectangle<float> titleOf(juce::Rectangle<float> box);
    /// Whether a title strip has room for its text.
    [[nodiscard]] static bool titleShowsText(juce::Rectangle<float> title);
    // A clip with takes (ADR 0032): its parts as one clip, its badge, and its takes' strips.
    [[nodiscard]] juce::Rectangle<float> groupBox(const model::ClipGroup& group, const model::TrackInfo& track, float y,
                                                  float h) const;
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
    /// A take strip's name chip (recording.md §6): its audition button (ADR 0038) and its name, on
    /// a backing as tall as its clips, at the left of what's in sight.
    struct StripChip
    {
        juce::Rectangle<float> button, name, backing, hit; // `hit`: where a click auditions
    };
    [[nodiscard]] StripChip stripChip(juce::Rectangle<float> strip, const model::TakeStackInfo& stack, const juce::String& name) const;
    /// The audition button under a point: its track and take.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>> auditionButtonAt(juce::Point<float> p) const;
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
        float baseBend = 0.0f;
        std::optional<model::FadeEdit> edit;
        juce::Point<float> pointer; // where its value tag shows
    };
    /// What a fade gesture is changing, for its value tag ("Fade in 120 ms").
    [[nodiscard]] juce::String fadeDragText() const;
    /// A gesture on a clip of its own for loops (ADR 0048, clip-loops.md §5): a seam's notch (the
    /// loop's length) or the upper part of a right edge (loop the clip as it is, then extend it).
    struct LoopGesture
    {
        std::uint64_t clip = 0;
        model::DragKind kind = model::DragKind::LoopLength;
        int seam = 0;
    };
    [[nodiscard]] std::optional<LoopGesture> loopGestureAt(juce::Point<float> pos, const model::ClipHit& hit) const;
    /// What a clip drag makes of a looped clip: "4 bars · 4 loops", "Loop 1 bar"; empty otherwise.
    [[nodiscard]] juce::String clipDragText() const;
    CrossfadeSums sums_; // what crossfades play, drawn across them
    /// The track as it's drawn: with a fade gesture's preview, if one is on it.
    [[nodiscard]] const model::TrackInfo& shownTrack(const model::TrackInfo& track) const;
    [[nodiscard]] juce::Rectangle<float> bodyOf(const model::ClipInfo& clip, float y, float h) const;
    void paintFades(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body, juce::Colour colour, bool selected);
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

    // The song row (ArrangementSongRow.cpp).
    struct HeaderControls;
    [[nodiscard]] juce::Rectangle<int> songArea() const;
    /// The sections as drawn: the snapshot's, or what a drag on them is making of them.
    [[nodiscard]] std::vector<model::SectionInfo> shownSections() const;
    /// The section under `x` in the song lane, and whether `x` is at its start or end edge.
    struct SectionAt
    {
        std::uint64_t section = 0;
        enum class Zone
        {
            Body,
            Start,
            End
        } zone = Zone::Body;
    };
    [[nodiscard]] std::optional<SectionAt> sectionAt(float x) const;
    void paintSongRow(juce::Graphics& g);
    void paintOverview(juce::Graphics& g, juce::Rectangle<int> area);
    /// Where a section drag lands (the line across the rows), on top of everything.
    void paintSectionDrop(juce::Graphics& g);
    void songMouseMove(const juce::MouseEvent& e);
    void songMouseDown(const juce::MouseEvent& e);
    void songMouseDrag(const juce::MouseEvent& e);
    void songMouseUp(const juce::MouseEvent& e);
    /// The master's mixer controls in the song row's header.
    void placeSongControls();
    /// A gesture on the sections (arranger.md §8): making one, moving or copying a run of them, or
    /// moving an edge.
    struct SectionDrag
    {
        enum class Kind
        {
            Make,
            Move,
            Copy,
            Start,
            End
        } kind = Kind::Make;
        std::uint64_t section = 0;   // the one grabbed (Start, End, Move, Copy)
        double from = 0.0, to = 0.0; // Make: the stretch; Start/End: the section's new marks
        std::optional<double> drop;  // Move/Copy: where it cuts the song (the plan's `to`)
        double landing = 0.0;        // ... and where the run starts then
        double pressTime = 0.0;      // Move/Copy: the time under the press
        bool moved = false;          // dragged past the threshold
        std::optional<std::uint64_t> clickedInSelection; // a click without a drag selects just it
    };
    std::optional<SectionDrag> sectionDrag_;
    juce::Component songLayer_; // clips the master's controls to the song row's header
    std::unique_ptr<HeaderControls> masterControls_;
    std::uint64_t masterControlsFor_ = 0;        // the master they were made for (a new project has another)
    std::optional<std::uint64_t> sectionAnchor_; // Shift extends a run from here
    /// A clip's notes or waveform in `content` (clipContent of its colour, or a role).
    void paintNotes(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body, juce::Colour content);
    void paintWaveform(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body,
                       juce::Rectangle<int> visible, juce::Colour content);

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
    LaneGesture laneGesture_; // the lane gestures, shared with the clip view's controller lanes
    int laneRow_ = -1;        // the row it's on
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
    ui::ViewOption snapToggle_{"Snap"}; // how the view edits: a view option
    ui::ViewChoice gridChoice_;         // the grid's step, opening its menu
    model::Selection selection_, notified_;

    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    [[nodiscard]] model::BraceZone loopZoneAt(float x) const;

    bool follow_ = true;
    int coveredRight_ = 0;
    std::optional<double> scrollTarget_; // follow-playhead paging, eased
    std::pair<double, double> loop_{0.0, 0.0};
    bool looping_ = false;
    std::optional<std::pair<double, double>> loopPreview_; // while dragging the brace
    model::BraceZone loopDrag_ = model::BraceZone::None;
    std::pair<double, double> loopAtDragStart_{0.0, 0.0};

    enum class Drag
    {
        None,
        Ruler,
        Clip,
        Box,
        Loop,
        Lane,
        Resize,
        Fade,
        Swipe,
        Join,
        Row,
        Section,
        SongResize
    };
    /// A header dragged to reorder its row (§6): moving once past the drag threshold.
    struct RowDrag
    {
        int row = 0;
        bool moving = false;
        model::RowDrop drop;
    };
    std::optional<RowDrag> rowDrag_;
    /// The row boundary nearest `y` (view coordinates): b is above row b.
    [[nodiscard]] std::size_t boundaryAt(float y) const;
    [[nodiscard]] float boundaryY(std::size_t boundary) const;
    bool renameRow(int row);
    ui::NameEditor names_{*this};
    bool headerActive_ = false;
    /// The line between two takes of a comp, being dragged (recording.md §7): on release, the
    /// take on the side it moved away from plays up to its new place.
    struct JoinDrag
    {
        std::uint64_t track = 0;
        model::CompJoin join;
        double to = 0.0;     // where it's going (snapped, within the two parts)
        bool moved = false;  // dragged past the threshold
    };
    std::optional<JoinDrag> joinDrag_;
    /// The join between two parts of a comp under `pos`, in a clip's body: its track and it.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, model::CompJoin>> joinAt(juce::Point<float> pos) const;
    /// A press on a take's strip (recording.md §6, §7): a click uses the take, a drag across it
    /// comps that stretch of it in.
    struct TakeSwipe
    {
        std::uint64_t track = 0, take = 0;
        bool onClip = false;    // pressed on one of the take's clips (a click uses it)
        double at = 0.0;        // where it was pressed (seconds): a click uses the take for the section there
        double from = 0.0, to = 0.0; // the stretch, snapped (seconds; either order)
        bool swiping = false;   // dragged past the threshold
    };
    std::optional<TakeSwipe> swipe_;
    Drag drag_ = Drag::None;
    juce::Point<float> lastDrag_, dragStart_;
    time::RulerGesture rulerGesture_;
    model::Drag clipDrag_;
    juce::MouseCursor loopCursor_; // looping a clip by its edge (ui::cursorFor)
    juce::Point<float> hover_;     // where the pointer last moved over the view (tooltips)
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
