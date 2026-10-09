#pragma once

#include "fd/dsp/Filters.h"

#include <array>
#include <vector>

namespace fd::dsp
{
// Loudness and true peak (ITU-R BS.1770-4, EBU R 128): the Limiter's meters, and the
// measurements of rendered audio (exports, the MCP render tool). Part of the dynamics core
// (M2 step A1). process() is real-time safe after prepare().

/// The K-weighting filter for `sampleRate`: the pre-filter (a high shelf, +4 dB) and the RLB
/// high-pass, as two biquads. At 48 kHz they are the standard's coefficients.
[[nodiscard]] std::array<BiquadCoeffs, 2> kWeighting(double sampleRate) noexcept;

/// Momentary (400 ms), short-term (3 s) and integrated (gated) loudness, in LUFS, of up to
/// two channels (each weighted 1, as left and right are). Values are -inf before there is
/// enough audio, or for silence.
class LoudnessMeter
{
public:
    static constexpr int maxChannels = 2;

    /// May allocate. Resets.
    void prepare(double sampleRate, int numChannels);
    /// Clears every value (the integrated loudness too).
    void reset() noexcept;
    void process(const float* const* channels, int numSamples) noexcept;

    [[nodiscard]] float momentary() const noexcept;
    [[nodiscard]] float shortTerm() const noexcept;
    /// Over everything since reset(), with the absolute (-70 LUFS) and relative (-10 LU) gates.
    [[nodiscard]] float integrated() const noexcept;
    /// The loudest momentary and short-term values since reset().
    [[nodiscard]] float maxMomentary() const noexcept { return maxMomentary_; }
    [[nodiscard]] float maxShortTerm() const noexcept { return maxShortTerm_; }

private:
    static constexpr int blocksShortTerm = 30; // 100 ms steps
    static constexpr int blocksMomentary = 4;
    static constexpr double histogramFloor = -70.0, histogramStep = 0.01;
    static constexpr int histogramBins = 8000; // -70 .. +10 LUFS

    void finishStep() noexcept;
    [[nodiscard]] double meanOfLast(int steps) const noexcept;

    int numChannels_ = 2;
    std::array<std::array<Biquad, 2>, maxChannels> filters_{};
    int stepLength_ = 4800, stepPos_ = 0;
    double stepSum_ = 0.0;
    std::array<double, blocksShortTerm> steps_{}; // mean square of each 100 ms step
    int stepsDone_ = 0, stepWrite_ = 0;
    float maxMomentary_ = 0.0f, maxShortTerm_ = 0.0f;
    std::vector<double> binEnergy_; // gated 400 ms blocks: summed mean squares per loudness bin
    std::vector<int> binCount_;
};

/// True peak: the largest absolute value of a signal oversampled 4x (polyphase FIR
/// interpolation, linear phase), so peaks between samples count too.
class TruePeakDetector
{
public:
    static constexpr int factor = 4, tapsPerPhase = 16, maxChannels = 8;

    /// May allocate. Resets.
    void prepare(int numChannels);
    void reset() noexcept;
    /// The largest of the four interpolated values ending at this sample, `latencySamples()`
    /// behind the input.
    float processSample(int channel, float x) noexcept;
    /// Feeds a block; peak() keeps the largest value since reset().
    void process(const float* const* channels, int numChannels, int numSamples) noexcept;
    [[nodiscard]] float peak() const noexcept { return peak_; }
    /// The interpolator's delay, in whole input samples (rounded up).
    [[nodiscard]] static constexpr int latencySamples() noexcept { return (factor * tapsPerPhase / 2 + factor - 1) / factor; }

private:
    std::vector<std::array<float, tapsPerPhase>> history_;
    std::vector<int> pos_;
    float peak_ = 0.0f;
};
} // namespace fd::dsp
