#pragma once

#include <optional>
#include <span>
#include <vector>

namespace fd::dsp
{
// Where sounds start in audio (the audio editor's transients, tempo and mode; Segno's
// docs/design/audio-editor.md §7.3, §7.4, §7.8). Offline analysis, never on the audio thread: one
// envelope of how strongly each moment starts something (spectral flux), shared by the onsets,
// the tempo and how percussive a sound is.

/// How strongly each moment starts something: the half-wave rectified spectral flux of log
/// magnitudes, frame k centred `hop` × k seconds into the signal.
struct OnsetEnvelope
{
    std::vector<float> flux;
    double hop = 0.0;
};
[[nodiscard]] OnsetEnvelope onsetEnvelope(std::span<const float> mono, double sampleRate);

/// Something that may start a sound: when (seconds from the signal's start, at its attack's start,
/// to the millisecond), and how far it stands above its surroundings (0 … 1 and over).
struct OnsetCandidate
{
    double time = 0.0;
    float strength = 0.0f;
    friend bool operator==(const OnsetCandidate&, const OnsetCandidate&) = default;
};
/// Every moment of a mono signal that some sensitivity counts as an onset, in order: found once,
/// so a sensitivity dragged picks among them at once.
[[nodiscard]] std::vector<OnsetCandidate> onsetCandidates(std::span<const float> mono, double sampleRate);
/// The candidates a sensitivity (0 … 1) counts: more as it rises; 0.5 finds what a listener would
/// call a hit or a note. Seconds, in order.
[[nodiscard]] std::vector<double> onsetsAt(const std::vector<OnsetCandidate>& candidates, double sensitivity);
/// onsetsAt(onsetCandidates(mono, sampleRate), sensitivity).
[[nodiscard]] std::vector<double> findOnsets(std::span<const float> mono, double sampleRate,
                                             double sensitivity);

/// A tempo found in audio, and where its beats and bars fall.
struct TempoEstimate
{
    double bpm = 120.0;
    double firstBeat = 0.0;     // seconds: the first beat from the start
    double firstDownbeat = 0.0; // seconds: the first beat of a bar (4/4)
    double confidence = 0.0;    // 0 … 1: how much better its beats line up than other tempos'
};
/// The tempo whose beats line up best with what starts in a mono signal, between `minBpm` and
/// `maxBpm`. A short one (under 32 s, a loop) prefers a tempo that makes it a whole number of
/// 4/4 bars. Nullopt when nothing in it is rhythmic.
[[nodiscard]] std::optional<TempoEstimate> estimateTempo(std::span<const float> mono, double sampleRate,
                                                         double minBpm = 60.0, double maxBpm = 180.0);

/// How percussive a sound is, 0 … 1: how much of its energy comes in short broadband bursts
/// rather than held tones (harmonic–percussive separation by median filtering). Above 0.5 it's
/// drums or percussion.
[[nodiscard]] double percussiveness(std::span<const float> mono, double sampleRate);
} // namespace fd::dsp
