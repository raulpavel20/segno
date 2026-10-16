#pragma once

#include <array>
#include <span>

namespace fd::dsp
{
// How much of each pitch class sounds in audio (the clip's key, Segno's
// docs/design/audio-editor.md §8.2). Offline analysis, never on the audio thread: the spectral
// peaks of long frames, each counted at its pitch to a tenth of a semitone, so the audio's tuning
// can be read before they're folded into twelve pitch classes.

/// A pitch-class profile and how pitched the audio is.
struct PitchClasses
{
    /// C = 0 … B = 11: each one's share of the pitched energy, summing to 1 (all 0 when nothing
    /// pitched sounds).
    std::array<double, 12> weights{};
    /// How far the audio's semitones lie from A = 440 Hz's, in cents (−40 … +50).
    double tuningCents = 0.0;
    /// 0 … 1: how much of its peaks' energy lies within 15 cents of a semitone. Notes and chords
    /// score 0.8 and over; noise and drums about 0.3, what any spread of pitches gives.
    double tonal = 0.0;
};

/// Counts the pitches in a mono signal, stretch by stretch (a file streamed), and gives the
/// profile of all it's been given.
class PitchClassCounter
{
public:
    /// Adds a stretch of a mono signal: its spectral peaks between 50 Hz and 5 kHz, frame by frame
    /// (a third of a second each, half overlapping), weighted by their level.
    void add(std::span<const float> mono, double sampleRate);
    [[nodiscard]] PitchClasses result() const;

    /// The pitch histogram: a tenth of a semitone a bin, from C up (bin 90 is A at 440 Hz's tuning).
    static constexpr int kBins = 120;

private:
    std::array<double, kBins> bins_{};
};

/// The profile of a whole mono signal.
[[nodiscard]] PitchClasses pitchClasses(std::span<const float> mono, double sampleRate);
} // namespace fd::dsp
