#pragma once

#include <cstdint>

namespace fd::music
{
/// The orders an arpeggio plays its notes in (devices/arpeggiator/SPEC.md, `style`).
enum class ArpStyle : std::uint8_t
{
    Up,
    Down,
    UpDown,
    DownUp,
    Converge,
    Diverge,
    Played,
    Chord,
    Random,
    Shuffled
};

/// Which of `count` notes (sorted low to high; as played, for Played) an arpeggio plays at its
/// step `n` (from 0): an index, or −1 for all of them (Chord). Random chooses each step from the
/// roll; Shuffled keeps one order, shuffled by the roll, for as long as the notes stay the same.
/// Pure and allocation free (a hash of the roll and the step, ADR 0062), so a render, a jump and
/// playing through agree.
[[nodiscard]] int arpIndex(ArpStyle style, int count, std::int64_t n, std::uint64_t roll) noexcept;
} // namespace fd::music
