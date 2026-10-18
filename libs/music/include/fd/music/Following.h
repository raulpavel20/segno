#pragma once

#include "fd/music/Chords.h"
#include "fd/music/Scales.h"

#include <cstdint>
#include <span>

namespace fd::music
{
// What MIDI effects do with notes as they play (docs/design/midi-effects.md §6, ADR 0074): steps
// of a key, fitting to a key or a chord, mirroring, chords voiced from one note. Pure, allocation
// free and real-time safe, so the modules, the clip view's tools and assistants all run them. Sets
// are absolute pitch classes: bit n is pitch class n (0 = C).

inline constexpr PitchSet allPitchClasses = 0x0FFF;

/// Whether a MIDI pitch's class is in `set`.
[[nodiscard]] constexpr bool inSet(PitchSet set, int pitch) noexcept
{
    return ((set >> (((pitch % 12) + 12) % 12)) & 1u) != 0;
}

/// A set given relative to a root (a scale's or a chord's intervals), as absolute pitch classes.
[[nodiscard]] PitchSet rotated(PitchSet intervals, int root) noexcept;

/// `pitch` moved by `steps` notes of `set`. A pitch outside the set moves as the set's note
/// below it does, keeping how far above that note it is. An empty set moves by semitones.
[[nodiscard]] int stepBy(int pitch, PitchSet set, int steps) noexcept;

enum class Toward : std::uint8_t
{
    Near,
    Up,
    Down
};

/// The pitch of `set` nearest `pitch` (itself if it's in the set), up or down, or either:
/// at an equal distance it goes the way `tie` says (+1 up, −1 down). An empty set: `pitch`.
[[nodiscard]] int fitTo(int pitch, PitchSet set, Toward toward, int tie = -1) noexcept;

/// `pitch` turned upside down around `axis`, by steps of `set` (semitones for an empty set): as
/// many steps below the axis as it was above.
[[nodiscard]] int mirror(int pitch, int axis, PitchSet set) noexcept;

/// Whether a moment is on a beat (to within a sixteenth's tenth): a chord's notes go there,
/// passing notes between.
[[nodiscard]] bool onBeat(double beat) noexcept;

/// The key's chord on `pitch`'s step: `notes` notes stacked in thirds of `set` from `pitch` (3 a
/// triad, 4 a 7th, 5 a 9th), into `out`; how many.
std::size_t stackThirds(int pitch, PitchSet set, int notes, std::span<int> out) noexcept;

/// A chord's tones from its root: the root, then the rest of `set` by their distance above it.
std::size_t chordTones(int root, PitchSet set, std::span<int> out) noexcept;

/// Where the note played goes in a chord voiced from it.
enum class Place : std::uint8_t
{
    Top,    // it's the chord's top note
    Bottom, // its bottom note
    Smooth  // wherever leads with the least movement from the chord before
};

/// A chord (`root`, its pitch classes `set`) voiced around the note played: the note always
/// sounds; the chord's tones placed by `place` (Smooth: the inversion that moves least from
/// `previous`, else Top), then spread by `voicing` (Close, Open, Drop2, Spread; Smooth reads as
/// Close). Writes up to out.size() pitches, low to high, each in 0..127; returns how many.
std::size_t voiceAround(int played, int root, PitchSet set, Place place, Voicing voicing,
                        std::span<const int> previous, std::span<int> out) noexcept;

/// A chord (`root`, its pitch classes `set`) voiced near `center` with no note that must sound:
/// the inversion moving least from `previous` among those centred within a fifth of `center`
/// (with no chord before, the one centred nearest), spread by `voicing`. Low to high, into `out`;
/// how many.
std::size_t voiceNear(int root, PitchSet set, int center, Voicing voicing, std::span<const int> previous,
                      std::span<int> out) noexcept;

/// A chord on `root` with `notes` notes (3 a triad, 4 a 7th, 5 a 9th): its third, fifth, seventh
/// and ninth from `chord` where it has them (a sus chord's 4th or 2nd as its third), else from
/// `key`; tones it has beyond those are left out. Absolute pitch classes.
[[nodiscard]] PitchSet chordOfKind(int root, PitchSet chord, PitchSet key, int notes) noexcept;

/// The total distance between two voicings, each note to its nearest in the other.
[[nodiscard]] int movement(std::span<const int> from, std::span<const int> to) noexcept;

/// What the Pitch module does to a note before it fits it (devices/pitch/SPEC.md).
struct PitchMoves
{
    int semitones = 0, steps = 0;
    bool mirror = false;
    int axis = 60;
    double chance = 0.0; // 0..1
    int spread = 2;      // steps, 1..12
    int way = 2;         // 0 up, 1 down, 2 both
};

/// How it fits a note and keeps it in range.
struct PitchFit
{
    int fit = 0; // 0 off, 1 the key, 2 the chord
    Toward toward = Toward::Near;
    bool passing = true; // fitting to the chord, off the beat to the key
    int lowest = 0, highest = 127;
    int outside = 0; // 0 fold, 1 drop, 2 limit
};

/// A note mirrored around the axis, moved by semitones and steps of `key`, then by chance: `dice`
/// is the note's roll at its place (ADR 0062).
[[nodiscard]] int pitchMoved(int note, const PitchMoves& m, PitchSet key, std::uint64_t dice) noexcept;
/// A moved note fitted to `key` or `chord` (off the beat to the key, with passing notes; ties the
/// way `tie` says), then kept in range: the note, or −1 when the range leaves it out.
[[nodiscard]] int pitchFitted(int moved, const PitchFit& f, PitchSet key, PitchSet chord, bool onTheBeat,
                              int tie) noexcept;

/// A number from a roll and a place, the same every time it's asked (ADR 0062: randomness is
/// computed from the roll and where it is, never run).
[[nodiscard]] std::uint64_t mix(std::uint64_t roll, std::uint64_t a, std::uint64_t b = 0) noexcept;
/// The same as a number in [0, 1).
[[nodiscard]] double unit(std::uint64_t hashed) noexcept;
} // namespace fd::music
