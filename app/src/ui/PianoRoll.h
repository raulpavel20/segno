#pragma once

#include "engine/Session.h"
#include "model/NoteEditing.h"
#include "model/NoteTools.h"
#include "model/PianoRollOptions.h"
#include "model/Timeline.h"
#include "ui/ClipLoopBrace.h"
#include "ui/time/TimeView.h"

#include <fd_ui/fd_ui.h>

#include <map>
#include <optional>
#include <set>

namespace fd::app
{
/// The clip view's piano roll (C2, docs/design/daw-shell.md "Piano roll"; P4,
/// docs/design/piano-roll.md, ADR 0058): keyboard, ruler, chord lane, note grid and velocity lane
/// for one MIDI clip. Gestures preview the model's note functions live and end in one
/// Session::setNotes (one undo step). Time runs in song seconds on its own Timeline, so its bars
/// and grid match the arrangement's.
class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll(engine::Session& session);
    ~PianoRoll() override;

    /// The clip to edit (nullopt or an audio clip: nothing). Another clip resets the view to fit
    /// it; the same clip takes its new notes and keeps the selection where notes still match.
    void setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo);
    [[nodiscard]] const std::optional<model::ClipInfo>& clip() const noexcept { return clip_; }
    /// Once per display frame: the playhead.
    void frame(double playheadSeconds);
    /// Where it shows the playhead: over a looped clip's later laps, in its loop (clip-loops.md §6).
    [[nodiscard]] double shownPlayhead() const noexcept { return playhead_; }

    /// The piano roll's grid (ADR 0047), its own and kept by the Session: setting it here changes
    /// the Session's; the view follows the Session's every frame (assistants set it there too).
    /// Wide by default: its step is also the length of a drawn note.
    void setGrid(const model::GridSettings& grid);
    [[nodiscard]] const model::GridSettings& grid() const noexcept { return timeline_.grid(); }
    void setSnapEnabled(bool on);
    [[nodiscard]] bool isSnapEnabled() const noexcept { return timeline_.grid().snap; }
    /// The grid's menu (the clip view header's step chip; a Grid submenu on right-click).
    [[nodiscard]] juce::PopupMenu gridMenu();
    /// The step it snaps to now ("1/16", "1/8T"), for the header's chip.
    [[nodiscard]] juce::String gridStepName() const;

    // ---- the clip view's options (piano-roll.md §4), view state kept with the project ----------

    [[nodiscard]] const model::PianoRollOptions& options() const noexcept { return options_; }
    void setOptions(const model::PianoRollOptions& options);
    void setKeySnap(bool on);
    void setFold(model::Fold fold);
    void setOthers(bool on);
    void setDraw(model::DrawMode mode);
    void setChordStyle(const model::ChordStyle& style);
    void setQuantiseSettings(const model::QuantiseSettings& settings);
    /// The options changed (the header and the menus follow).
    std::function<void()> onOptionsChanged;
    /// The song's key where the clip is (none: no key).
    [[nodiscard]] const std::optional<music::Key>& key() const noexcept { return key_; }
    /// Drawing and moving go by the key's notes: Key is on and the song has a key (§6).
    [[nodiscard]] bool snapsToKey() const noexcept { return options_.keySnap && key_.has_value(); }
    /// The Draw choice's menu (§7.2): Note, Chord, Chord under, and the chord's settings.
    [[nodiscard]] juce::PopupMenu drawMenu();
    /// The Fold choice's menu (§6).
    [[nodiscard]] juce::PopupMenu foldMenu();
    /// The Draw menu's last item: the Chord popover with the chord's settings only.
    std::function<void()> onChordSettings;

    [[nodiscard]] const std::vector<model::Note>& notes() const noexcept { return notes_; }
    [[nodiscard]] const std::set<std::size_t>& selection() const noexcept { return selection_; }
    [[nodiscard]] bool hasSelection() const noexcept { return !selection_.empty(); }
    void setSelection(std::set<std::size_t> selection);
    /// The notes or the selection changed (the inspector's NOTES section follows).
    std::function<void()> onSelectionChanged;

    // Commands on the selection, or on every note when none is selected (each one undo step).
    void deleteSelection();
    void duplicateSelection();
    void selectAll();
    void transpose(int semitones);
    /// ↑/↓ (§6): steps of the scale while it snaps to the key, else semitones.
    void transposeSteps(int steps);
    /// Moves the selection by whole grid steps.
    void nudge(int gridSteps);
    /// Quantises the selection, or every note when nothing is selected, with Quantise…'s settings.
    void quantise();
    /// The tools of the Notes menu (§13).
    void fitToKey();
    void makeChords();
    void invertChords(bool up);
    void voiceChords(music::Voicing voicing);
    void addBass();
    void join();
    /// Splits the notes crossing the playhead (Edit ▸ Split at playhead, §9.1).
    void splitAtPlayhead();
    void legato();
    void fixedLength();
    /// Double speed (0.5), half speed (2): from the first note's start.
    void stretch(double factor);
    void reverse();
    void mirrorPitches();
    void toggleMute();
    /// Whether every note the commands act on is muted (Mute notes then unmutes).
    [[nodiscard]] bool allMuted() const;
    void selectSamePitch();
    void selectOutsideKey();
    void selectInLoop();
    void invertSelection();
    /// What a tool would do, shown until previewEdit(nullopt) or applyEdit (Quantise…).
    void previewEdit(std::optional<model::NoteEdit> edit);
    void applyEdit(model::NoteEdit edit);
    /// The selected notes, the earliest starting at 0 (cut, copy: tracks-and-clips.md §9).
    [[nodiscard]] std::vector<model::Note> copySelection() const;
    /// Adds `notes` at `seconds` (the playhead) in the clip, or at its start when the playhead
    /// isn't over it; they're selected. One undo step.
    void pasteNotes(const std::vector<model::Note>& notes, double seconds);
    /// The grid quantise and Quantise… use.
    [[nodiscard]] model::BeatGrid quantiseGrid() const;

    // ---- chords (§7) -----------------------------------------------------------------------

    /// The chords the clip's notes make, as the chord lane names them.
    [[nodiscard]] const std::vector<model::ChordSpan>& chordSpans() const noexcept { return spans_; }
    /// Where a chord is written: [start, end) in content beats, and the notes it replaces.
    struct ChordPlace
    {
        double start = 0.0, end = 0.0;
        std::set<std::size_t> replacing;

        friend bool operator==(const ChordPlace&, const ChordPlace&) = default;
    };
    /// The chord at a place now (its notes'), if they make one.
    [[nodiscard]] std::optional<music::Chord> chordAt(const ChordPlace& place) const;
    /// The chord sounding before a place (the popover's Next), leaving out the notes it replaces.
    [[nodiscard]] std::optional<music::Chord> chordBefore(const ChordPlace& place) const;
    /// Notes ▸ Make chord… (§7.3): the selected notes' place, else a grid step at the playhead.
    [[nodiscard]] ChordPlace placeForSelection() const;
    /// The chord lane asks for the Chord popover at a place (the shell opens it beside `anchor`,
    /// in this component's coordinates).
    std::function<void(const ChordPlace&, juce::Rectangle<int> anchor)> onChordPopover;
    /// Where a place is in the chord lane (for the popover's anchor and its mark).
    [[nodiscard]] juce::Rectangle<int> laneAreaFor(const ChordPlace& place) const;
    /// The place and the chord before it in the lane: what the popover keeps in view.
    [[nodiscard]] juce::Rectangle<int> laneAreaWithBefore(const ChordPlace& place) const;
    /// Marks the place the popover writes (none: no popover).
    void setChordTarget(std::optional<ChordPlace> place);
    /// Writes a chord at a place (one undo step) and returns where it now is (its notes).
    ChordPlace writeChord(const ChordPlace& place, const music::Chord& chord);
    /// A chord hovered in the popover: heard, and its notes shown in place until nullopt.
    void previewChord(const ChordPlace& place, const std::optional<music::Chord>& chord);
    [[nodiscard]] const model::TempoMap& tempo() const noexcept { return tempo_; }
    /// The song beat a content beat is at (the popover's title).
    [[nodiscard]] double songBeat(double contentBeat) const noexcept;

    /// Called when the piano roll is clicked (the shell routes commands to the area used last).
    std::function<void()> onActivated;
    /// A right-click in the grid: the notes' menu (the shell's, from its actions).
    std::function<void()> onMenu;

    // Geometry, in component coordinates (tests drive the mouse with it).
    [[nodiscard]] juce::Rectangle<int> gridArea() const;
    [[nodiscard]] juce::Rectangle<int> velocityArea() const;
    [[nodiscard]] juce::Rectangle<int> keysArea() const;
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    /// The loop strip above the ruler: the clip's loop brace (ADR 0048, clip-loops.md §6).
    [[nodiscard]] juce::Rectangle<int> loopStripArea() const;
    /// The chord lane under the ruler (§7.1); empty while it isn't shown.
    [[nodiscard]] juce::Rectangle<int> chordLaneArea() const;
    /// Shown while the clip has chords or Draw writes them (§7.1).
    [[nodiscard]] bool showsChordLane() const;
    /// The time handles at each end of the selection in the ruler (§9.2); empty without two notes.
    [[nodiscard]] std::pair<juce::Rectangle<int>, juce::Rectangle<int>> timeHandles() const;
    /// The point at `beat` (content beats) in the middle of `pitch`'s row.
    [[nodiscard]] juce::Point<float> pointFor(double beat, int pitch) const;
    [[nodiscard]] const model::Timeline& timeline() const noexcept { return timeline_; }
    /// The rows shown (piano-roll.md §6, Fold), low to high; empty when every pitch is.
    [[nodiscard]] const std::vector<int>& foldedRows() const noexcept { return rows_; }
    /// The other tracks' notes shown faint (§8): how many, for tests.
    [[nodiscard]] std::size_t ghostCount() const noexcept { return ghosts_.size(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

private:
    enum class Gesture
    {
        None,
        Notes,
        Draw,
        Box,
        Velocity,
        VelocityOffset,
        VelocityLine,
        Keys,
        Ruler,
        Brace,
        TimeHandle,
        ChordLane
    };

    /// Another track's note, in this clip's content beats (§8).
    struct Ghost
    {
        model::Note note;
        int colour = 0;
    };

    [[nodiscard]] model::NoteGeometry geometry() const; // grid-local coordinates
    [[nodiscard]] model::BeatGrid beatGrid(bool bypass) const;
    void syncGrid();
    [[nodiscard]] double gridStep() const;
    [[nodiscard]] double contentStartSeconds() const;
    /// What the clip plays, in the view's time: its loop (ADR 0048), or the clip.
    [[nodiscard]] std::pair<double, double> playedRange() const;
    /// Height of the strip, the ruler and the chord lane above the grid.
    [[nodiscard]] int topHeight() const;
    /// How many lines the chord lane has: the clip's chords, and with Others, everything's.
    [[nodiscard]] int chordLaneLines() const;
    [[nodiscard]] double beatForSeconds(double seconds) const;
    [[nodiscard]] double secondsForBeat(double beat) const;
    void clampPitchView();
    void fitToClip();
    void commit(model::NoteEdit edit);
    /// The notes changed: the folded rows and the chord lane follow.
    void notesChanged();
    void updateRows();
    void updateGhosts();
    void optionsChanged();
    void selectionChanged();
    [[nodiscard]] const std::vector<model::Note>& shownNotes() const;
    [[nodiscard]] const std::set<std::size_t>& shownSelection() const;
    [[nodiscard]] std::optional<std::size_t> velocityStemAt(float localX) const;
    [[nodiscard]] float velocityAtY(float y) const;
    /// The content beat under a component x.
    [[nodiscard]] double beatAtX(float x) const;
    /// The chord span under a component x in the lane's first line, if any.
    [[nodiscard]] std::optional<std::size_t> spanAtX(float x) const;
    /// The notes of a chord span, as a place to write in.
    [[nodiscard]] static ChordPlace placeOf(const model::ChordSpan& span);
    /// The selection's span in content beats while two or more notes are selected.
    [[nodiscard]] std::optional<std::pair<double, double>> selectionSpan() const;
    [[nodiscard]] const music::Key* keyOrNull() const noexcept { return key_ ? &*key_ : nullptr; }
    /// A drawn note's pitch: the key's nearest note while it snaps (Alt: as clicked).
    [[nodiscard]] int drawnPitch(int pitch, bool bypass) const;
    /// The selection a command acts on is shown in the notes it returns.
    void run(model::NoteEdit edit) { commit(std::move(edit)); }

    void previewOn(int pitch, float velocity);
    void previewOff();
    void previewChordOn(const std::vector<int>& pitches, float velocity);

    void paintRows(juce::Graphics& g);
    void paintGhosts(juce::Graphics& g);
    void paintNotes(juce::Graphics& g);
    void paintVelocities(juce::Graphics& g);
    void paintKeys(juce::Graphics& g);
    void paintRuler(juce::Graphics& g);
    void paintChordLane(juce::Graphics& g);
    /// A chord's label in the lane, made when the chords change rather than every frame.
    struct LaneLabel
    {
        juce::String name, step; // "C/E", " · III"
        int nameWidth = 0, stepWidth = 0;
    };
    [[nodiscard]] std::vector<LaneLabel> labelsFor(const std::vector<model::ChordSpan>& spans) const;
    void updateLabels();
    /// A text's width in the lane's style, measured once.
    [[nodiscard]] int nameWidth(const juce::String& text) const;
    mutable std::map<juce::String, int> widths_;
    /// One line of chord names over `area` (the lane's first or second line).
    void paintChordLine(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<model::ChordSpan>& spans,
                        const std::vector<LaneLabel>& labels, bool own);

    engine::Session& session_;
    std::optional<model::ClipInfo> clip_;
    model::TempoMap tempo_;
    /// The song's key where the clip is (key-and-scale.md §7): its rows lit, its root labelled,
    /// note names spelled by it. None: the piano's pattern, as before.
    std::optional<music::Key> key_;
    /// A note's name, spelled by the key.
    [[nodiscard]] juce::String noteName(int pitch) const;
    model::PianoRollOptions options_;
    std::vector<model::Note> notes_;    // the clip's notes: the snapshot's, or our last edit
    std::set<std::size_t> selection_;   // indices into notes_
    std::optional<model::NoteEdit> preview_; // during a gesture, or a tool's preview
    std::vector<int> rows_;             // folded: the pitches shown; empty: every pitch
    std::vector<model::ChordSpan> spans_;    // the chord lane, read from notes_
    std::vector<Ghost> ghosts_;              // the other tracks' notes (§8)
    std::vector<model::ChordSpan> allSpans_; // the chords of everything shown, with Others on
    std::vector<LaneLabel> spanLabels_, allLabels_; // their labels
    std::optional<ChordPlace> chordTarget_;  // where the open Chord popover writes
    model::Timeline timeline_;
    std::vector<model::Timeline::GridLine> lines_; // paint scratch
    double rowHeight_ = 12.0;
    double topEdge_ = 84.0; // see NoteGeometry
    bool needsFit_ = false;
    double lastGridHeight_ = 0.0;
    double playhead_ = 0.0;
    time::PlayheadTracker playheadTracker_;

    Gesture gesture_ = Gesture::None;
    juce::Point<float> dragStart_, lastDrag_;
    bool moved_ = false;
    model::NoteDrag drag_;
    model::NoteEdit base_;           // the notes a gesture started from
    std::optional<std::size_t> clickedInSelection_; // a click without a drag selects just it
    std::optional<std::size_t> toggleOnClick_;      // Ctrl+click without a drag: in or out of the selection
    std::set<std::size_t> boxBase_;  // selection before a Shift box drag
    time::RulerGesture rulerGesture_;
    ClipLoopBrace brace_; // the clip's loop brace (clip-loops.md §6)
    bool handleAtEnd_ = true; // the time handle dragged: the end's (else the start's)
    double stretchFactor_ = 1.0;
    std::optional<std::size_t> hoveredSpan_;
    float drawVelocity_ = 100.0f / 127.0f;
    std::optional<int> previewPitch_;      // the note sounding now
    std::vector<int> previewChordPitches_; // a chord sounding now (the popover's hover)
};
} // namespace fd::app
