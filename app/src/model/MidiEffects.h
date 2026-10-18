#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fd::model
{
/// What a module along a track's chain does with notes, for where MIDI effects go
/// (docs/design/midi-effects.md §11).
enum class ChainRole : std::uint8_t
{
    Other,      // an audio effect, a modulator
    MidiEffect, // notes in, notes out
    Player      // plays notes: an instrument, a Drum Kit, a rack that starts with one
};

/// Where a module asked to go at `index` (−1: the end) along `chain` lands: a MIDI effect before
/// the first module that plays notes, however far after it it's dropped; anything else where it's
/// asked. `chain` as it'll be when the module goes in (without it, when it's moving).
[[nodiscard]] int placeInChain(std::span<const ChainRole> chain, int index, ChainRole role) noexcept;

/// Whether a track's notes make the song's harmony (midi-effects.md §5.1; Track ▸ Harmony): Auto
/// counts a track that plays chords somewhere, never a Drum Kit's; On counts whatever it plays (a
/// bass part gives the chords their bass); Off never.
enum class HarmonyUse : std::uint8_t
{
    Auto,
    On,
    Off
};
/// How often the song's chords may change (Song ▸ Chords change): a shorter chord joins the one
/// before it, as a musician hears a passing chord.
enum class ChordChange : std::uint8_t
{
    Bar,
    HalfBar,
    Beat,
    Any
};

/// The takes Keep as notes adds (midi-effects.md §9.1): the clip's notes before, and what was kept.
inline constexpr const char* kBeforeMidiEffectsName = "Before MIDI effects";
inline constexpr const char* kKeptNotesName = "Kept notes";

struct TrackInfo;
struct Selection;
/// The clips Keep as notes acts on, on `track` (§9.1): its MIDI clips selected; else those across
/// the time selection, if it's on the track; else every one.
[[nodiscard]] std::vector<std::uint64_t> clipsToKeep(const TrackInfo& track, const Selection& selection);

/// Their IDs, as projects and assistants write them ("auto", "half-bar"); nullopt for none.
[[nodiscard]] std::string_view idOf(HarmonyUse use) noexcept;
[[nodiscard]] std::optional<HarmonyUse> harmonyUseOf(std::string_view id) noexcept;
[[nodiscard]] std::string_view idOf(ChordChange change) noexcept;
[[nodiscard]] std::optional<ChordChange> chordChangeOf(std::string_view id) noexcept;
/// The shortest a chord lasts, in beats, in a bar of `numerator`/`denominator`.
[[nodiscard]] double shortestChord(ChordChange change, int numerator, int denominator) noexcept;
} // namespace fd::model
