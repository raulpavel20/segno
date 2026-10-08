#pragma once

#include <algorithm>
#include <cmath>

namespace fd::model
{
/// Musical time for the arrangement. M1 has one tempo and one time signature; tempo changes
/// later replace this type without touching its users (docs/design/daw-shell.md).
/// Beats are quarter notes; BPM counts quarter notes.
struct TempoMap
{
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;

    [[nodiscard]] double beatsForSeconds(double seconds) const noexcept { return seconds * bpm / 60.0; }
    [[nodiscard]] double secondsForBeats(double beats) const noexcept { return beats * 60.0 / bpm; }
    [[nodiscard]] double beatsPerBar() const noexcept { return numerator * 4.0 / denominator; }

    /// Position as 1-based bar, beat (in the signature's beat unit) and sixteenth.
    struct BarsBeats
    {
        int bar = 1, beat = 1, sixteenth = 1;
    };
    [[nodiscard]] BarsBeats barsBeatsForSeconds(double seconds) const noexcept
    {
        const double quarter = std::max(0.0, beatsForSeconds(seconds)) + 1e-9;
        const double perBar = beatsPerBar();
        const double unit = 4.0 / denominator; // quarter notes per signature beat
        const auto bar = static_cast<int>(std::floor(quarter / perBar));
        const double inBar = quarter - bar * perBar;
        const auto beat = static_cast<int>(std::floor(inBar / unit));
        const double inBeat = inBar - beat * unit;
        return {bar + 1, beat + 1, static_cast<int>(std::floor(inBeat / 0.25)) + 1};
    }

    friend bool operator==(const TempoMap&, const TempoMap&) = default;
};
} // namespace fd::model
