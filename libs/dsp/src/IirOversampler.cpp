#include "fd/dsp/IirOversampler.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
// 106 dB down past the transition, which runs from 0.2268 to 0.2732 of the oversampled rate
// (20 kHz to 24.1 kHz at 44.1): the images and aliases all land above 20 kHz, the audible band
// untouched at every rate from 44.1 kHz up.
constexpr double kAttenuationDb = 100.0, kTransition = 0.0232;

// The elliptic design's sums (the nome's series); they converge fast, the nome being small.
double numeratorSum(double q, int order, int c)
{
    double sum = 0.0, term = 0.0, sign = 1.0;
    int i = 0;
    do
    {
        term = std::pow(q, i * (i + 1)) * std::sin((2 * i + 1) * c * pi / order) * sign;
        sum += term;
        sign = -sign;
        ++i;
    } while (std::abs(term) > 1e-100);
    return sum;
}

double denominatorSum(double q, int order, int c)
{
    double sum = 0.0, term = 0.0, sign = -1.0;
    int i = 1;
    do
    {
        term = std::pow(q, i * i) * std::cos(2 * i * c * pi / order) * sign;
        sum += term;
        sign = -sign;
        ++i;
    } while (std::abs(term) > 1e-100);
    return sum;
}
} // namespace

std::vector<double> IirHalfband::design(double attenuationDb, double transition)
{
    // The transition's elliptic modulus and its nome.
    const double k = std::pow(std::tan((1.0 - 2.0 * transition) * pi / 4.0), 2.0);
    const double kk = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    // The order the attenuation needs (odd).
    const double power = std::pow(10.0, -attenuationDb / 10.0);
    const double a = power / (1.0 - power);
    int order = static_cast<int>(std::ceil(std::log(a * a / 16.0) / std::log(q)));
    order += order % 2 == 0 ? 1 : 0;

    std::vector<double> coefficients;
    for (int index = 0; index < (order - 1) / 2; ++index)
    {
        const int c = index + 1;
        const double w = numeratorSum(q, order, c) * std::pow(q, 0.25) / (denominatorSum(q, order, c) + 0.5);
        const double w2 = w * w;
        const double x = std::sqrt((1.0 - w2 * k) * (1.0 - w2 / k)) / (1.0 + w2);
        coefficients.push_back((1.0 - x) / (1.0 + x));
    }
    return coefficients;
}

double IirOversampler::Branch::process(double in, const double* coefficients) noexcept
{
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double out = coefficients[i] * (in - y[i]) + x[i];
        x[i] = in;
        y[i] = out;
        in = out;
    }
    return in;
}

namespace
{
struct DecimatorCoefficients
{
    std::array<double, 5> even{}, odd{};
    DecimatorCoefficients()
    {
        const auto c = IirHalfband::design(kAttenuationDb, kTransition);
        for (size_t i = 0; i < even.size(); ++i)
        {
            even[i] = c[2 * i];
            odd[i] = c[2 * i + 1];
        }
    }
};
const DecimatorCoefficients kDecimator;

double allpasses(std::array<double, 5>& x, std::array<double, 5>& y, const std::array<double, 5>& c,
                 double in) noexcept
{
    for (size_t i = 0; i < c.size(); ++i)
    {
        const double out = c[i] * (in - y[i]) + x[i];
        x[i] = in;
        y[i] = out;
        in = out;
    }
    return in;
}
} // namespace

HalfbandDecimator::HalfbandDecimator() = default;

void HalfbandDecimator::reset() noexcept
{
    even_ = {};
    odd_ = {};
}

float HalfbandDecimator::process(float first, float second) noexcept
{
    // As IirOversampler::downsample: A0 on the pair's second sample, A1 on its first.
    return static_cast<float>(0.5 * (allpasses(even_.x, even_.y, kDecimator.even, second) +
                                     allpasses(odd_.x, odd_.y, kDecimator.odd, first)));
}

void IirOversampler::prepare(int maxBlockSize, int numChannels)
{
    const auto coefficients = IirHalfband::design(kAttenuationDb, kTransition);
    for (size_t i = 0; i < even_.size(); ++i)
    {
        even_[i] = coefficients[2 * i];
        odd_[i] = coefficients[2 * i + 1];
    }
    maxBlock_ = maxBlockSize;
    channels_.assign(static_cast<size_t>(numChannels), Channel{});
    buffers_.assign(static_cast<size_t>(numChannels),
                    std::vector<float>(static_cast<size_t>(factor * maxBlockSize)));
    pointers_.resize(static_cast<size_t>(numChannels));
    for (size_t c = 0; c < buffers_.size(); ++c)
        pointers_[c] = buffers_[c].data();

    // The latency: where an impulse's round trip peaks.
    Channel probe;
    double peak = 0.0;
    latency_ = 0;
    for (int n = 0; n < 64; ++n)
    {
        const double in = n == 0 ? 1.0 : 0.0;
        const double even = probe.up0.process(in, even_.data()), odd = probe.up1.process(in, odd_.data());
        const double out =
            0.5 * (probe.down0.process(odd, even_.data()) + probe.down1.process(even, odd_.data()));
        if (std::abs(out) > peak)
        {
            peak = std::abs(out);
            latency_ = n;
        }
    }
}

void IirOversampler::reset() noexcept
{
    std::fill(channels_.begin(), channels_.end(), Channel{});
}

float* const* IirOversampler::upsample(const float* const* in, int numChannels, int n) noexcept
{
    // The oversampled pairs: A0's output first, A1's second (the z^-1 puts it a sample later).
    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels_[static_cast<size_t>(c)];
        float* out = pointers_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i)
        {
            out[2 * i] = static_cast<float>(ch.up0.process(in[c][i], even_.data()));
            out[2 * i + 1] = static_cast<float>(ch.up1.process(in[c][i], odd_.data()));
        }
    }
    return pointers_.data();
}

void IirOversampler::downsample(float* const* out, int numChannels, int n) noexcept
{
    // The filter's output at each pair's second sample: A0 on the odd samples, A1 on the even.
    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels_[static_cast<size_t>(c)];
        const float* in = pointers_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i)
            out[c][i] = static_cast<float>(0.5 * (ch.down0.process(in[2 * i + 1], even_.data()) +
                                                  ch.down1.process(in[2 * i], odd_.data())));
    }
}

void IirOversampler::roundTrip(float* const* io, int numChannels, int n) noexcept
{
    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i)
        {
            const double x = io[c][i];
            const double even = ch.tripUp0.process(x, even_.data()), odd = ch.tripUp1.process(x, odd_.data());
            io[c][i] =
                static_cast<float>(0.5 * (ch.tripDown0.process(static_cast<float>(odd), even_.data()) +
                                          ch.tripDown1.process(static_cast<float>(even), odd_.data())));
        }
    }
}
} // namespace fd::dsp
