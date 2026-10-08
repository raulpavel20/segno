#include "fd/dsp/Resampling.h"

#include "fd/dsp/Math.h"

#include <algorithm>

namespace fd::dsp
{
namespace
{
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

/// Kaiser window at x in [-1, 1].
double kaiser(double x, double beta)
{
    if (std::abs(x) >= 1.0)
        return 0.0;
    return besselI0(beta * std::sqrt(1.0 - x * x)) / besselI0(beta);
}

double sinc(double x)
{
    return std::abs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
}

constexpr double kCutoff = 0.9; // of Nyquist at stretch 1: -6 dB at 0.45 fs
constexpr double kBeta = 8.0;   // ~80 dB stopband
} // namespace

SincKernels::SincKernels()
{
    for (int j = 0; j <= stretchSteps; ++j)
    {
        const double stretch = 1.0 + static_cast<double>(j) / stretchSteps;
        auto& t = tables_[static_cast<size_t>(j)];
        t.taps = 2 * static_cast<int>(std::ceil(zeroCrossings * stretch));
        const double half = t.taps / 2;
        const double fc = kCutoff / stretch;
        t.data.resize(static_cast<size_t>((phases + 1) * t.taps));
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = static_cast<double>(p) / phases;
            float* row = t.data.data() + static_cast<size_t>(p * t.taps);
            double sum = 0.0;
            for (int k = 0; k < t.taps; ++k)
            {
                const double d = frac - (k - (t.taps / 2 - 1)); // distance from tap k's sample
                const double h = fc * sinc(fc * d) * kaiser(d / half, kBeta);
                row[k] = static_cast<float>(h);
                sum += h;
            }
            for (int k = 0; k < t.taps; ++k) // unity DC gain at every phase
                row[k] = static_cast<float>(row[k] / sum);
        }
    }
}

const SincKernels& SincKernels::instance()
{
    static const SincKernels kernels;
    return kernels;
}

const SincKernels::Table& SincKernels::table(double stretch) const noexcept
{
    const double s = std::clamp(stretch, 1.0, 2.0);
    const int j = std::min(stretchSteps, static_cast<int>(std::ceil((s - 1.0) * stretchSteps - 1e-9)));
    return tables_[static_cast<size_t>(j)];
}

std::vector<float> decimateByTwo(std::span<const float> in)
{
    // Pass band to 0.205 fs, stop band from 0.25 fs (the new Nyquist) at ~80 dB.
    constexpr int half = 64;
    constexpr double fc = 2.0 * 0.2275; // of the input's Nyquist
    std::array<double, 2 * half + 1> h{};
    double sum = 0.0;
    for (int k = -half; k <= half; ++k)
    {
        h[static_cast<size_t>(k + half)] = fc * sinc(fc * k) * kaiser(static_cast<double>(k) / (half + 1), kBeta);
        sum += h[static_cast<size_t>(k + half)];
    }
    for (double& c : h)
        c /= sum;

    const long n = static_cast<long>(in.size());
    std::vector<float> out(static_cast<size_t>((n + 1) / 2));
    for (long i = 0; i < static_cast<long>(out.size()); ++i)
    {
        double acc = 0.0;
        const long centre = 2 * i;
        const long k0 = std::max<long>(-half, -centre);
        const long k1 = std::min<long>(half, n - 1 - centre);
        for (long k = k0; k <= k1; ++k)
            acc += h[static_cast<size_t>(k + half)] * in[static_cast<size_t>(centre + k)];
        out[static_cast<size_t>(i)] = static_cast<float>(acc);
    }
    return out;
}
} // namespace fd::dsp
