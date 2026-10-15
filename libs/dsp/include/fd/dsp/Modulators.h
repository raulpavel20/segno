#pragma once

#include "fd/dsp/Wavetable.h" // CurveTable

#include <cstdint>
#include <span>
#include <string_view>

// The modulators' signals (docs/design/modulators.md §12): pure functions of where a modulator is
// and its settings, so the engine plays them exact to the time a parameter asks for (§22) and the
// panels draw them with the same functions. Values are -1 ... +1.
namespace fd::dsp::mod
{
/// The note values Sync offers, as the Wavetable's: Off, then 1/64 to 8 bars, with dotted and
/// triplet values, from the fastest to the slowest.
inline constexpr std::string_view syncValues[] = {
    "Off",  "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T",  "1/16D",  "1/8",    "1/4T",
    "1/8D", "1/4",  "1/2T",  "1/4D", "1/2",   "1/2D", "1 bar", "2 bars", "4 bars", "8 bars"};
inline constexpr int numSyncValues = 20;
/// Note value `index`'s length in beats (quarter notes), a bar being `beatsPerBar`; 0 for Off.
[[nodiscard]] double syncBeats(int index, double beatsPerBar) noexcept;

/// A value from -1 to 1 that depends only on its three inputs (SplitMix64, as a note's chance):
/// a roll's dice. Nothing random runs anywhere, so a roll plays the same every time.
[[nodiscard]] float dice(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept;

enum class Shape : std::uint8_t
{
    Sine,     // 0, rising
    Triangle, // 0, up to 1 at a quarter, down to -1 at three quarters
    SawUp,    // -1 to 1
    SawDown,  // 1 to -1
    Square,   // 1 for the first half, -1 for the second
    Drawn     // a drawn curve (the Wavetable's shape editor)
};
inline constexpr std::string_view shapeNames[] = {"Sine",     "Triangle", "Saw Up",
                                                  "Saw Down", "Square",   "Drawn"};

/// Where a cycle's own phase is under Slant (-1 ... +1): the cycle's middle moves from half way to
/// (1 + slant) / 2, so it rises quickly toward -1 and falls quickly toward +1. 0 leaves it.
[[nodiscard]] double slanted(double phase, double slant) noexcept;

/// The LFO at `phase` (cycles; only the fraction counts): the shape (Drawn: `drawn`, 0..1 played
/// as -1 ... +1; a sine without one), slanted, its corners rounded by `smooth` (0..1).
[[nodiscard]] float lfo(Shape shape, double phase, double slant, double smooth,
                        const CurveTable* drawn = nullptr) noexcept;

/// With Per Target, copy `copy` of `copies` starts this far into a cycle: spread evenly.
[[nodiscard]] double spread(int copy, int copies) noexcept;

/// How Random moves (§12.2).
struct Wander
{
    double smooth = 1.0; // 0: jumps to each value (sample and hold); 1: glides the whole step
    double slant = 0.0;  // -1: rises at once; +1: falls at once
    double cyclic = 0.0; // 0: each value anywhere; 1: up and down in turn
    double jitter = 0.0; // 0: steps evenly; 1: each step's start moved by up to 45 % of a step
};

/// Random at `x` (steps since playback started; a new value each) for a roll's `seed` and a copy
/// (Per Target): each step's value from the dice, moved to as Wander says.
[[nodiscard]] float random(std::uint64_t seed, std::uint64_t copy, double x, const Wander& wander) noexcept;

/// Which step `x` falls in, its starts moved by `jitter` (0..1) from the dice: steps stay in
/// order, each at least a tenth of a step long. Writes where in the step `x` is (0..1).
[[nodiscard]] std::int64_t stepAt(std::uint64_t seed, std::uint64_t copy, double x, double jitter,
                                  double& within) noexcept;

/// Whether step `n` (since playback started) plays for a copy, at `chance` (0..1).
[[nodiscard]] bool stepPlays(std::uint64_t seed, std::uint64_t copy, std::int64_t n, double chance) noexcept;

/// Steps at `x` (steps since the pattern started; it repeats): each step's value (-1 ... +1),
/// glided into over `smooth` (0..1) of a step; a step whose chance doesn't come up sits at 0.
[[nodiscard]] float steps(std::span<const float> values, std::span<const float> chances, std::uint64_t seed,
                          std::uint64_t copy, double x, double smooth, double jitter) noexcept;
} // namespace fd::dsp::mod
