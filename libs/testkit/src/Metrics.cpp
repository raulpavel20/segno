#include "fd/testkit/Metrics.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace fd::testkit
{
namespace
{
template <typename Fn>
void forEachChannel(const AudioBuffer& b, int channel, Fn&& fn)
{
    if (channel >= 0)
        fn(channel);
    else
        for (int c = 0; c < b.numChannels(); ++c)
            fn(c);
}
} // namespace

double toDb(double linear) noexcept
{
    return linear <= 0.0 ? silenceDb : std::max(silenceDb, 20.0 * std::log10(linear));
}

double peak(const AudioBuffer& b, int channel) noexcept
{
    double p = 0.0;
    forEachChannel(b, channel, [&](int c) {
        for (int i = 0; i < b.numSamples(); ++i)
            p = std::max(p, static_cast<double>(std::abs(b.at(c, i))));
    });
    return p;
}

double peakDb(const AudioBuffer& b, int channel) noexcept
{
    return toDb(peak(b, channel));
}

double rms(const AudioBuffer& b, int channel) noexcept
{
    double sum = 0.0;
    long n = 0;
    forEachChannel(b, channel, [&](int c) {
        for (int i = 0; i < b.numSamples(); ++i)
        {
            const double v = b.at(c, i);
            sum += v * v;
        }
        n += b.numSamples();
    });
    return n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

double rmsDb(const AudioBuffer& b, int channel) noexcept
{
    return toDb(rms(b, channel));
}

double dcOffset(const AudioBuffer& b, int channel) noexcept
{
    double sum = 0.0;
    for (int i = 0; i < b.numSamples(); ++i)
        sum += b.at(channel, i);
    return b.numSamples() > 0 ? sum / b.numSamples() : 0.0;
}

bool allFinite(const AudioBuffer& b) noexcept
{
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            if (!std::isfinite(b.at(c, i)))
                return false;
    return true;
}

int countSubnormals(const AudioBuffer& b) noexcept
{
    int n = 0;
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            if (std::fpclassify(b.at(c, i)) == FP_SUBNORMAL)
                ++n;
    return n;
}

bool bitIdentical(const AudioBuffer& a, const AudioBuffer& b) noexcept
{
    if (a.numChannels() != b.numChannels() || a.numSamples() != b.numSamples())
        return false;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < a.numSamples(); ++i)
            if (a.at(c, i) != b.at(c, i)) // exact comparison is the point
                return false;
    return true;
}

double nullResidualDb(const AudioBuffer& a, const AudioBuffer& b) noexcept
{
    const int channels = std::min(a.numChannels(), b.numChannels());
    const int n = std::min(a.numSamples(), b.numSamples());
    double p = 0.0;
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i)
            p = std::max(p, std::abs(static_cast<double>(a.at(c, i)) - b.at(c, i)));
    return toDb(p);
}

int estimateLatency(const AudioBuffer& reference, const AudioBuffer& processed, int maxLag, int channel)
{
    const int n = std::min(reference.numSamples(), processed.numSamples());
    int best = 0;
    double bestValue = -1.0;
    for (int lag = 0; lag <= maxLag && lag < n; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i + lag < n; ++i)
            sum += static_cast<double>(reference.at(channel, i)) * processed.at(channel, i + lag);
        if (sum > bestValue)
        {
            bestValue = sum;
            best = lag;
        }
    }
    return best;
}

double clickScore(const AudioBuffer& b, int channel)
{
    const int n = b.numSamples();
    if (n < 3)
        return 1.0;
    std::vector<double> d2(static_cast<size_t>(n - 2));
    for (int i = 2; i < n; ++i)
        d2[static_cast<size_t>(i - 2)] =
            std::abs(static_cast<double>(b.at(channel, i)) - 2.0 * b.at(channel, i - 1) + b.at(channel, i - 2));
    const double maxValue = *std::max_element(d2.begin(), d2.end());
    auto p99 = d2.begin() + static_cast<long>(0.99 * static_cast<double>(d2.size() - 1));
    std::nth_element(d2.begin(), p99, d2.end());
    const double reference = *p99;
    if (maxValue < 1.0e-12)
        return 1.0;
    return maxValue / std::max(reference, 1.0e-12);
}

std::complex<double> responseAt(const float* impulseResponse, int length, double freqHz, double sampleRate) noexcept
{
    const double w = 2.0 * std::numbers::pi * freqHz / sampleRate;
    std::complex<double> sum{0.0, 0.0};
    for (int n = 0; n < length; ++n)
        sum += static_cast<double>(impulseResponse[n]) * std::polar(1.0, -w * n);
    return sum;
}

void fft(std::vector<std::complex<double>>& data)
{
    const size_t n = data.size();
    if (n == 0 || (n & (n - 1)) != 0)
        throw std::invalid_argument("fft: size must be a power of two");

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen = std::polar(1.0, angle);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w{1.0, 0.0};
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::vector<double> magnitudeSpectrum(const AudioBuffer& b, int channel, int size)
{
    std::vector<std::complex<double>> data(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / size);
        const double v = i < b.numSamples() ? b.at(channel, i) : 0.0;
        data[static_cast<size_t>(i)] = {v * window, 0.0};
    }
    fft(data);
    std::vector<double> mags(static_cast<size_t>(size / 2 + 1));
    for (size_t k = 0; k < mags.size(); ++k)
        mags[k] = std::abs(data[k]) * 4.0 / size;
    return mags;
}
} // namespace fd::testkit
