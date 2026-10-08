#include "fd/dsp/Waveshapers.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
constexpr double kWarmNegative = 1.6; // slope stays 1 at 0; negative side saturates at -1/1.6

/// log(cosh x), stable for large |x|.
double logCosh(double x) noexcept
{
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - std::log(2.0);
}

double sign(double x) noexcept
{
    return x < 0.0 ? -1.0 : 1.0;
}
} // namespace

double shape(ShaperCurve curve, double x) noexcept
{
    switch (curve)
    {
    case ShaperCurve::Soft:
        return std::tanh(x);
    case ShaperCurve::Warm:
        return x >= 0.0 ? std::tanh(x) : std::tanh(kWarmNegative * x) / kWarmNegative;
    case ShaperCurve::Tape:
        return x / (1.0 + std::abs(x));
    case ShaperCurve::Hard:
        return std::clamp(x, -1.0, 1.0);
    case ShaperCurve::Fold:
        return std::sin(pi * x * 0.5);
    case ShaperCurve::Rectify:
        return 0.8 * std::tanh(std::abs(x)) + 0.2 * std::tanh(x);
    }
    return x;
}

double shapeAntiderivative(ShaperCurve curve, double x) noexcept
{
    switch (curve)
    {
    case ShaperCurve::Soft:
        return logCosh(x);
    case ShaperCurve::Warm:
        return x >= 0.0 ? logCosh(x) : logCosh(kWarmNegative * x) / (kWarmNegative * kWarmNegative);
    case ShaperCurve::Tape:
    {
        const double a = std::abs(x);
        return a - std::log1p(a);
    }
    case ShaperCurve::Hard:
    {
        const double a = std::abs(x);
        return a <= 1.0 ? 0.5 * x * x : a - 0.5;
    }
    case ShaperCurve::Fold:
        return (2.0 / pi) * (1.0 - std::cos(pi * x * 0.5));
    case ShaperCurve::Rectify:
        return 0.8 * sign(x) * logCosh(x) + 0.2 * logCosh(x);
    }
    return 0.5 * x * x;
}
} // namespace fd::dsp
