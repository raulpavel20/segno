#pragma once

#include "fd/music/Key.h"

#include <array>
#include <optional>
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
/// The same with more weight from elsewhere (audio clips with a key, docs/design/audio-editor.md
/// §8.2), added to the notes' for C … B. With any, too few notes doesn't stop it.
[[nodiscard]] std::vector<KeyCandidate> detectKeys(std::span<const DetectNote> notes,
                                                   const std::array<double, 12>& more, int count = 3);

/// How much each pitch class belongs to a key (C … B), summing to 1: a major or minor key's
/// Krumhansl–Kessler profile; another scale's notes alike, its root twice as much.
[[nodiscard]] std::array<double, 12> keyProfile(const Key& key);

/// The major and minor keys that fit a pitch-class profile best (how much of each of C … B
/// sounds: audio's, docs/design/audio-editor.md §8.2), best first, at most `count`, matched as
/// notes are. None when nothing sounds.
[[nodiscard]] std::vector<KeyCandidate> keysFromProfile(const std::array<double, 12>& weights, int count = 3);

/// How clearly a profile has a key: the best fit must reach `kClearKeyFit`, and every other key
/// but its relative (whose notes are the same) fit at least `kClearKeyMargin` worse. A single note
/// held, or drums, have none: A major and A minor fit a held A alike.
inline constexpr double kClearKeyFit = 0.6, kClearKeyMargin = 0.08;
[[nodiscard]] std::optional<KeyCandidate> clearKey(const std::array<double, 12>& weights);

/// A major key's relative minor, a minor key's relative major (C major and A minor); none for any
/// other scale.
[[nodiscard]] std::optional<Key> relativeKey(const Key& key);
} // namespace fd::music
