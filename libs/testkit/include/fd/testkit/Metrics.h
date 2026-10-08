#pragma once

#include "fd/testkit/AudioBuffer.h"

#include <complex>
#include <vector>

namespace fd::testkit
{
/// Value reported for "silence" in dB metrics (a finite stand-in for -inf).
inline constexpr double silenceDb = -300.0;

[[nodiscard]] double toDb(double linear) noexcept;

/// Absolute peak over all channels (or one channel if channel >= 0).
[[nodiscard]] double peak(const AudioBuffer& b, int channel = -1) noexcept;
[[nodiscard]] double peakDb(const AudioBuffer& b, int channel = -1) noexcept;
[[nodiscard]] double rms(const AudioBuffer& b, int channel = -1) noexcept;
[[nodiscard]] double rmsDb(const AudioBuffer& b, int channel = -1) noexcept;
[[nodiscard]] double dcOffset(const AudioBuffer& b, int channel) noexcept;

[[nodiscard]] bool allFinite(const AudioBuffer& b) noexcept;
[[nodiscard]] int countSubnormals(const AudioBuffer& b) noexcept;
[[nodiscard]] bool bitIdentical(const AudioBuffer& a, const AudioBuffer& b) noexcept;

/// Peak of (a - b) in dBFS over the common length; silenceDb if identical.
[[nodiscard]] double nullResidualDb(const AudioBuffer& a, const AudioBuffer& b) noexcept;

/// Latency of `processed` relative to `reference` (in samples, 0..maxLag) by cross-correlation.
[[nodiscard]] int estimateLatency(const AudioBuffer& reference, const AudioBuffer& processed, int maxLag,
                                  int channel = 0);

/// Discontinuity detector. Ratio of the largest |second difference| to its 99th percentile.
/// A smooth signal (sine with smoothed parameter changes) stays near 1-2; a click jumps far above.
[[nodiscard]] double clickScore(const AudioBuffer& b, int channel);

/// DTFT of an impulse response at one frequency (exact, no FFT binning).
[[nodiscard]] std::complex<double> responseAt(const float* impulseResponse, int length, double freqHz,
                                              double sampleRate) noexcept;

/// In-place radix-2 complex FFT (size must be a power of two).
void fft(std::vector<std::complex<double>>& data);

/// Magnitude spectrum (linear, Hann window, normalised so a full-scale sine reads ~1.0 at its bin).
/// Uses the first `size` samples (power of two) of `channel`. Returns size/2 + 1 bins.
[[nodiscard]] std::vector<double> magnitudeSpectrum(const AudioBuffer& b, int channel, int size);
} // namespace fd::testkit
