#include "fd/music/Scales.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <initializer_list>

namespace fd::music
{
namespace
{
constexpr PitchSet set(std::initializer_list<int> intervals)
{
    PitchSet s = 0;
    for (int i : intervals)
        s = static_cast<PitchSet>(s | (1u << i));
    return s;
}

constexpr std::string_view common = "Common", modes = "Modes", colours = "More colours", symmetric = "Symmetric",
                           japanese = "Japanese and Indonesian";

constexpr std::array<std::string_view, 5> groups{common, modes, colours, symmetric, japanese};

// docs/design/key-and-scale.md §5. The IDs are permanent (docs/ids.md): never rename or reuse one.
// signatureMode: 0 major-like, 9 minor-like, else the mode of the major scale it's written as.
constexpr std::array<Scale, 36> catalogue{{
    {"major", "Major", "maj", common, set({0, 2, 4, 5, 7, 9, 11}), 0},
    {"minor", "Minor", "min", common, set({0, 2, 3, 5, 7, 8, 10}), 9},
    {"harmonic-minor", "Harmonic minor", "harm min", common, set({0, 2, 3, 5, 7, 8, 11}), 9},
    {"melodic-minor", "Melodic minor", "mel min", common, set({0, 2, 3, 5, 7, 9, 11}), 9},
    {"major-pentatonic", "Major pentatonic", "maj pent", common, set({0, 2, 4, 7, 9}), 0},
    {"minor-pentatonic", "Minor pentatonic", "min pent", common, set({0, 3, 5, 7, 10}), 9},
    {"blues", "Blues", "blues", common, set({0, 3, 5, 6, 7, 10}), 9},
    {"major-blues", "Major blues", "maj blues", common, set({0, 2, 3, 4, 7, 9}), 0},

    {"dorian", "Dorian", "dor", modes, set({0, 2, 3, 5, 7, 9, 10}), 2, true},
    {"phrygian", "Phrygian", "phr", modes, set({0, 1, 3, 5, 7, 8, 10}), 4, true},
    {"lydian", "Lydian", "lyd", modes, set({0, 2, 4, 6, 7, 9, 11}), 5, true},
    {"mixolydian", "Mixolydian", "mix", modes, set({0, 2, 4, 5, 7, 9, 10}), 7, true},
    {"locrian", "Locrian", "loc", modes, set({0, 1, 3, 5, 6, 8, 10}), 11, true},

    {"harmonic-major", "Harmonic major", "harm maj", colours, set({0, 2, 4, 5, 7, 8, 11}), 0},
    {"dorian-sharp-4", "Dorian ♯4", "dor ♯4", colours, set({0, 2, 3, 6, 7, 9, 10}), 2, true},
    {"phrygian-dominant", "Phrygian dominant", "phr dom", colours, set({0, 1, 4, 5, 7, 8, 10}), 4, true},
    {"lydian-augmented", "Lydian augmented", "lyd aug", colours, set({0, 2, 4, 6, 8, 9, 11}), 5, true},
    {"lydian-dominant", "Lydian dominant", "lyd dom", colours, set({0, 2, 4, 6, 7, 9, 10}), 7, true},
    {"super-locrian", "Super Locrian", "super loc", colours, set({0, 1, 3, 4, 6, 8, 10}), 11, true, "Altered"},
    {"hungarian-minor", "Hungarian minor", "hung min", colours, set({0, 2, 3, 6, 7, 8, 11}), 9, true},
    {"double-harmonic", "Double harmonic", "dbl harm", colours, set({0, 1, 4, 5, 7, 8, 11}), 0, false, "Bhairav"},
    {"spanish-8-tone", "Spanish 8-tone", "span 8", colours, set({0, 1, 3, 4, 5, 6, 8, 10}), 4, true},

    {"whole-tone", "Whole tone", "whole", symmetric, set({0, 2, 4, 6, 8, 10}), 0},
    {"diminished-half-whole", "Diminished half-whole", "dim h-w", symmetric, set({0, 1, 3, 4, 6, 7, 9, 10}), 0},
    {"diminished-whole-half", "Diminished whole-half", "dim w-h", symmetric, set({0, 2, 3, 5, 6, 8, 9, 11}), 9},
    {"messiaen-3", "Messiaen 3", "mess 3", symmetric, set({0, 2, 3, 4, 6, 7, 8, 10, 11}), 0, true},
    {"messiaen-4", "Messiaen 4", "mess 4", symmetric, set({0, 1, 2, 5, 6, 7, 8, 11}), 0, true},
    {"messiaen-5", "Messiaen 5", "mess 5", symmetric, set({0, 1, 5, 6, 7, 11}), 0, true},
    {"messiaen-6", "Messiaen 6", "mess 6", symmetric, set({0, 2, 4, 5, 6, 8, 10, 11}), 0, true},
    {"messiaen-7", "Messiaen 7", "mess 7", symmetric, set({0, 1, 2, 3, 5, 6, 7, 8, 9, 11}), 9, true},

    {"hirajoshi", "Hirajoshi", "hira", japanese, set({0, 2, 3, 7, 8}), 9, true},
    {"in-sen", "In-sen", "in-sen", japanese, set({0, 1, 5, 7, 10}), 4, true},
    {"iwato", "Iwato", "iwato", japanese, set({0, 1, 5, 6, 10}), 11, true},
    {"kumoi", "Kumoi", "kumoi", japanese, set({0, 2, 3, 7, 9}), 2, true},
    {"pelog-selisir", "Pelog selisir", "pel sel", japanese, set({0, 1, 3, 7, 8}), 4, true},
    {"pelog-tembung", "Pelog tembung", "pel tem", japanese, set({0, 1, 5, 7, 8}), 4, true},
}};

bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}
} // namespace

std::span<const Scale> scales() noexcept { return catalogue; }

std::span<const std::string_view> scaleGroups() noexcept { return groups; }

const Scale* findScale(std::string_view idOrName) noexcept
{
    for (const auto& s : catalogue)
        if (s.id == idOrName)
            return &s;
    for (const auto& s : catalogue)
        if (equalsIgnoringCase(s.name, idOrName) || (!s.alsoCalled.empty() && equalsIgnoringCase(s.alsoCalled, idOrName))
            || equalsIgnoringCase(s.id, idOrName))
            return &s;
    return nullptr;
}

int noteCount(PitchSet set) noexcept { return std::popcount(static_cast<unsigned>(set & 0xfff)); }
} // namespace fd::music
