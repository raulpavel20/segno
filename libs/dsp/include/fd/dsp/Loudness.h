#pragma once

#include "fd/dsp/Filters.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace fd::dsp
{
// Loudness and true peak (ITU-R BS.1770, EBU R 128, EBU Tech 3341 and 3342): the Limiter's
// meters, the Loudness Meter, and the measurements of rendered audio (exports, the MCP render
// tool). Part of the dynamics core (M2 step A1). process() is real-time safe after prepare().

/// The K-weighting filter for `sampleRate`: the pre-filter (a high shelf, +4 dB) and the RLB
/// high-pass, as two biquads. At 48 kHz they are the standard's coefficients.
[[nodiscard]] std::array<BiquadCoeffs, 2> kWeighting(double sampleRate) noexcept;

/// Loudness of a channel-summed, K-weighted mean square, in LUFS (BS.1770's -0.691 offset);
/// -inf for silence.
[[nodiscard]] double lufsOf(double meanSquare) noexcept;

/// Loudness values, counted by their mean squares in 0.01 LU bins from -70 to +10 LUFS (louder
/// ones in the top bin): what the gated measures need, at a cost that doesn't grow with the
/// length measured. Values under -70 LUFS (the absolute gate) aren't counted. A value can be
/// taken out again, so a measurement can replace part of itself.
class LoudnessHistogram
{
public:
    static constexpr double floor = -70.0, step = 0.01;
    static constexpr int bins = 8000;

    /// May allocate.
    void prepare();
    void clear() noexcept;
    void add(double meanSquare) noexcept;
    /// Takes out a value added before (the same mean square).
    void remove(double meanSquare) noexcept;
    [[nodiscard]] long long count() const noexcept { return total_; }

    /// BS.1770's gated loudness: the mean of the values no more than `relativeGate` LU below the
    /// mean of them all (-10 for integrated loudness). NaN with none.
    [[nodiscard]] float gatedLoudness(double relativeGate) const noexcept;
    /// EBU Tech 3342's loudness range: the 95th percentile of the values above the -20 LU relative
    /// gate minus their 10th percentile, in LU. NaN with none.
    [[nodiscard]] float range() const noexcept;
    /// The loudest value (to the bin's 0.01 LU). NaN with none.
    [[nodiscard]] float loudest() const noexcept;

private:
    [[nodiscard]] static int binOf(double loudness) noexcept;
    [[nodiscard]] static double centreOf(int bin) noexcept { return floor + (bin + 0.5) * step; }

    std::vector<double> energy_; // summed mean squares per bin
    std::vector<int> count_;
    long long total_ = 0;
};

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
    /// EBU Tech 3342's loudness range since reset(), in LU, from the short-term loudness every
    /// 100 ms. NaN before there is a short-term value above the gates.
    [[nodiscard]] float loudnessRange() const noexcept { return windows_.range(); }
    /// The loudest momentary and short-term values since reset().
    [[nodiscard]] float maxMomentary() const noexcept { return maxMomentary_; }
    [[nodiscard]] float maxShortTerm() const noexcept { return maxShortTerm_; }

private:
    static constexpr int blocksShortTerm = 30; // 100 ms steps
    static constexpr int blocksMomentary = 4;

    void finishStep() noexcept;
    [[nodiscard]] double meanOfLast(int steps) const noexcept;

    int numChannels_ = 2;
    std::array<std::array<Biquad, 2>, maxChannels> filters_{};
    int stepLength_ = 4800, stepPos_ = 0;
    double stepSum_ = 0.0;
    std::array<double, blocksShortTerm> steps_{}; // mean square of each 100 ms step
    int stepsDone_ = 0, stepWrite_ = 0;
    float maxMomentary_ = 0.0f, maxShortTerm_ = 0.0f;
    LoudnessHistogram blocks_;  // the 400 ms gating blocks, for integrated loudness
    LoudnessHistogram windows_; // the 3 s short-term windows every step, for loudness range
};

/// A stretch of audio's loudness kept 100 ms step by step: a song's, by its position, or
/// everything since a reset, in order (the Loudness Meter's Song and Running). Each step holds its
/// channel-summed, K-weighted mean square and its true peak. Writing a step again replaces it, and
/// every reading follows: integrated loudness (BS.1770, over the 400 ms blocks of four steps in a
/// row), loudness range (EBU Tech 3342, over the 3 s windows of thirty), the loudest momentary and
/// short-term values, and the true-peak maximum. Readings cost the same at any length.
///
/// One thread writes (the audio thread: set(), clear() and the readings); any thread may read the
/// steps (momentaryAt(), shortTermAt(), truePeakAt(), extent()) for a history display.
class LoudnessStore
{
public:
    static constexpr int stepsMomentary = 4, stepsShortTerm = 30;

    /// May allocate: room for `steps` steps, none heard.
    void prepare(int steps);
    [[nodiscard]] int capacity() const noexcept { return capacity_; }
    /// Forgets every step.
    void clear() noexcept;
    /// Step `index` (0 to capacity - 1): its mean square and its true peak (linear). Replaces what
    /// was there. A meter that slides its windows more finely than a step gives the loudest
    /// momentary and short-term values whose windows end in this step (LUFS), so the maximums don't
    /// miss a peak between steps (EBU Tech 3341 cases 11 and 14); NaN: none.
    void set(int index, double meanSquare, float truePeak, float loudestMomentary = NAN,
             float loudestShortTerm = NAN) noexcept;

    [[nodiscard]] bool heard(int index) const noexcept;
    [[nodiscard]] int heardCount() const noexcept { return heard_; }
    /// One past the furthest step heard (0 with none). Any thread.
    [[nodiscard]] int extent() const noexcept { return extent_.load(std::memory_order_relaxed); }

    /// LUFS (NaN without a reading).
    [[nodiscard]] float integrated() const noexcept { return blocks_.gatedLoudness(-10.0); }
    /// LU (NaN without a reading).
    [[nodiscard]] float range() const noexcept { return windows_.range(); }
    [[nodiscard]] float maxMomentary() const noexcept;
    [[nodiscard]] float maxShortTerm() const noexcept;
    /// Linear (0 with no step).
    [[nodiscard]] float truePeakMax() const noexcept { return peaks_.max(truePeak_.get(), extent()); }
    /// The finer maximums given with the step (NaN: none). Any thread.
    [[nodiscard]] float loudestMomentaryAt(int index) const noexcept;
    [[nodiscard]] float loudestShortTermAt(int index) const noexcept;

    /// The step's mean square (negative: not heard). Any thread.
    [[nodiscard]] float meanSquareAt(int index) const noexcept;
    /// The momentary and short-term loudness of the block and window ending with step `index`,
    /// in LUFS (NaN unless all of its steps were heard). Any thread.
    [[nodiscard]] float momentaryAt(int index) const noexcept
    {
        return loudnessEndingAt(index, stepsMomentary);
    }
    [[nodiscard]] float shortTermAt(int index) const noexcept
    {
        return loudnessEndingAt(index, stepsShortTerm);
    }
    /// The step's true peak, linear (0 if not heard). Any thread.
    [[nodiscard]] float truePeakAt(int index) const noexcept;

private:
    /// The mean square of the `length` steps ending at `end`, or a negative value unless all
    /// were heard.
    [[nodiscard]] double meanEndingAt(int end, int length) const noexcept;
    [[nodiscard]] float loudnessEndingAt(int end, int length) const noexcept;
    /// Takes out (or puts back) every block and window that step `index` is in.
    void account(int index, bool add) noexcept;

    /// The largest of one value per step: kept as each step is set, found again only when the step
    /// holding it got smaller.
    struct Largest
    {
        void reset(float none) noexcept { value = none, stale = false; }
        void update(float old, float now, bool replaced) noexcept;
        [[nodiscard]] float max(const std::atomic<float>* values, int extent) const noexcept;
        mutable float value = 0.0f;
        mutable bool stale = false;
    };

    int capacity_ = 0, heard_ = 0;
    std::unique_ptr<std::atomic<float>[]> meanSquare_; // -1: not heard
    std::unique_ptr<std::atomic<float>[]> truePeak_;
    std::unique_ptr<std::atomic<float>[]> loudestMomentary_, loudestShortTerm_; // NaN: none
    std::atomic<int> extent_{0};
    LoudnessHistogram blocks_, windows_;
    Largest peaks_, momentaryMax_, shortTermMax_;
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
