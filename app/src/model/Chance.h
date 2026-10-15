#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <vector>

namespace fd::model
{
/// Chance (docs/design/piano-roll.md §10, §26, ADR 0058, ADR 0062): what a note does each time it
/// comes round. Its fate is decided from a seed, the clip, the note and the lap, never from a
/// running random state. The seed mixes the clip's own (Re-roll) with the pass's roll and the
/// transport loop's lap (passSeed): it rolls again each time the song plays and each time the loop
/// comes round, and a clip that keeps its roll plays the same in playback, a bounce and an export.

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

/// The seed a pass decides a clip's notes with: its own seed (Re-roll), the pass's roll (a new one
/// each time playback starts, or the kept one) and the transport loop's lap within the pass.
[[nodiscard]] std::uint32_t passSeed(std::uint32_t seed, std::uint64_t roll, int lap) noexcept;

/// Whether any note is left to chance: a chance under full, or a velocity spread.
[[nodiscard]] bool usesChance(const std::vector<Note>& notes) noexcept;
} // namespace fd::model
