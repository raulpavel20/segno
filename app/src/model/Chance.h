#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <vector>

namespace fd::model
{
/// Chance (docs/design/piano-roll.md §10, ADR 0058): what a note does each time it comes round.
/// Its fate is decided from the clip's seed, the clip, the note and the lap, never from a running
/// random state, so playback, a bounce and an export always agree, and Re-roll (a new seed) is
/// the only thing that changes it.

/// What a note does on one lap.
struct NoteFate
{
    bool plays = true;
    float velocity = 0.0f; // 0..1 in MIDI steps: the note's, moved by its spread
};

/// A note's fate. `place` is its start in the clip's content (in its loop, for a looped clip), in
/// beats: the same lap after lap. `lap` counts the clip's loops from 0 (0 when it doesn't loop).
/// A note at full chance always plays; without spread its velocity is its own.
[[nodiscard]] NoteFate noteFate(const Note& note, double place, int lap, std::uint32_t seed,
                                std::uint64_t clip) noexcept;

/// Whether any note is left to chance: a chance under full, or a velocity spread.
[[nodiscard]] bool usesChance(const std::vector<Note>& notes) noexcept;
} // namespace fd::model
