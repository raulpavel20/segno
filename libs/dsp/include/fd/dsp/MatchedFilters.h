#pragma once

#include "fd/dsp/Filters.h"

#include <complex>

namespace fd::dsp
{
/// Analog-matched filter design (for the EQ): digital filters whose magnitude response matches
/// the analog prototype's up to near Nyquist, without the "cramping" of bilinear designs.
///
/// Three candidates are computed and the one closest to the analog magnitude (checked at 24
/// log-spaced frequencies up to 20 kHz) is used, so the result is never worse than a classic
/// bilinear design:
///   - 5-point: poles and zeros solved together so the magnitude matches exactly at DC, the
///     band edges, the centre and Nyquist (minimum-phase factorisation of |N|^2 and |D|^2);
///   - 3-point (Vicanek 2016): exact pole mapping z = e^{sT}, numerator matched at DC, centre
///     and Nyquist;
///   - bilinear transform with prewarping at the centre (fallback near Nyquist).
/// Pure functions: the UI draws curves from the same coefficients the audio uses.

/// Second-order analog prototype in normalised s (s = j f / fc): (n0 s^2 + n1 s + n2) / (d0 s^2 + d1 s + d2).
struct AnalogBiquad
{
    double n0 = 0, n1 = 0, n2 = 1;
    double d0 = 1, d1 = 1, d2 = 1;
    double q = 0.71; // bandwidth hint for choosing match points

    /// |H(j x)|^2 at normalised frequency x = f / fc.
    [[nodiscard]] double magnitudeSquared(double x) const noexcept;
};

enum class MatchedType
{
    Bell,
    LowShelf,
    HighShelf,
    Tilt,
    Lowpass,  // second order (Q)
    Highpass, // second order (Q)
    Bandpass, // constant 0 dB peak
    Notch
};

/// Analog prototype for a type at gain (dB) and Q.
[[nodiscard]] AnalogBiquad analogPrototype(MatchedType type, double gainDb, double q) noexcept;

/// Matched digital biquad for an analog prototype at cutoff/centre `freqHz` (best candidate).
[[nodiscard]] BiquadCoeffs designMatched(const AnalogBiquad& prototype, double freqHz, double sampleRate) noexcept;

/// The individual candidates (exposed for tests and comparison).
[[nodiscard]] BiquadCoeffs designMatched3(const AnalogBiquad& prototype, double freqHz, double sampleRate) noexcept;
[[nodiscard]] bool designMatched5(const AnalogBiquad& prototype, double freqHz, double sampleRate,
                                  BiquadCoeffs& out) noexcept;
[[nodiscard]] BiquadCoeffs designBilinear(const AnalogBiquad& prototype, double freqHz, double sampleRate) noexcept;

/// Largest |digital - analog| in dB over `numPoints` log-spaced frequencies from 20 Hz to
/// min(20 kHz, 0.95 x Nyquist).
[[nodiscard]] double matchErrorDb(const AnalogBiquad& prototype, const BiquadCoeffs& c, double freqHz,
                                  double sampleRate, int numPoints = 24) noexcept;

/// Matched design for a type. Notches get exact zeros on the unit circle at the centre.
[[nodiscard]] BiquadCoeffs designMatched(MatchedType type, double freqHz, double gainDb, double q,
                                         double sampleRate) noexcept;

/// First-order matched low/high-pass (6 dB/oct), as a biquad with b2 = a2 = 0.
[[nodiscard]] BiquadCoeffs designMatchedFirstOrder(bool highpass, double freqHz, double sampleRate) noexcept;

/// Butterworth section Qs for an order-N low/high-pass cascade (N/2 sections; odd N adds a
/// first-order section). Returns the number of second-order sections written to `qs`.
int butterworthQs(int order, double* qs, int maxSections) noexcept;
} // namespace fd::dsp
