#pragma once

#include <vector>

namespace fd::dsp
{
// The dynamics core (M2 step A1): the parts the Compressor, Gate and Limiter share, and later
// Multiband Dynamics and Drum Bus. Levels and gains are in dB; process() functions are
// real-time safe (allocation happens in prepare()).

// ---- static curves (pure: the devices' displays draw the same functions) ---------------------

/// The output level (dB) of a downward compressor at `inputDb`: unity below the threshold,
/// `ratio`:1 above it (infinity is a limiter), with the standard quadratic soft knee
/// `kneeDb` wide centred on the threshold.
[[nodiscard]] float compressorCurve(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

/// The output level (dB) of a downward expander at `inputDb`: unity above the threshold,
/// falling `ratio` dB per dB below it, with a quadratic soft knee `kneeDb` wide. The reduction
/// stops at `rangeDb` (<= 0; -inf for none).
[[nodiscard]] float expanderCurve(float inputDb, float thresholdDb, float ratio, float kneeDb,
                                  float rangeDb) noexcept;

// ---- times --------------------------------------------------------------------------------

/// The per-sample factor of a one-pole smoother whose steps go from 10 % to 90 % in `ms`:
/// the suite's attack times.
[[nodiscard]] double attackCoefficient(double ms, double sampleRate) noexcept;
/// The per-sample factor of a one-pole smoother whose steps fall to one thousandth (-60 dB) of
/// what's left in `ms`: the suite's release times.
[[nodiscard]] double releaseCoefficient(double ms, double sampleRate) noexcept;

// ---- gain smoothing -------------------------------------------------------------------------

/// A gain in dB moving towards its target with one time going down and another going up
/// (factors from attackCoefficient and releaseCoefficient). Compressors and limiters: down is
/// the attack, up the release. Gates and expanders: up is the attack, down the release.
/// Targets below dsp::minusInfinityDb count as minusInfinityDb (silence).
class GainSmoother
{
public:
    void setCoefficients(double down, double up) noexcept;
    void reset(float gainDb = 0.0f) noexcept;
    float process(float targetDb) noexcept;
    [[nodiscard]] float value() const noexcept { return static_cast<float>(y_); }

private:
    double down_ = 0.0, up_ = 0.0, y_ = 0.0;
};

/// Program-dependent release (Auto release): the release time blends from fast to slow
/// according to how long the gain reduction has lasted. A short burst recovers with the fast
/// release; sustained reduction (about `memory` of it) with the slow one.
class AutoReleaseSmoother
{
public:
    /// Factors: the attack (gain going down), the fast and slow releases, and how quickly
    /// sustained reduction counts (an attack factor).
    void setCoefficients(double attack, double fastRelease, double slowRelease, double memory) noexcept;
    void reset(float gainDb = 0.0f) noexcept;
    float process(float targetDb) noexcept;
    [[nodiscard]] float value() const noexcept { return static_cast<float>(y_); }

private:
    double attack_ = 0.0, fast_ = 0.0, slow_ = 0.0, memory_ = 0.0;
    double y_ = 0.0, sustained_ = 0.0; // 0 = short bursts only .. 1 = reducing for a while
};

// ---- detection ------------------------------------------------------------------------------

/// A sample's level for dynamics, in dB: the rectified sample (Peak), or its mean square over a
/// window (RMS, a one-pole of `rmsWindowMs`, 10 ms by default).
class LevelDetector
{
public:
    enum class Mode
    {
        Peak,
        Rms
    };

    void prepare(double sampleRate, double rmsWindowMs = 10.0) noexcept;
    void setMode(Mode mode) noexcept { mode_ = mode; }
    void reset() noexcept { meanSquare_ = 0.0; }
    /// The level of the next sample (dsp::minusInfinityDb for silence).
    float processDb(float x) noexcept;

private:
    Mode mode_ = Mode::Peak;
    double rmsFactor_ = 0.0, meanSquare_ = 0.0;
};

/// Stereo link: each channel's level moves towards the loudest channel's by `amount` (0 =
/// independent, 1 = one level for all), in dB.
void linkLevels(float* levelsDb, int numChannels, float amount) noexcept;

// ---- lookahead ------------------------------------------------------------------------------

/// A whole-sample delay: the audio path of a lookahead detector.
class DelayLine
{
public:
    /// May allocate.
    void prepare(int maxDelaySamples);
    /// At most the prepared maximum. Resets.
    void setDelay(int samples) noexcept;
    [[nodiscard]] int delay() const noexcept { return delay_; }
    void reset() noexcept;
    float process(float x) noexcept;

private:
    std::vector<float> buffer_;
    int delay_ = 0, write_ = 0;
};

/// The smallest of the last `length` values, in constant amortised time: a lookahead limiter
/// holds the gain each peak needs for as long as the peak is still ahead.
class SlidingMinimum
{
public:
    /// May allocate.
    void prepare(int maxLength);
    /// 1 .. the prepared maximum. Resets.
    void setLength(int length) noexcept;
    void reset() noexcept;
    float process(float x) noexcept;

private:
    struct Entry
    {
        float value;
        long long time;
    };
    std::vector<Entry> ring_;
    int head_ = 0, count_ = 0, length_ = 1;
    long long time_ = 0;
};

/// The mean of the last `length` values: smoothed over the lookahead window, a held gain
/// reaches its target exactly when the peak arrives.
class MovingAverage
{
public:
    /// May allocate.
    void prepare(int maxLength);
    /// 1 .. the prepared maximum. Resets to `value`.
    void setLength(int length, float value = 0.0f) noexcept;
    void reset(float value = 0.0f) noexcept;
    float process(float x) noexcept;

private:
    std::vector<double> ring_;
    double sum_ = 0.0;
    int length_ = 1, pos_ = 0;
};
} // namespace fd::dsp
