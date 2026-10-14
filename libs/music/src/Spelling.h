#pragma once

// Notes written as letters and accidentals: shared by Key.cpp and Chords.cpp, not installed.

#include "fd/music/Key.h"

#include <array>
#include <cstddef>
#include <string>

namespace fd::music::detail
{
inline constexpr std::array<int, 7> naturals{0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
inline constexpr std::array<char, 7> letters{'C', 'D', 'E', 'F', 'G', 'A', 'B'};
inline constexpr std::array<int, 7> majorSteps{0, 2, 4, 5, 7, 9, 11};

inline constexpr int mod12(int x) noexcept
{
    return ((x % 12) + 12) % 12;
}

/// A note written as a letter and an accidental (−2 … 2: double flat … double sharp).
struct Spelled
{
    int letter = 0;
    int accidental = 0;
};

/// The accidental that puts `letter` on pitch class `pc`, from −6 to 5.
inline int accidentalOn(int letter, int pc) noexcept
{
    const int a = mod12(pc - naturals[static_cast<std::size_t>(letter)]);
    return a > 6 ? a - 12 : a;
}

/// The note as text: "B♭", or "Bb" in ASCII.
std::string text(Spelled s, Accidentals style);

/// A pitch class as the key writes it (sharps without a key), as noteName does.
Spelled spell(const Key* key, int pitchClass);
} // namespace fd::music::detail
