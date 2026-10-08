#include "fd/dsp/Oversampler.h"

#include "fd/dsp/Math.h"

#include <cassert>
#include <cmath>

namespace fd::dsp
{
namespace
{
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}

int nextPow2(int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

// Stage lengths (4k + 3). Stage 1 carries the steep transition (0.45–0.55 of the base rate);
// later stages only reject images far above the band of interest.
constexpr std::array<int, 3> kTaps{127, 31, 19};
constexpr std::array<double, 3> kAttenuationDb{110.0, 110.0, 110.0};
} // namespace

void HalfbandStage::design(int numTaps, double attenuationDb)
{
    assert(numTaps % 4 == 3);
    const int c = (numTaps - 1) / 2;
    const double beta = attenuationDb > 50.0 ? 0.1102 * (attenuationDb - 8.7) : 0.5842 * std::pow(attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
    const double i0b = besselI0(beta);
    taps_.assign(static_cast<size_t>(numTaps), 0.0);
    for (int n = 0; n < numTaps; ++n)
    {
        const int k = n - c;
        if (k == 0)
        {
            taps_[static_cast<size_t>(n)] = 0.5;
            continue;
        }
        if (k % 2 == 0)
            continue; // half-band: even offsets are exactly zero
        const double x = static_cast<double>(k) / static_cast<double>(c);
        const double window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - x * x))) / i0b;
        const double sinc = std::sin(pi * k / 2.0) / (pi * k / 2.0);
        taps_[static_cast<size_t>(n)] = 0.5 * sinc * window;
    }

    for (auto& p : upPhases_)
        p.clear();
    downTaps_.clear();
    for (int n = 0; n < numTaps; ++n)
    {
        const double h = taps_[static_cast<size_t>(n)];
        if (h == 0.0)
            continue;
        upPhases_[static_cast<size_t>(n % 2)].push_back({n / 2, 2.0 * h});
        downTaps_.push_back({n, h});
    }

    upMask_ = nextPow2(numTaps / 2 + 2) - 1;
    downMask_ = nextPow2(numTaps + 1) - 1;
    upHistory_.assign(static_cast<size_t>(upMask_ + 1), 0.0f);
    downHistory_.assign(static_cast<size_t>(downMask_ + 1), 0.0f);
    reset();
}

void HalfbandStage::reset() noexcept
{
    std::fill(upHistory_.begin(), upHistory_.end(), 0.0f);
    std::fill(downHistory_.begin(), downHistory_.end(), 0.0f);
    upPos_ = downPos_ = 0;
}

void HalfbandStage::upsample(const float* in, float* out, int n) noexcept
{
    for (int m = 0; m < n; ++m)
    {
        upPos_ = (upPos_ + 1) & upMask_;
        upHistory_[static_cast<size_t>(upPos_)] = in[m];
        for (int p = 0; p < 2; ++p)
        {
            double acc = 0.0;
            for (const auto& [j, h] : upPhases_[static_cast<size_t>(p)])
                acc += h * upHistory_[static_cast<size_t>((upPos_ - j) & upMask_)];
            out[2 * m + p] = static_cast<float>(acc);
        }
    }
}

void HalfbandStage::downsample(const float* in, float* out, int n) noexcept
{
    // Keep the even phase: y[m] = sum h[k] z[2m - k]. Decimating on the odd phase would add a
    // half-sample offset and break the whole-sample latency.
    for (int m = 0; m < n; ++m)
    {
        downPos_ = (downPos_ + 1) & downMask_;
        downHistory_[static_cast<size_t>(downPos_)] = in[2 * m];
        double acc = 0.0;
        for (const auto& [k, h] : downTaps_)
            acc += h * downHistory_[static_cast<size_t>((downPos_ - k) & downMask_)];
        out[m] = static_cast<float>(acc);
        downPos_ = (downPos_ + 1) & downMask_;
        downHistory_[static_cast<size_t>(downPos_)] = in[2 * m + 1];
    }
}

void Oversampler::prepare(int maxBlockSize, int numChannels)
{
    maxBlock_ = maxBlockSize;
    channels_.assign(static_cast<size_t>(numChannels), {});

    // Latency per stage count, in samples at the highest rate of that chain:
    // stage s (rate 2^s) up + down contributes (N_s - 1) samples at rate 2^s.
    // Pad with whole samples at the top rate to reach a whole number of base samples, then pad
    // at the base rate up to the longest chain, so every factor has the same latency.
    std::array<double, 4> chainLatency{};
    for (int stages = 1; stages <= 3; ++stages)
    {
        double latencyTop = 0.0; // in samples at rate 2^stages
        for (int s = 1; s <= stages; ++s)
            latencyTop += (kTaps[static_cast<size_t>(s - 1)] - 1) * std::pow(2.0, stages - s);
        const int top = 1 << stages;
        const auto whole = static_cast<int>(std::ceil(latencyTop / top - 1e-9));
        fineDelayFor_[static_cast<size_t>(stages)] = whole * top - static_cast<int>(std::lround(latencyTop));
        chainLatency[static_cast<size_t>(stages)] = whole;
    }
    latency_ = static_cast<int>(chainLatency[3]);
    for (int stages = 1; stages <= 3; ++stages)
        baseDelayFor_[static_cast<size_t>(stages)] = latency_ - static_cast<int>(chainLatency[static_cast<size_t>(stages)]);

    for (auto& ch : channels_)
    {
        for (int s = 0; s < 3; ++s)
        {
            ch.up[static_cast<size_t>(s)].design(kTaps[static_cast<size_t>(s)], kAttenuationDb[static_cast<size_t>(s)]);
            ch.down[static_cast<size_t>(s)].design(kTaps[static_cast<size_t>(s)], kAttenuationDb[static_cast<size_t>(s)]);
        }
        ch.fineDelay.assign(16, 0.0f);
        ch.baseDelay.assign(static_cast<size_t>(nextPow2(latency_ + 1)), 0.0f);
    }
    bufA_.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(maxBlockSize * maxFactor)));
    bufB_.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(maxBlockSize * maxFactor)));
    outPtrs_.assign(static_cast<size_t>(numChannels), nullptr);
    reset();
}

void Oversampler::setFactor(int factor) noexcept
{
    factor_ = factor <= 2 ? 2 : (factor <= 4 ? 4 : 8);
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& ch : channels_)
    {
        for (auto& s : ch.up)
            s.reset();
        for (auto& s : ch.down)
            s.reset();
        std::fill(ch.fineDelay.begin(), ch.fineDelay.end(), 0.0f);
        std::fill(ch.baseDelay.begin(), ch.baseDelay.end(), 0.0f);
        ch.fineDelayPos = ch.baseDelayPos = 0;
    }
}

float* const* Oversampler::upsample(const float* const* in, int numChannels, int n) noexcept
{
    const int stages = factor_ == 2 ? 1 : (factor_ == 4 ? 2 : 3);
    const int fine = fineDelayFor_[static_cast<size_t>(stages)];
    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels_[static_cast<size_t>(c)];
        float* a = bufA_[static_cast<size_t>(c)].data();
        float* b = bufB_[static_cast<size_t>(c)].data();
        const float* src = in[c];
        int len = n;
        for (int s = 0; s < stages; ++s)
        {
            float* dst = (s % 2 == 0) ? a : b;
            ch.up[static_cast<size_t>(s)].upsample(src, dst, len);
            src = dst;
            len *= 2;
        }
        float* result = const_cast<float*>(src);
        if (fine > 0) // whole-sample padding at the top rate
        {
            const int mask = static_cast<int>(ch.fineDelay.size()) - 1;
            for (int i = 0; i < len; ++i)
            {
                ch.fineDelay[static_cast<size_t>(ch.fineDelayPos)] = result[i];
                result[i] = ch.fineDelay[static_cast<size_t>((ch.fineDelayPos - fine) & mask)];
                ch.fineDelayPos = (ch.fineDelayPos + 1) & mask;
            }
        }
        outPtrs_[static_cast<size_t>(c)] = result;
    }
    return outPtrs_.data();
}

void Oversampler::downsample(float* const* out, int numChannels, int n) noexcept
{
    const int stages = factor_ == 2 ? 1 : (factor_ == 4 ? 2 : 3);
    const int pad = baseDelayFor_[static_cast<size_t>(stages)];
    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = channels_[static_cast<size_t>(c)];
        float* src = outPtrs_[static_cast<size_t>(c)];
        float* other = src == bufA_[static_cast<size_t>(c)].data() ? bufB_[static_cast<size_t>(c)].data()
                                                                   : bufA_[static_cast<size_t>(c)].data();
        int len = n * factor_;
        for (int s = stages - 1; s >= 0; --s)
        {
            len /= 2;
            float* dst = s == 0 ? out[c] : other;
            ch.down[static_cast<size_t>(s)].downsample(src, dst, len);
            other = src;
            src = dst;
        }
        if (pad > 0)
        {
            const int mask = static_cast<int>(ch.baseDelay.size()) - 1;
            for (int i = 0; i < n; ++i)
            {
                ch.baseDelay[static_cast<size_t>(ch.baseDelayPos)] = out[c][i];
                out[c][i] = ch.baseDelay[static_cast<size_t>((ch.baseDelayPos - pad) & mask)];
                ch.baseDelayPos = (ch.baseDelayPos + 1) & mask;
            }
        }
    }
}
} // namespace fd::dsp
