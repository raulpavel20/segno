#include "fd/dsp/Filters.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
void DcBlocker::prepare(double sampleRate, double cutoffHz) noexcept
{
    r_ = std::exp(-twoPi * cutoffHz / sampleRate);
}

void OnePoleLowpass::setCutoff(double cutoffHz, double sampleRate) noexcept
{
    const double fc = std::clamp(cutoffHz, 1.0, 0.49 * sampleRate);
    const double g = std::tan(pi * fc / sampleRate);
    g_ = g / (1.0 + g);
}

BiquadCoeffs designBiquad(BiquadType type, double sampleRate, double freqHz, double q, double gainDb) noexcept
{
    const double f = std::clamp(freqHz, 1.0, 0.4999 * sampleRate);
    const double w0 = twoPi * f / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * std::max(q, 1.0e-4));
    const double A = std::pow(10.0, gainDb / 40.0);

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
    case BiquadType::Lowpass:
        b0 = (1 - cosw) / 2;
        b1 = 1 - cosw;
        b2 = (1 - cosw) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cosw;
        a2 = 1 - alpha;
        break;
    case BiquadType::Highpass:
        b0 = (1 + cosw) / 2;
        b1 = -(1 + cosw);
        b2 = (1 + cosw) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cosw;
        a2 = 1 - alpha;
        break;
    case BiquadType::Bandpass: // constant 0 dB peak gain
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * cosw;
        a2 = 1 - alpha;
        break;
    case BiquadType::Notch:
        b0 = 1;
        b1 = -2 * cosw;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cosw;
        a2 = 1 - alpha;
        break;
    case BiquadType::Peak:
        b0 = 1 + alpha * A;
        b1 = -2 * cosw;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cosw;
        a2 = 1 - alpha / A;
        break;
    case BiquadType::LowShelf:
    {
        const double sq = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cosw + sq);
        b1 = 2 * A * ((A - 1) - (A + 1) * cosw);
        b2 = A * ((A + 1) - (A - 1) * cosw - sq);
        a0 = (A + 1) + (A - 1) * cosw + sq;
        a1 = -2 * ((A - 1) + (A + 1) * cosw);
        a2 = (A + 1) + (A - 1) * cosw - sq;
        break;
    }
    case BiquadType::HighShelf:
    {
        const double sq = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cosw + sq);
        b1 = -2 * A * ((A - 1) + (A + 1) * cosw);
        b2 = A * ((A + 1) + (A - 1) * cosw - sq);
        a0 = (A + 1) - (A - 1) * cosw + sq;
        a1 = 2 * ((A - 1) - (A + 1) * cosw);
        a2 = (A + 1) - (A - 1) * cosw - sq;
        break;
    }
    case BiquadType::Allpass:
        b0 = 1 - alpha;
        b1 = -2 * cosw;
        b2 = 1 + alpha;
        a0 = 1 + alpha;
        a1 = -2 * cosw;
        a2 = 1 - alpha;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

std::complex<double> biquadResponse(const BiquadCoeffs& c, double freqHz, double sampleRate) noexcept
{
    const double w = twoPi * freqHz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w); // z^-1
    const std::complex<double> z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}
} // namespace fd::dsp
