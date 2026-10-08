#pragma once

#include <complex>

namespace fd::dsp
{
/// First-order DC blocker: y[n] = x[n] - x[n-1] + r * y[n-1].
class DcBlocker
{
public:
    void prepare(double sampleRate, double cutoffHz = 5.0) noexcept;
    void reset() noexcept { x1_ = y1_ = 0.0; }

    float process(float x) noexcept
    {
        const double y = static_cast<double>(x) - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return static_cast<float>(y);
    }

private:
    double r_ = 0.999;
    double x1_ = 0.0, y1_ = 0.0;
};

/// One-pole low-pass (topology-preserving transform, matched at the cutoff).
class OnePoleLowpass
{
public:
    void setCutoff(double cutoffHz, double sampleRate) noexcept;
    void reset(float value = 0.0f) noexcept { s_ = value; }

    float process(float x) noexcept
    {
        const double v = (static_cast<double>(x) - s_) * g_;
        const double y = v + s_;
        s_ = y + v;
        return static_cast<float>(y);
    }

private:
    double g_ = 0.5;
    double s_ = 0.0;
};

enum class BiquadType
{
    Lowpass,
    Highpass,
    Bandpass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
    Allpass
};

/// Normalised biquad coefficients (a0 == 1).
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

/// RBJ "Audio EQ Cookbook" designs. `gainDb` is used by Peak and the shelves only.
[[nodiscard]] BiquadCoeffs designBiquad(BiquadType type, double sampleRate, double freqHz, double q,
                                        double gainDb = 0.0) noexcept;

/// Complex frequency response H(e^jw). Pure function: used by both DSP tests and UI curves.
[[nodiscard]] std::complex<double> biquadResponse(const BiquadCoeffs& c, double freqHz,
                                                  double sampleRate) noexcept;

[[nodiscard]] inline double biquadMagnitude(const BiquadCoeffs& c, double freqHz, double sampleRate) noexcept
{
    return std::abs(biquadResponse(c, freqHz, sampleRate));
}

/// Transposed direct form II biquad with double-precision state.
class Biquad
{
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { s1_ = s2_ = 0.0; }

    float process(float in) noexcept
    {
        const double x = in;
        const double y = c_.b0 * x + s1_;
        s1_ = c_.b1 * x - c_.a1 * y + s2_;
        s2_ = c_.b2 * x - c_.a2 * y;
        return static_cast<float>(y);
    }

private:
    BiquadCoeffs c_{};
    double s1_ = 0.0, s2_ = 0.0;
};
} // namespace fd::dsp
