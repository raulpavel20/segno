#pragma once

#include "fd/music/Key.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fd::music
{
/// A kind of chord: the notes it stacks above its root, and how it's written
/// (docs/design/piano-roll.md §7, ADR 0058).
struct ChordKind
{
    std::string_view id;     // "m7b5": ASCII, as assistants write it after a root
    std::string_view symbol; // "m7♭5": after the root, "Bm7♭5"; empty for a major triad
    std::string_view roman;  // after a Roman numeral, whose case gives the third: "ø7" in "viiø7"
    std::string_view name;   // "Half-diminished seventh"
    bool minor = false;      // a minor or diminished third: its Roman numeral is lower case
    std::array<std::int8_t, 6> tones{}; // semitones above the root, stacked from it
    int toneCount = 0;
};

/// Every kind, in the order a name prefers them when notes could be read two ways.
[[nodiscard]] std::span<const ChordKind> chordKinds() noexcept;

/// A kind's index by its ID or symbol ("m7b5", "m7♭5", "ø7" and the usual spellings: "min", "-",
/// "dim", "M7", "Δ7"); none for anything else.
[[nodiscard]] std::optional<int> findChordKind(std::string_view idOrSymbol) noexcept;

/// A chord: a root, a kind, notes added beyond the kind, and the note under it when it isn't the
/// root. It names a set of pitch classes, not a voicing.
struct Chord
{
    int root = 0;            // pitch class: 0 = C … 11 = B
    int kind = 0;            // an index in chordKinds()
    PitchSet tensions = 0;   // added notes, as semitones above the root (bit 1: the ♭9)
    std::optional<int> bass; // the pitch class in the bass when it isn't the root ("C/E")

    friend bool operator==(const Chord&, const Chord&) = default;
};

[[nodiscard]] const ChordKind& kindOf(const Chord& chord) noexcept;

/// The chord's pitch classes, its bass included.
[[nodiscard]] PitchSet pitchClasses(const Chord& chord) noexcept;

/// The chord in root position, as semitones above its root: the kind's tones, then its tensions
/// above them. The bass isn't in it.
[[nodiscard]] std::vector<int> stackedTones(const Chord& chord);

/// The chord that pitches sounding together make (§7.1): three or more pitch classes, read as a
/// root and a kind, with tensions, and a bass when the lowest pitch isn't the root. None for fewer
/// pitch classes, or for ones no kind explains.
[[nodiscard]] std::optional<Chord> nameChord(std::span<const int> pitches);

/// How a chord is written (§7.1): "Am7", "C/E", "G7(♭9)", its notes spelled for the key (a
/// borrowed ♭VII in C major is B♭, not A♯); without a key, as in C major.
[[nodiscard]] std::string chordName(const Chord& chord, const Key* key, Accidentals style = Accidentals::Glyphs);

/// The chord's step in the key, as a Roman numeral (§7.1): "vi7", "V", "♭VII", "viiø7". A
/// borrowed chord's numeral has its accidental. Empty for a key of an unknown scale.
[[nodiscard]] std::string romanNumeral(const Chord& chord, const Key& key, Accidentals style = Accidentals::Glyphs);

/// A chord from how it's written: a name ("Dm7", "C/E", "Bbmaj7", "F#m7b5"), or with a key, a
/// Roman numeral ("ii7", "bVII", "V7"). None for anything else.
[[nodiscard]] std::optional<Chord> parseChord(std::string_view text, const Key* key);

/// What the key's chords stack: the Chord popover's Kind (§7.3).
enum class ChordShape
{
    Triad,   // thirds of the scale: the key's triad
    Seventh, // and its seventh
    Ninth,   // and its ninth
    Sus2,    // a major second for the third
    Sus4,    // a perfect fourth for the third
    Add9     // the key's triad and a major ninth
};

/// The key's chord on a pitch class (§7.2): for the key's notes, the thirds of its scale (or, in
/// scales without seven notes, the chord its notes make); for others, the kind whose notes are
/// most in the key. Without a key, as in C major: a major or minor triad by the note's place in it.
[[nodiscard]] Chord keyChord(const Key* key, int pitchClass, ChordShape shape = ChordShape::Triad);

/// The key's chord on each note of its scale, from the root up (§7.3, In the key).
[[nodiscard]] std::vector<Chord> keyChords(const Key& key, ChordShape shape = ChordShape::Triad);

/// The key's chord with a pitch class in it that fits best (§7.2, Chord under): after a chord,
/// the most usual next one that has it; else the most usual one (I, IV, V, vi …). A pitch class
/// outside the key gets the chord on it.
[[nodiscard]] Chord harmonise(const Key* key, int pitchClass, ChordShape shape, const Chord* before);

/// A chord the Chord popover offers, and why (§7.3): suggestions are explainable.
struct ChordSuggestion
{
    Chord chord;
    std::string_view why; // "V to I: home"
};

/// The usual chords after `before` in the key, most usual first (§7.3, Next): the common moves
/// between steps, the deceptive and plagal ones, and a borrowed or secondary chord's resolution.
[[nodiscard]] std::vector<ChordSuggestion> nextChords(const Key& key, const Chord& before,
                                                      ChordShape shape = ChordShape::Triad);

/// Chords from outside the key for colour (§7.3, Borrowed): in a major key, ♭VII, iv, ♭VI and ♭III
/// from the parallel minor; in a minor key, IV and V from the parallel major; and the dominant of
/// the fifth (V/V). None that the key already has.
[[nodiscard]] std::vector<ChordSuggestion> borrowedChords(const Key& key, ChordShape shape = ChordShape::Triad);

/// How a chord's notes are spread (§7.4).
enum class Voicing
{
    Smooth, // the inversion and octave that move least from the chord before
    Close,  // within the smallest span, from the bass
    Open,   // close, with its second note up an octave
    Drop2,  // close, with its second note from the top down an octave
    Drop3,  // close, with its third note from the top down an octave
    Spread  // open, with its bass down an octave
};

/// Which of the chord's tones is in the bass (Auto: root position, or what Smooth picks).
enum class Inversion
{
    Auto,
    Root,
    First,
    Second,
    Third
};

/// What a voicing is asked for: its kind, and where it goes.
struct VoiceRequest
{
    Voicing voicing = Voicing::Smooth;
    Inversion inversion = Inversion::Auto;
    std::span<const int> before;   // the chord before, which Smooth moves least from
    std::optional<int> including;  // a pitch it must have (the note clicked for Chord)
    std::optional<int> top;        // a pitch it must have on top (the note clicked for Chord under)
    double centre = 64.0;          // otherwise, the pitch its notes are centred nearest
    int count = 0;                 // how many notes: 0 for the chord's own, more doubles, fewer omits
};

/// The chord's pitches as asked, low to high, within MIDI's range. Empty when no voicing can
/// satisfy the request (a pitch it doesn't have).
[[nodiscard]] std::vector<int> voiceChord(const Chord& chord, const VoiceRequest& request);

/// How far one chord's notes move to another's: each voice to its counterpart in order when they
/// have as many, else each note to the nearest of the other's.
[[nodiscard]] double voiceLeadingDistance(std::span<const int> from, std::span<const int> to);
} // namespace fd::music
