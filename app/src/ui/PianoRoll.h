#pragma once

#include "engine/Session.h"
#include "model/NoteEditing.h"
#include "model/Timeline.h"
#include "ui/ClipLoopBrace.h"
#include "ui/time/TimeView.h"

#include <fd_ui/fd_ui.h>

#include <optional>
#include <set>

namespace fd::app
{
/// The clip view's piano roll (C2, docs/design/daw-shell.md "Piano roll"): keyboard, ruler,
/// note grid and velocity lane for one MIDI clip. Gestures preview the model's note functions
/// live and end in one Session::setNotes (one undo step). Time runs in song seconds on its own
/// Timeline, so its bars and grid match the arrangement's.
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

    [[nodiscard]] const std::vector<model::Note>& notes() const noexcept { return notes_; }
    [[nodiscard]] const std::set<std::size_t>& selection() const noexcept { return selection_; }
    [[nodiscard]] bool hasSelection() const noexcept { return !selection_.empty(); }

    // Commands on the selection (each one undo step).
    void deleteSelection();
    void duplicateSelection();
    void selectAll();
    void transpose(int semitones);
    /// Moves the selection by whole grid steps.
    void nudge(int gridSteps);
    /// Quantises the selection, or every note when nothing is selected.
    void quantise();
    /// The selected notes, the earliest starting at 0 (cut, copy: tracks-and-clips.md §9).
    [[nodiscard]] std::vector<model::Note> copySelection() const;
    /// Adds `notes` at `seconds` (the playhead) in the clip, or at its start when the playhead
    /// isn't over it; they're selected. One undo step.
    void pasteNotes(const std::vector<model::Note>& notes, double seconds);

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
    /// The point at `beat` (content beats) in the middle of `pitch`'s row.
    [[nodiscard]] juce::Point<float> pointFor(double beat, int pitch) const;
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
    enum class Gesture { None, Notes, Draw, Box, Velocity, VelocityOffset, Keys, Ruler, Brace };

    [[nodiscard]] model::NoteGeometry geometry() const; // grid-local coordinates
    [[nodiscard]] model::BeatGrid beatGrid(bool bypass) const;
    void syncGrid();
    [[nodiscard]] double gridStep() const;
    [[nodiscard]] double contentStartSeconds() const;
    /// What the clip plays, in the view's time: its loop (ADR 0048), or the clip.
    [[nodiscard]] std::pair<double, double> playedRange() const;
    /// Height of the strip and the ruler above the grid.
    [[nodiscard]] static int topHeight();
    [[nodiscard]] double beatForSeconds(double seconds) const;
    [[nodiscard]] double secondsForBeat(double beat) const;
    void clampPitchView();
    void fitToClip();
    void commit(model::NoteEdit edit);
    [[nodiscard]] const std::vector<model::Note>& shownNotes() const;
    [[nodiscard]] const std::set<std::size_t>& shownSelection() const;
    [[nodiscard]] std::optional<std::size_t> velocityStemAt(float localX) const;
    [[nodiscard]] float velocityAtY(float y) const;

    void previewOn(int pitch, float velocity);
    void previewOff();

    void paintRows(juce::Graphics& g);
    void paintNotes(juce::Graphics& g);
    void paintVelocities(juce::Graphics& g);
    void paintKeys(juce::Graphics& g);
    void paintRuler(juce::Graphics& g);

    engine::Session& session_;
    std::optional<model::ClipInfo> clip_;
    model::TempoMap tempo_;
    /// The song's key where the clip is (key-and-scale.md §7): its rows lit, its root labelled,
    /// note names spelled by it. None: the piano's pattern, as before.
    std::optional<music::Key> key_;
    /// A note's name, spelled by the key.
    [[nodiscard]] juce::String noteName(int pitch) const;
    std::vector<model::Note> notes_;    // the clip's notes: the snapshot's, or our last edit
    std::set<std::size_t> selection_;   // indices into notes_
    std::optional<model::NoteEdit> preview_; // during a gesture
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
    std::set<std::size_t> boxBase_;  // selection before a Shift box drag
    time::RulerGesture rulerGesture_;
    ClipLoopBrace brace_; // the clip's loop brace (clip-loops.md §6)
    float drawVelocity_ = 100.0f / 127.0f;
    std::optional<int> previewPitch_; // the note sounding now
};
} // namespace fd::app
