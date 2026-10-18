#pragma once

#include <cstdint>
#include <span>

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
} // namespace fd::model
