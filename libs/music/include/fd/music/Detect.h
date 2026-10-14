#pragma once

#include "fd/music/Key.h"

#include <span>
#include <vector>

namespace fd::music
{
/// A note for key detection: its MIDI pitch and how long it lasts (any unit, the same for all).
struct DetectNote
{
    int pitch = 60;
    double length = 1.0;
};

/// A key that fits some notes, and how well: the correlation of the notes' pitch classes,
/// weighted by length, with the key's profile (0 to 1; higher fits better).
struct KeyCandidate
{
    Key key;
    double fit = 0.0;
};

/// The fewest notes, and different pitch classes, that detection judges.
inline constexpr int detectMinNotes = 8;
inline constexpr int detectMinPitchClasses = 5;

/// The major and minor keys that fit the notes best, best first, at most `count`
/// (docs/design/key-and-scale.md §11): the Krumhansl–Schmuckler method, with the
/// Krumhansl–Kessler profiles. None when there are too few notes to judge, or none fits.
[[nodiscard]] std::vector<KeyCandidate> detectKeys(std::span<const DetectNote> notes, int count = 3);
} // namespace fd::music
