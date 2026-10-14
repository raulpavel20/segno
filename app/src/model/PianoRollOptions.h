#pragma once

#include "model/NoteTools.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace fd::model
{
/// What a double-click in the piano roll draws (docs/design/piano-roll.md §7.2, the Draw choice).
enum class DrawMode
{
    Note,
    Chord,     // the key's chord on the clicked note
    ChordUnder // the key's chord with the clicked note on top
};

/// What the chord lane writes for each chord (§7.1, Display): its name, its step in the key, or both.
enum class ChordLabels
{
    Both,
    Names,
    Steps
};

/// What the clip view's bottom lane shows (§11): a level of each note, or a controller's lane.
struct BottomLane
{
    enum class Kind
    {
        Velocity,
        Chance,
        Release,
        Controller
    };
    Kind kind = Kind::Velocity;
    int controller = controllers::modWheel; // for Kind::Controller (model/Controllers.h)

    /// The note level it edits (none for a controller).
    [[nodiscard]] std::optional<NoteLevel> level() const noexcept;
    friend bool operator==(const BottomLane&, const BottomLane&) = default;
};

/// "Velocity", "Chance", "Release velocity", or the controller's ("Pitch bend").
[[nodiscard]] std::string bottomLaneName(const BottomLane& lane);
/// Short enough for the lane's head (a LaneChoice beside it): "VELOCITY", "RELEASE", "BEND".
[[nodiscard]] std::string bottomLaneLabel(const BottomLane& lane);

/// The clip view's options (piano-roll.md §4): view state, kept with the project.
struct PianoRollOptions
{
    /// In key (§6, §21): on for every clip while the song has a key, except these, turned to the
    /// whole roll (a drum part stays whole).
    std::set<std::uint64_t> wholeRoll;
    bool others = true; // Others (§8): the other tracks' notes, faint
    std::set<std::uint64_t> othersHidden; // tracks Others leaves out, chosen one by one (§8)
    BottomLane lane;                      // the bottom lane (§11)
    DrawMode draw = DrawMode::Note;
    ChordLabels chordLabels = ChordLabels::Both;
    ChordStyle chord;          // the Chord popover's Kind, Voicing and Inversion (§7.3)
    QuantiseSettings quantise; // Quantise…'s last settings (§9.5)

    friend bool operator==(const PianoRollOptions&, const PianoRollOptions&) = default;
};

/// The options as view state, and back; missing or unknown values keep the defaults.
[[nodiscard]] nlohmann::json pianoRollOptionsToJson(const PianoRollOptions& options);
[[nodiscard]] PianoRollOptions pianoRollOptionsFromJson(const nlohmann::json& json);

/// The words kinds, voicings and inversions are written with in view state and by assistants
/// ("seventh", "drop2", "first"), in the enums' order; and back.
[[nodiscard]] std::span<const std::string_view> chordShapeWords() noexcept;
[[nodiscard]] std::span<const std::string_view> voicingWords() noexcept;
[[nodiscard]] std::span<const std::string_view> inversionWords() noexcept;
[[nodiscard]] std::optional<music::ChordShape> chordShapeFromWord(std::string_view word) noexcept;
[[nodiscard]] std::optional<music::Voicing> voicingFromWord(std::string_view word) noexcept;
[[nodiscard]] std::optional<music::Inversion> inversionFromWord(std::string_view word) noexcept;

/// The names the header and menus show: "Draw chord".
[[nodiscard]] const char* drawModeName(DrawMode mode) noexcept;
[[nodiscard]] const char* chordLabelsName(ChordLabels labels) noexcept;
/// The Chord popover's names for a kind, a voicing and an inversion ("7th", "Drop 2", "1st").
[[nodiscard]] const char* chordShapeName(music::ChordShape shape) noexcept;
[[nodiscard]] const char* voicingName(music::Voicing voicing) noexcept;
[[nodiscard]] const char* inversionName(music::Inversion inversion) noexcept;
} // namespace fd::model
