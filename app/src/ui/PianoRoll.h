#pragma once

#include "engine/Session.h"
#include "model/NoteEditing.h"
#include "model/NoteTools.h"
#include "model/PianoRollOptions.h"
#include "model/Timeline.h"
#include "ui/AutomationLane.h"
#include "ui/ClipLoopBrace.h"
#include "ui/time/TimeView.h"

#include <bitset>
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
    /// In key (§6, §21): only the key's rows, or (off) the whole roll, for the clip shown; turned
    /// off, it's remembered for that clip.
    void setInKey(bool on);
    /// The pads (drum-kit.md §23, ADR 0066), or (off) the whole roll, for a Drum Kit's clip;
    /// remembered for the clip as In key is, in the same place.
    void setPads(bool on);
    void setOthers(bool on);
    /// Others leaves out a track's notes, or shows them again (§8).
    void setOthersHidden(std::uint64_t track, bool hidden);
    /// Others' menu: each other track with notes, shown or not (§8).
    [[nodiscard]] juce::PopupMenu othersMenu();
    void setDraw(model::DrawMode mode);
    void setChordStyle(const model::ChordStyle& style);
    /// The options changed (the header and the menus follow).
    std::function<void()> onOptionsChanged;
    /// The song's key where the clip is (none: no key).
    [[nodiscard]] const std::optional<music::Key>& key() const noexcept { return key_; }
    /// Shows only the key's rows (§21): the song has a key, and the clip isn't turned to the whole
    /// roll (nor a Drum Kit's). A row is then a step of the key: drawing lands on it, ↑/↓ move by
    /// the key's steps.
    [[nodiscard]] bool inKey() const noexcept;

    // ---- a Drum Kit's clip (drum-kit.md §23, ADR 0066) ------------------------------------------

    /// One of the kit's pads with a sound, as its row shows it.
    struct PadRow
    {
        int note = 0;
        juce::String name;
        bool on = true;

        friend bool operator==(const PadRow&, const PadRow&) = default;
    };
    /// The clip's track's Drum Kit (0: none): Pads takes In key's place for it.
    [[nodiscard]] std::uint64_t kit() const noexcept { return kit_; }
    [[nodiscard]] const std::vector<PadRow>& pads() const noexcept { return pads_; }
    /// The pad on a note, if it has a sound.
    [[nodiscard]] const PadRow* padOn(int note) const noexcept;
    /// Shows the kit's pads: a kit's clip not turned to the whole roll. A row is then a pad (and a
    /// pitch with notes and no pad, hatched): drawing lands on it, ↑/↓ move a row; the chord lane,
    /// Draw's chords and Others go (§23.4).
    [[nodiscard]] bool onPads() const noexcept;
    // ---- the bottom lane (§11) -----------------------------------------------------------------

    void setLane(const model::BottomLane& lane);
    /// The lane's menu: Velocity, Chance, Release velocity, then the controllers (the clip's and the
    /// usual ones), then any other controller.
    [[nodiscard]] juce::PopupMenu laneMenu();
    /// The lane's head, beside it in the keys' column.
    [[nodiscard]] ui::LaneChoice& laneChoice() noexcept { return laneChoice_; }
    /// The controller lane shown (its points in content beats) and its selected points.
    [[nodiscard]] model::ControllerLane controllerLane() const;
    [[nodiscard]] const std::set<size_t>& pointSelection() const noexcept { return points_; }
    /// The controller lane was used last and has points selected: Delete deletes them.
    [[nodiscard]] bool hasPointSelection() const noexcept
    {
        return pointsUsed_ && showsController() && !points_.empty();
    }
    /// Where a controller lane's point at `beat` (content beats) and `value` (0..1) is drawn.
    [[nodiscard]] juce::Point<float> lanePointFor(double beat, float value) const;
    /// A controller lane says why it does nothing (Alt on a pedal's steps).
    std::function<void(const juce::String&)> onMessage;
    /// The controller lane's selected points for the clipboard (times from the first, in seconds,
    /// as automation lanes' are), and back: pasted at `seconds` (the playhead) where it's over the
    /// clip, else at its start; false without a controller lane. Duplicate copies them after
    /// themselves. One undo step each (§11).
    [[nodiscard]] model::AutomationPoints copyPoints() const;
    bool pastePoints(const model::AutomationPoints& points, double seconds);
    void duplicatePoints();
    // ---- the tools that transform and generate (§12, §23) --------------------------------------

    /// What a tool works with here: the song's key, its bars, and (a generator) where it writes: the
    /// selected notes' span, else the clip's loop, or the clip.
    [[nodiscard]] model::ToolContext toolContext() const;
    /// A tool's last settings (its defaults the first time), and keeping new ones.
    [[nodiscard]] model::ToolValues toolValues(const model::ToolSpec& tool) const;
    void setToolValues(const model::ToolSpec& tool, const model::ToolValues& values);
    /// Another editor's tool's last settings, kept with these under `key` (the audio editor's, whose
    /// Quantise isn't the notes').
    [[nodiscard]] model::ToolValues keptToolValues(const std::string& key, const model::ToolSpec& tool) const;
    void keepToolValues(const std::string& key, const model::ToolSpec& tool, const model::ToolValues& values);

    /// Notes ▸ Re-roll chance (§10): the clip's notes decided again.
    void rerollChance();
    /// Keep chance as played, or Let chance roll (§26, ADR 0062).
    void keepChance(bool keep);

    /// The Draw choice's menu (§7.2): Note, Chord, Chord under.
    [[nodiscard]] juce::PopupMenu drawMenu();
    /// The drawn chords' voicing menu (§20.2): the voicings, then the inversions.
    [[nodiscard]] juce::PopupMenu voicingMenu();

    [[nodiscard]] const std::vector<model::Note>& notes() const noexcept { return notes_; }
    [[nodiscard]] const std::set<std::size_t>& selection() const noexcept { return selection_; }
    [[nodiscard]] bool hasSelection() const noexcept { return !selection_.empty(); }
    void setSelection(std::set<std::size_t> selection);
    /// The notes or the selection changed (the inspector's NOTES section follows).
    std::function<void()> onSelectionChanged;
    /// Counts changes of the notes and the selection: the Tools pane shows its tool again on them.
    [[nodiscard]] std::uint64_t editVersion() const noexcept { return editVersion_; }

    // Commands on the selection, or on every note when none is selected (each one undo step).
    void deleteSelection();
    void duplicateSelection();
    void selectAll();
    void transpose(int semitones);
    /// ↑/↓ (§6): steps of the scale while In key, else semitones.
    void transposeSteps(int steps);
    /// Moves the selection by whole grid steps.
    void nudge(int gridSteps);
    /// Quantises the selection, or every note when nothing is selected, with the Quantise tool's
    /// settings (§25).
    void quantise();
    /// The tools of the Notes menu (§13).
    void fitToKey();
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
    /// Whether a tool's result or a gesture shows in place of the notes (for tests).
    [[nodiscard]] bool isPreviewing() const noexcept { return preview_.has_value(); }
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
    /// Writes a chord at a place, voiced by `style` (one undo step), and returns where it now is.
    ChordPlace writeChord(const ChordPlace& place, const music::Chord& chord, const model::ChordStyle& style);
    /// A chord hovered in the popover: heard, and its notes shown in place until nullopt.
    void previewChord(const ChordPlace& place, const std::optional<music::Chord>& chord,
                      const model::ChordStyle& style);
    /// Plays pitches through the track's instrument until auditionOff (Make chords…' cards).
    void audition(const std::vector<int>& pitches);
    void auditionOff();
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
    /// The rows shown (§21), low to high: In key, the key's and those of notes outside it; on the
    /// pads, theirs; empty for the whole roll.
    [[nodiscard]] const std::vector<int>& shownRows() const noexcept { return rows_; }
    /// The keys' column: the piano's width, or on the pads as wide as their names need (§23.2).
    [[nodiscard]] int keysWidth() const noexcept;
    /// The bottom lane (velocity, chance, release velocity or a controller), right of the keys.
    [[nodiscard]] juce::Rectangle<int> laneArea() const { return velocityArea(); }
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
        ChordLane,
        Controller // a controller lane's points (LaneGesture)
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
    /// The rows' height and where they are: the pads fill the height (§23.2), the roll centres on
    /// the notes at its own row height.
    void fitRows();
    void setWholeRoll(bool whole);
    /// The clip's track's kit and its pads, from the snapshot; true if they changed.
    bool updateKit(const std::optional<model::ClipInfo>& clip);
    void updateKeysWidth();
    void commit(model::NoteEdit edit);
    /// The notes changed: the rows shown and the chord lane follow.
    void notesChanged();
    /// The rows In key shows: the key's, and those of the notes and of a preview outside it.
    void updateRows();
    void updateGhosts();
    void optionsChanged();
    void selectionChanged();
    [[nodiscard]] const std::vector<model::Note>& shownNotes() const;
    [[nodiscard]] const std::set<std::size_t>& shownSelection() const;
    [[nodiscard]] std::optional<std::size_t> velocityStemAt(float localX) const;
    /// The bottom lane's level (0..1) at a component y.
    [[nodiscard]] float velocityAtY(float y) const;
    /// The note level the bottom lane edits (velocity when it shows a controller).
    [[nodiscard]] model::NoteLevel laneLevel() const noexcept;
    [[nodiscard]] bool showsController() const noexcept
    {
        return options_.lane.kind == model::BottomLane::Kind::Controller;
    }
    /// The controller lane's points in song seconds, as its gestures and painter see them.
    void updateLanePoints();
    /// The bottom lane's head: what it shows, and a lock while the Chance view's clip keeps its roll.
    void updateLaneHead();
    [[nodiscard]] LaneGesture::Lane gestureLane() const;
    [[nodiscard]] LaneGeometry laneGeometry() const;
    /// Keeps an edit of the controller lane (seconds) as one undo step.
    void keepLane(const model::PointEdit& edit, const std::string& undoName);
    void paintControllerLane(juce::Graphics& g);
    /// The content beat under a component x.
    [[nodiscard]] double beatAtX(float x) const;
    /// The chord span under a component x in the lane's first line, if any.
    [[nodiscard]] std::optional<std::size_t> spanAtX(float x) const;
    /// The notes of a chord span, as a place to write in.
    [[nodiscard]] static ChordPlace placeOf(const model::ChordSpan& span);
    /// The selection's span in content beats while two or more notes are selected.
    [[nodiscard]] std::optional<std::pair<double, double>> selectionSpan() const;
    [[nodiscard]] const music::Key* keyOrNull() const noexcept { return key_ ? &*key_ : nullptr; }
    /// What a double-click at a grid-local point draws, on the row clicked: a note, or the chord
    /// Draw makes there. `bypass` (Alt) frees it from the grid.
    [[nodiscard]] model::NoteEdit drawnAt(juce::Point<float> local, bool bypass) const;
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
    void paintPadKeys(juce::Graphics& g); // the pads' names (§23.2)
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
    [[nodiscard]] int nameWidth(const juce::String& text, ui::TextStyle style = ui::TextStyle::Name) const;
    mutable std::map<juce::String, int> widths_;
    /// One line of chord names over `area` (the lane's first or second line).
    void paintChordLine(juce::Graphics& g, juce::Rectangle<int> area,
                        const std::vector<model::ChordSpan>& spans, const std::vector<LaneLabel>& labels,
                        bool own);

    engine::Session& session_;
    std::optional<model::ClipInfo> clip_;
    model::TempoMap tempo_;
    /// The song's key where the clip is (key-and-scale.md §7): In key shows its rows (§21); note
    /// names are spelled by it.
    std::optional<music::Key> key_;
    /// A note's name, spelled by the key.
    [[nodiscard]] juce::String noteName(int pitch) const;
    model::PianoRollOptions options_;
    std::vector<model::Note> notes_;    // the clip's notes: the snapshot's, or our last edit
    std::set<std::size_t> selection_;   // indices into notes_
    std::optional<model::NoteEdit> preview_;        // during a gesture, or a tool's preview
    std::vector<int> rows_;                         // In key or on the pads: the pitches shown
    bool folded_ = false;                           // rows_ are the rows (else every pitch is one)
    std::uint64_t kit_ = 0;                         // the clip's track's Drum Kit (§23)
    std::vector<PadRow> pads_;                      // its pads with sounds, low to high
    std::bitset<128> padNotes_;                     // their notes
    bool padsShown_ = false;                        // the rows were the pads at the last fit...
    std::size_t fittedRows_ = 0;                    // ...and this many
    int keysWidth_ = 0;                             // on the pads: as wide as their names (0: the piano's)
    std::optional<int> pressedPad_;                 // the row pressed last on the pads (Rhythm's pad)
    std::vector<model::ChordSpan> spans_;           // the chord lane, read from notes_
    std::vector<Ghost> ghosts_;                     // the other tracks' notes (§8)
    std::vector<model::ChordSpan> allSpans_;        // the chords of everything shown, with Others on
    std::vector<LaneLabel> spanLabels_, allLabels_; // their labels
    std::optional<ChordPlace> chordTarget_;         // where the open Chord popover writes
    model::Timeline timeline_;
    std::vector<model::Timeline::GridLine> lines_; // paint scratch
    ui::LaneChoice laneChoice_;                    // the bottom lane's head (§11)
    model::AutomationPoints lanePoints_;           // the controller lane's points, in song seconds
    std::optional<model::PointEdit> lanePreview_;  // a controller lane gesture, while it lasts
    std::optional<model::PointEdit> laneKept_;     // the last edit kept, until the snapshot has it
    std::set<size_t> points_;                      // its selected points
    std::optional<size_t> hoveredPoint_;
    LaneGesture laneGesture_;
    bool pointsUsed_ = false; // the controller lane was used last: Delete deletes its points
    std::uint64_t editVersion_ = 0;
    double rowHeight_ = 12.0;
    double rollRowHeight_ = 12.0; // the roll's own, as zoomed: the pads fill the height instead
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
