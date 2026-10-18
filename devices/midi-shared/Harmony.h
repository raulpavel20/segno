#pragma once

#include <array>
#include <fd/device/Param.h>
#include <span>
#include <string_view>

namespace fd::devices::harmony
{
/// The HARMONY section's parameters, the same in every MIDI effect that follows harmony
/// (docs/design/midi-effects.md §5.1; devices/pitch/SPEC.md): what it follows, its own key and its
/// own chord. A module lists them after its own, in this order.
enum Offset : std::size_t
{
    Follow,
    Key,
    Root,
    Scale,
    Chord,
    Split,
    Count
};

inline constexpr std::array<std::string_view, 4> followChoices{"Song", "Track", "Played", "Own"};
inline constexpr std::array<std::string_view, 2> keyChoices{"Song", "Own"};
inline constexpr std::array<std::string_view, 12> rootChoices{
    "C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF",
    "G", "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
inline constexpr std::array<std::string_view, 8> chordChoices{"Major", "Minor", "7",   "Maj 7",
                                                              "Min 7", "Sus 4", "Dim", "Aug"};
/// The scale catalogue's names (libs/music), in its order.
[[nodiscard]] std::span<const std::string_view> scaleChoices();

/// The six parameters, for a module's table.
[[nodiscard]] std::array<ParamSpec, Count> params();
} // namespace fd::devices::harmony
