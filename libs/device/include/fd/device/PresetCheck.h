#pragma once

#include "fd/device/Registry.h"
#include "fd/device/State.h"

#include <string>
#include <vector>

namespace fd
{
/// How an instrument's preset plays across the keyboard (docs/design/presets.md, "The quality
/// bar", ADR 0069): rendered at full velocity with the song playing at 120 bpm, a single note in
/// every octave and a four-note chord (a major triad and its octave) in five, each held long enough
/// for a slow pad to reach its peak. JUCE-free; `fd-presets` refuses to make a preset that fails,
/// and the conformance suite checks the factory sets with it.
struct KeyboardCheck
{
    struct Take
    {
        int key = 60;        // the note, or the chord's root (MIDI, C3 = 60)
        double peakDb = 0.0; // the loudest sample, dBFS
        double rmsDb = 0.0;  // while held, after the first quarter second
        std::string played;  // for the extremes: how it was played
    };
    std::vector<Take> notes, chords;
    /// The chord on C3 played at the extremes: every macro at 0, every macro at full (an instrument
    /// with macros: parameters "macro1" …), and with the mod wheel and pressure at full.
    std::vector<Take> extremes;

    /// The loudest sample of every take.
    [[nodiscard]] double peakDb() const noexcept;
    /// The single notes from `low` to `high` that sound (above -60 dB): their loudest level over
    /// their quietest. The range a preset is played in: the middle five octaves unless its recipe
    /// says (a bass's lower, a lead's higher).
    [[nodiscard]] double spreadDb(int low = 36, int high = 84) const noexcept;
    /// What fails the bar: any take louder than `ceilingDb`, or notes in the range further apart
    /// than `spreadLimitDb`. Empty when it passes.
    [[nodiscard]] std::vector<std::string> problems(double ceilingDb = -1.0, double spreadLimitDb = 12.0,
                                                    int low = 36, int high = 84) const;
};

/// Renders `state` on a new instance from `factory` (an instrument) across the keyboard.
[[nodiscard]] KeyboardCheck checkKeyboard(const DeviceFactory& factory, const DeviceState& state,
                                          double holdSeconds = 4.0);
} // namespace fd
