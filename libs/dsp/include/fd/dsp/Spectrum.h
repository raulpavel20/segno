#pragma once

#include "fd/dsp/Fft.h"

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace fd::dsp
{
/// The analysers' frequency axis (devices/spectrum/SPEC.md): 121 bands a twelfth of an octave
/// apart, from 20 Hz (band 0) to 20.48 kHz (band 120).
namespace spectrum
{
inline constexpr int numBands = 121;
inline constexpr int bandsPerOctave = 12;
inline constexpr double lowestHz = 20.0;
/// What a band with nothing in it reads.
inline constexpr float floorDb = -200.0f;

/// A band's centre frequency.
[[nodiscard]] double bandHz(int band) noexcept;
/// The band at `hz`, fractional.
[[nodiscard]] double bandAt(double hz) noexcept;
/// The Slope's tilt at `hz`: `slope` dB per octave, 0 at 1 kHz.
[[nodiscard]] float tiltDb(double hz, float slope) noexcept;
} // namespace spectrum

/// A spectrum in bands: a level in dB per band, without the Slope's tilt.
using SpectrumCurve = std::array<float, spectrum::numBands>;

/// Which signal of a stereo pair is analysed, in the order of the Spectrum's Channel parameter.
/// Both: the average of the left and right power spectra (a mono sum would hide what's out of
/// phase); Mid: (L + R) / 2; Side: (L − R) / 2.
enum class SpectrumChannel
{
    Both,
    Left,
    Right,
    Mid,
    Side
};
inline constexpr int numSpectrumChannels = 5;

/// A spectrum of a stereo signal: Hann-windowed FFTs a quarter of their size apart (75 %
/// overlap), each bin's power averaged over time, read in bands. For displays and file analysis:
/// it allocates in prepare() and never runs on the audio thread.
///
/// Bands are power densities scaled to a reference bin (48 kHz / 8192), so noise reads the same
/// at any resolution or sample rate and pink noise falls 3 dB per octave. A band's average spreads
/// a sine over the band's bins: a sine's own level is read with peakNear().
class SpectrumAnalyser
{
public:
    /// An FFT of 2^`order` points (11 to 14) at `sampleRate`. Starts again.
    void prepare(int order, double sampleRate);
    [[nodiscard]] int order() const noexcept { return order_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// The signal it analyses; a change starts the average again.
    void setChannel(SpectrumChannel channel) noexcept;
    [[nodiscard]] SpectrumChannel channel() const noexcept { return channel_; }
    /// The averaging time constant (0: each transform alone).
    void setAveraging(double ms) noexcept { averagingMs_ = ms; }
    /// Averages everything pushed with equal weight, however long (a file's long-term spectrum).
    void setCumulative(bool cumulative) noexcept { cumulative_ = cumulative; }

    /// Forgets everything pushed: no data until the next transform.
    void reset() noexcept;

    /// Adds `n` stereo samples (`right` null: mono, both sides the same). Transforms every hop
    /// once a whole FFT's worth has arrived. Returns whether it transformed.
    bool push(const float* left, const float* right, std::size_t n);

    /// Whether a transform has run since prepare() or reset().
    [[nodiscard]] bool hasData() const noexcept { return frames_ > 0; }
    /// The averaged spectrum in bands.
    [[nodiscard]] SpectrumCurve bands() const;

    /// The loudest bin within half a band of `hz` (two bins at least): its frequency and level,
    /// interpolated, as a sine there reads (−12 dB for a −12 dBFS sine).
    struct Peak
    {
        double hz = 0.0;
        float db = spectrum::floorDb;
    };
    [[nodiscard]] Peak peakNear(double hz) const;

private:
    void transform();
    /// The bin power of `frame` (a whole FFT, oldest first) into `out`.
    void binPower(const std::vector<float>& frame, std::vector<double>& out);

    Fft fft_{11};
    int order_ = 0, size_ = 0, hop_ = 0;
    int write_ = 0, filled_ = 0, sinceHop_ = 0;
    double sampleRate_ = 48000.0, averagingMs_ = 300.0;
    bool cumulative_ = false;
    SpectrumChannel channel_ = SpectrumChannel::Both;
    std::vector<float> left_, right_, frame_, mags_;
    std::vector<double> power_, now_, other_;
    std::vector<std::complex<float>> scratch_;
    long long frames_ = 0;
};

/// Peak hold per band: each band keeps its highest level for `holdSeconds` (negative: for good),
/// then falls at 20 dB/s (devices/spectrum/SPEC.md).
class SpectrumPeakHold
{
public:
    void update(const SpectrumCurve& now, double elapsedSeconds, double holdSeconds) noexcept;
    void reset() noexcept { any_ = false; }
    [[nodiscard]] bool hasData() const noexcept { return any_; }
    [[nodiscard]] const SpectrumCurve& curve() const noexcept { return held_; }

private:
    SpectrumCurve held_{}, age_{};
    bool any_ = false;
};

/// The bands where two curves collide: both near their loudest, each within `withinDb` of its own
/// loudest band between 30 Hz and 16 kHz, after `slope` (the Spectrum's collisions; the EQ's
/// masking display builds on it). Only bands in that range collide.
[[nodiscard]] std::array<bool, spectrum::numBands>
spectrumCollisions(const SpectrumCurve& a, const SpectrumCurve& b, float slope, float withinDb = 12.0f);

/// The strongest peaks of `curve` after `slope`, loudest first, at most `count`: bands standing at
/// least 6 dB above the lowest point within a third of an octave on each side, at least a third of
/// an octave apart, above `floorDb`.
[[nodiscard]] std::vector<int> spectrumPeaks(const SpectrumCurve& curve, float slope, int count,
                                             float floorDb);

/// The offset that brings `curve`'s average level over 50 Hz–15 kHz to `target`'s (a level-matched
/// reference): the average of their differences there. 0 where either has nothing.
[[nodiscard]] float spectrumMatchOffset(const SpectrumCurve& curve, const SpectrumCurve& target);
} // namespace fd::dsp
