#pragma once

#include "model/NoteEditing.h"

#include <fd/music/Chords.h>

#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fd::model
{
/// The piano roll's tools (docs/design/piano-roll.md, ADR 0058): pure functions on a clip's notes,
/// as NoteEditing's are. Each acts on the selected notes, or on every note when none is selected
/// (then nothing is selected after, as Quantise does). The view previews their result; the Session
/// applies it as one undo step.

// ---- In key (§6) ----------------------------------------------------------------------------

/// Which rows the piano roll shows (§6, Fold).
enum class Fold
{
    All,   // every pitch
    Key,   // the key's notes (and any pitch with notes, so none is hidden)
    Notes  // the pitches with notes
};

/// The pitches a fold shows, low to high. Key without a key, or Notes without notes, shows all.
[[nodiscard]] std::vector<int> foldedRows(Fold fold, const music::Key* key, const std::vector<Note>& notes);

/// The arrows with Key on (§6): the notes moved `steps` notes of the key's scale, as a group,
/// limited so every note stays within 0..127.
[[nodiscard]] NoteEdit transposeInKey(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                      const music::Key& key, int steps);

/// Fit to key (§6): every note outside the key moves to its nearest note. At an equal distance it
/// goes the way its melody was moving (up after a rising step from the note before, down after a
/// falling one), else down.
[[nodiscard]] NoteEdit fitToKey(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                const music::Key& key);

// ---- Chords (§7) ----------------------------------------------------------------------------

/// Where notes sounding together make a chord, as the chord lane names it (§7.1).
struct ChordSpan
{
    double start = 0.0; // beats
    double end = 0.0;
    music::Chord chord;
    std::vector<std::size_t> notes; // the notes in it (indices into the notes it was read from)
};

/// The chords the notes make, in time order (§7.1): wherever three or more pitch classes sound
/// together (muted notes don't sound). Spans of the same chord join, across a gap of up to a beat
/// with nothing named in it (a chord played as stabs is one chord).
[[nodiscard]] std::vector<ChordSpan> chordSpans(const std::vector<Note>& notes);

/// How chords are written: the Chord popover's Kind, Voicing and Inversion (§7.3).
struct ChordStyle
{
    music::ChordShape shape = music::ChordShape::Triad;
    music::Voicing voicing = music::Voicing::Smooth;
    music::Inversion inversion = music::Inversion::Auto;

    friend bool operator==(const ChordStyle&, const ChordStyle&) = default;
};

/// The pitches of the chord sounding just before `beat` (none if no chord is), leaving out the
/// notes in `ignoring`: what a chord written there is voice-led from.
[[nodiscard]] std::vector<int> pitchesBefore(const std::vector<Note>& notes, double beat,
                                             const std::set<std::size_t>& ignoring = {});
/// That chord itself.
[[nodiscard]] std::optional<music::Chord> chordBefore(const std::vector<Note>& notes, double beat,
                                                      const std::set<std::size_t>& ignoring = {});

/// A chord drawn by a double-click (§7.2): the key's chord on the clicked note (`under`: the key's
/// chord with the note on top), voiced by the style from the chord before, the clicked pitch kept.
/// Its notes take the clicked note's start, length and velocity, and are selected.
[[nodiscard]] NoteEdit drawChord(const std::vector<Note>& notes, const Note& clicked, const music::Key* key,
                                 const ChordStyle& style, bool under);

/// How a chord written over a place would be voiced (§7.3; the popover's hover plays these):
/// replacing the notes `replacing` (as many notes as they were, or the chord's own if more),
/// voice-led from the chord before `start`.
[[nodiscard]] std::vector<int> chordVoicing(const std::vector<Note>& notes, double start, const music::Chord& chord,
                                            const std::set<std::size_t>& replacing, const ChordStyle& style);

/// Writes a chord over [start, end) (§7.1, §7.3): in place of the notes `replacing`, keeping their
/// timing and velocities (each pitch takes the nearest old note's), else as new notes of
/// `velocity`. The chord's notes are selected.
[[nodiscard]] NoteEdit writeChord(const std::vector<Note>& notes, double start, double end, const music::Chord& chord,
                                  const std::set<std::size_t>& replacing, const ChordStyle& style, float velocity);

/// Make chords (§7.4): each note becomes the root of the key's chord on it, keeping its timing and
/// velocity, voiced by the style (Smooth: from the chord before, so a line becomes a progression).
[[nodiscard]] NoteEdit makeChords(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                  const music::Key* key, const ChordStyle& style);

/// The notes in chords: groups that start together (within chordSpread), in time order.
inline constexpr double chordSpread = 1.0 / 32.0;
[[nodiscard]] std::vector<std::vector<std::size_t>> chordGroups(const std::vector<Note>& notes,
                                                                const std::set<std::size_t>& which);

/// Invert up / down (§7.4): in each chord, the lowest note up an octave, or the highest down.
[[nodiscard]] NoteEdit invertChords(const std::vector<Note>& notes, const std::set<std::size_t>& selection, bool up);

/// Voicing ▸ (§7.4): each chord re-voiced, keeping its notes' count and register. Smooth keeps
/// the first chord and voices each next one to move least from the one before.
[[nodiscard]] NoteEdit voiceChords(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                   music::Voicing voicing);

/// Add bass (§7.4): each chord's root, an octave or more under its lowest note, for its time.
/// The bass notes are selected.
[[nodiscard]] NoteEdit addBass(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

// ---- Editing (§9) ---------------------------------------------------------------------------

/// Join (§9.1): notes of the same pitch that touch or overlap become one, with the first's velocity.
[[nodiscard]] NoteEdit joinNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

/// Split at the playhead (§9.1): notes crossing `beat` become two there.
[[nodiscard]] NoteEdit splitNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double beat);

/// Legato (§9.1): each note lasts until the next one starts (the last ones keep their lengths).
[[nodiscard]] NoteEdit legatoNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

/// Fixed length (§9.1): every note `length` long.
[[nodiscard]] NoteEdit fixedLength(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double length);

/// Stretching in time (§9.1 Double/Half speed, §9.2 time handles): every moment t goes to
/// pivot + (t − pivot) × factor; a negative factor also reverses the order. Lengths scale too when
/// `lengths`, else each note keeps its own.
[[nodiscard]] NoteEdit stretchNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                    double pivot, double factor, bool lengths);

/// Reverse (§9.1): the notes' order in time mirrored within their span.
[[nodiscard]] NoteEdit reverseNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

/// Mirror pitches (§9.1): upside down around the middle of their range; with a key, by its steps.
[[nodiscard]] NoteEdit mirrorPitches(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                     const music::Key* key);

/// A velocity line (§9.3, Shift+drag): the notes starting between the two points (`only` those, if
/// given) get the velocity on the line between them. The order and selection are unchanged.
[[nodiscard]] std::vector<Note> lineVelocity(const std::vector<Note>& notes, double beat0, float velocity0,
                                             double beat1, float velocity1, const std::set<std::size_t>* only = nullptr);

/// Mute notes (§9.4): mutes them, or when they're all muted already, unmutes them.
[[nodiscard]] NoteEdit muteNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

/// Quantise… (§9.5): how far notes move to the grid, its swing, and what moves.
struct QuantiseSettings
{
    double strength = 1.0; // 0..1: the share of the way to the grid line
    double swing = 0.0;    // 0..1: every other line later, by up to a third of a step (a triplet feel)
    bool starts = true;
    bool ends = false;

    friend bool operator==(const QuantiseSettings&, const QuantiseSettings&) = default;
};

[[nodiscard]] NoteEdit quantiseNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                     const BeatGrid& grid, const QuantiseSettings& settings);

// ---- The inspector's NOTES section (§9.6) --------------------------------------------------

/// A note's value the inspector shows and edits.
enum class NoteField
{
    Pitch,    // MIDI 0..127
    Start,    // content beats
    Length,   // beats
    Velocity  // MIDI 1..127
};

/// A field's value on a note, in the inspector's units.
[[nodiscard]] double fieldOf(const Note& note, NoteField field) noexcept;
/// The lowest and highest value of a field over the notes (none for no notes).
[[nodiscard]] std::optional<std::pair<double, double>> fieldRange(const std::vector<Note>& notes,
                                                                  const std::set<std::size_t>& which, NoteField field);
/// Typed: every selected note's field set to `value`, kept in range.
[[nodiscard]] NoteEdit setNotesField(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                     NoteField field, double value);
/// Dragged: every selected note's field moved by the same amount, as far as all of them can go.
[[nodiscard]] NoteEdit offsetNotesField(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                        NoteField field, double delta);

// ---- Selecting (§5) -------------------------------------------------------------------------

/// Notes of a pitch any selected note has.
[[nodiscard]] std::set<std::size_t> selectSamePitch(const std::vector<Note>& notes, const std::set<std::size_t>& selection);
/// Notes outside the key.
[[nodiscard]] std::set<std::size_t> selectOutsideKey(const std::vector<Note>& notes, const music::Key& key);
/// Notes starting within [from, to).
[[nodiscard]] std::set<std::size_t> selectStartingIn(const std::vector<Note>& notes, double from, double to);
/// Every note not selected.
[[nodiscard]] std::set<std::size_t> invertSelection(const std::vector<Note>& notes, const std::set<std::size_t>& selection);

/// The span of the notes, from the first start to the last end; none for no notes.
[[nodiscard]] std::optional<std::pair<double, double>> notesSpan(const std::vector<Note>& notes,
                                                                 const std::set<std::size_t>& which);
} // namespace fd::model
