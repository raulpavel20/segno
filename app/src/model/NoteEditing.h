#pragma once

#include "model/Snapshot.h"

#include <cstddef>
#include <optional>
#include <set>
#include <vector>

namespace fd::model
{
/// Note editing in the piano roll (C2, docs/design/daw-shell.md "Piano roll"). Pure functions
/// on a clip's notes; the view previews their result and the Session applies it as one undo
/// step. Times are in beats from the clip's content start (see Note).

/// Shortest note any edit leaves.
inline constexpr double minNoteLength = 1.0 / 64.0;

/// The order notes are kept in: start, then pitch, length and velocity (a total order, so two
/// lists with the same notes compare equal).
[[nodiscard]] bool noteOrder(const Note& a, const Note& b) noexcept;

/// A velocity rounded to a MIDI step (k / 127), as the engine stores it.
[[nodiscard]] float midiVelocity(float velocity) noexcept;

/// The grid notes snap to. `origin` is the song beat of the clip's content start, so lines
/// fall on the song's grid, not the clip's; lines below a bar restart at every bar (ADR 0047).
struct BeatGrid
{
    double step = 0.25; // beats; 0 = no snapping
    double origin = 0.0;
    double bar = 0.0; // beats per bar; 0 = the lines never restart

    [[nodiscard]] double round(double beat) const noexcept;
    /// The grid line at or before `beat`.
    [[nodiscard]] double floor(double beat) const noexcept;
    /// The first grid line after `beat`.
    [[nodiscard]] double next(double beat) const noexcept;
};

/// The piano roll's mapping: content beats <-> x, pitches <-> y.
struct NoteGeometry
{
    double originX = 0.0;       // x of content beat 0
    double pixelsPerBeat = 80.0;
    double topEdge = 84.0;      // pitch boundary at y = 0: pitch p's row spans (topEdge - p - 1) * rowHeight onwards
    double rowHeight = 12.0;

    [[nodiscard]] double xForBeat(double beat) const noexcept { return originX + beat * pixelsPerBeat; }
    [[nodiscard]] double beatForX(double x) const noexcept { return (x - originX) / pixelsPerBeat; }
    [[nodiscard]] double yForPitch(int pitch) const noexcept { return (topEdge - pitch - 1) * rowHeight; }
    [[nodiscard]] int pitchForY(double y) const noexcept;
};

struct NoteHit
{
    enum class Zone { Body, StartEdge, EndEdge };
    std::size_t index = 0;
    Zone zone = Zone::Body;
};

/// The note under (x, y). The edges are the outer `edgePixels` of a note (less for short notes).
[[nodiscard]] std::optional<NoteHit> hitNote(const std::vector<Note>& notes, const NoteGeometry& geometry, double x,
                                             double y, double edgePixels = 5.0);

/// The result of an edit: the new notes (in noteOrder) and the indices of the notes to select.
struct NoteEdit
{
    std::vector<Note> notes;
    std::set<std::size_t> selection;
};

enum class NoteDragKind { Move, ResizeStart, ResizeEnd };

struct NoteDrag
{
    NoteDragKind kind = NoteDragKind::Move;
    std::size_t anchor = 0;  // the grabbed note: it snaps; the others follow by the same amount
    double deltaBeats = 0.0; // mouse movement in time
    int deltaPitch = 0;      // mouse movement in rows (moves only)
};

/// The selected notes after a drag. Nothing goes before beat 0 or outside pitches 0..127; resizes
/// never leave a note shorter than minNoteLength (the anchor: one grid step when snapping).
/// The moved notes win over notes of the same pitch they overlap (see resolveOverlaps).
[[nodiscard]] NoteEdit dragNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                 const NoteDrag& drag, const BeatGrid& grid);

/// A new note, selected alone.
[[nodiscard]] NoteEdit addNote(const std::vector<Note>& notes, const Note& note);

[[nodiscard]] NoteEdit deleteNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

/// Copies of the selection right after it, selected. The copies move by the selection's span
/// rounded up to the grid step, or to whole bars once the span is half a bar or more (so a
/// phrase stays on its beats).
[[nodiscard]] NoteEdit duplicateNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                      double gridStep, double beatsPerBar);

/// Moves the selection by `semitones`, limited so every note stays within 0..127.
[[nodiscard]] NoteEdit transposeNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                      int semitones);

/// Moves the selection by `beats`, limited so no note starts before beat 0.
[[nodiscard]] NoteEdit nudgeNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double beats);

/// Moves note starts to the nearest grid line (lengths kept): the selection, or every note when
/// nothing is selected (the selection stays as it was).
[[nodiscard]] NoteEdit quantiseNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                     const BeatGrid& grid);

/// Notes overlapping the beat range [beat0, beat1] with pitches in [pitch0, pitch1] (any order).
[[nodiscard]] std::set<std::size_t> notesInBox(const std::vector<Note>& notes, double beat0, double beat1, int pitch0,
                                               int pitch1);

/// Velocity drawing: notes starting within [beat0, beat1] (any order) get `velocity`; with
/// `only` set, just those notes. The order and selection are unchanged.
[[nodiscard]] std::vector<Note> paintVelocity(const std::vector<Note>& notes, double beat0, double beat1,
                                              float velocity, const std::set<std::size_t>* only = nullptr);

/// Velocity dragging: the given notes' velocities moved by `delta` (each clamped to 1/127..1).
[[nodiscard]] std::vector<Note> offsetVelocity(const std::vector<Note>& notes, const std::set<std::size_t>& which,
                                               float delta);

/// Where a MIDI clip should end so its notes play (open question B15, decided 2026-10-09):
/// when the last note ends past the clip, the end of the bar it ends in, unless another clip
/// on the track (`trackClips`) is in the way. nullopt leaves the clip as it is (notes past its
/// end are kept, drawn faded).
[[nodiscard]] std::optional<double> lengthenedClipEnd(const ClipInfo& clip, const std::vector<ClipInfo>& trackClips,
                                                      const std::vector<Note>& notes, const TempoMap& tempo);

/// No overlapping notes of the same pitch (MIDI can't tell them apart). The notes in `placed`
/// win: another note of the same pitch that starts inside a placed note is removed, one that
/// starts before it is shortened to end where it starts. Among placed notes the later one wins
/// the same way. Overlaps between notes that weren't placed are left alone. Returns the notes
/// in noteOrder; the selection is the placed notes that remain.
[[nodiscard]] NoteEdit resolveOverlaps(const std::vector<Note>& notes, const std::set<std::size_t>& placed);
} // namespace fd::model
