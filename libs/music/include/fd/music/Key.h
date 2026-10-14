#pragma once

#include "fd/music/Scales.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fd::music
{
/// A key: a root and a scale of the catalogue (docs/design/key-and-scale.md §4, ADR 0057).
/// "No key" is the absence of one (std::optional<Key>).
struct Key
{
    int root = 0;                // pitch class: 0 = C … 11 = B
    std::string scale = "major"; // a scale ID (docs/ids.md)

    friend bool operator==(const Key&, const Key&) = default;
};

/// How accidentals are written: the music glyphs for people (♯, ♭), or ASCII for assistants and
/// file names (#, b).
enum class Accidentals
{
    Glyphs,
    Ascii
};

/// The key's scale, or null for an ID this build doesn't know.
[[nodiscard]] const Scale* scaleOf(const Key& key) noexcept;

/// The key's notes as pitch classes; empty for an unknown scale.
[[nodiscard]] PitchSet notes(const Key& key) noexcept;

/// Whether a MIDI pitch (or a pitch class) is one of the key's notes.
[[nodiscard]] bool contains(const Key& key, int pitch) noexcept;

/// A pitch class's name in a key, spelled as written music does (§4): F major has B♭, E major
/// D♯, F♯ major E♯; never a double sharp or flat. Without a key, sharps.
[[nodiscard]] std::string pitchClassName(const Key* key, int pitchClass,
                                         Accidentals style = Accidentals::Glyphs);

/// A MIDI pitch's name with its octave, C3 = 60 (docs/design/daw-shell.md): "B♭3". The octave
/// follows the letter, so pitch 59 in E♭ minor is C♭3.
[[nodiscard]] std::string noteName(const Key* key, int pitch, Accidentals style = Accidentals::Glyphs);

/// The root's name for a scale ("D♭" for major, "C♯" for minor).
[[nodiscard]] std::string rootName(const Key& key, Accidentals style = Accidentals::Glyphs);

/// "A minor", "F♯ Phrygian dominant"; the short form "A min". An unknown scale gives the root
/// alone.
[[nodiscard]] std::string keyName(const Key& key, Accidentals style = Accidentals::Glyphs);
[[nodiscard]] std::string keyShortName(const Key& key, Accidentals style = Accidentals::Glyphs);

/// The key's notes from the root up, spelled: {"A", "B", "C", "D", "E", "F", "G"}.
[[nodiscard]] std::vector<std::string> spelledNotes(const Key& key, Accidentals style = Accidentals::Glyphs);

/// Whether the key writes its accidentals as flats (its signature has flats).
[[nodiscard]] bool usesFlats(const Key& key) noexcept;

/// A pitch class from a note's name: "C", "F#", "Gb", "G♭", "e flat", "Bb" (any case). Null for
/// anything else.
[[nodiscard]] std::optional<int> parsePitchClass(std::string_view name) noexcept;
} // namespace fd::music
