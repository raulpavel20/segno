#pragma once

namespace fd::dsp
{
/// Saturation curves. Each has a closed-form antiderivative for first-order ADAA
/// (anti-derivative anti-aliasing). Pure functions: also used by UI transfer-curve displays.
enum class ShaperCurve
{
    Soft,   // tanh x
    Warm,   // asymmetric tanh (steeper negative side): even harmonics
    Tape,   // x / (1 + |x|)
    Hard,   // clip to ±1
    Fold,   // sin(pi x / 2): folds beyond ±1
    Rectify // 0.8 tanh|x| + 0.2 tanh x: octave-up character
};

inline constexpr int numShaperCurves = 6;

[[nodiscard]] double shape(ShaperCurve curve, double x) noexcept;
[[nodiscard]] double shapeAntiderivative(ShaperCurve curve, double x) noexcept;

/// First-order ADAA waveshaper (one channel). Adds half a sample of delay at its running rate.
class AdaaShaper
{
public:
    void setCurve(ShaperCurve curve) noexcept { curve_ = curve; }
    [[nodiscard]] ShaperCurve curve() const noexcept { return curve_; }
    void reset() noexcept
    {
        x1_ = 0.0;
        f1_ = shapeAntiderivative(curve_, 0.0);
    }

    float process(float in) noexcept
    {
        const double x = in;
        const double F = shapeAntiderivative(curve_, x);
        const double dx = x - x1_;
        double y;
        if (dx > 1e-6 || dx < -1e-6)
            y = (F - f1_) / dx;
        else
            y = shape(curve_, 0.5 * (x + x1_));
        x1_ = x;
        f1_ = F;
        return static_cast<float>(y);
    }

private:
    ShaperCurve curve_ = ShaperCurve::Soft;
    double x1_ = 0.0;
    double f1_ = 0.0;
};
} // namespace fd::dsp
