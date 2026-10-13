#include "fd/dsp/SpectralDynamics.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
constexpr double kTiltPerOctave = 3.0;      // dB: the detection tilt, around 1 kHz
constexpr double kNeighbours = 1.0 / 48;    // octaves either side: the trigger's smoothing across bins
constexpr double kGainSmoothing = 1.0 / 96; // octaves either side: the gains' smoothing
constexpr double kTriggerMs = 40.0;         // the trigger's smoothing over time (at 1 kHz)
constexpr double kWidest = 2.0;             // octaves either side: the surroundings at Detail 0
constexpr double kNarrowing = 24.0;         // ...and how much narrower at Detail 1
constexpr float kClip = 0.5f;               // dB: peaks clipped this far over the surroundings
constexpr float kNoise = 2.5f;              // dB: sticking out by less is how noise goes
constexpr float kSure = 4.0f;               // dB more: then it's sure to be a peak
constexpr float kKnee = 2.0f;               // dB: Manual's soft start

/// The mean of `values` over each bin's window of `octaves` either side, and at least `minBins`
/// either side, into `out` (prefix sums; bin 0, DC, is left as it is).
void windowMean(const float* values, int bins, double octaves, int minBins, std::vector<double>& sums,
                float* out) noexcept
{
    sums[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        sums[static_cast<size_t>(k + 1)] = sums[static_cast<size_t>(k)] + values[k];
    out[0] = values[0];
    const double f = std::pow(2.0, octaves);
    for (int k = 1; k < bins; ++k)
    {
        const int lo = std::clamp(std::min(static_cast<int>(k / f), k - minBins), 1, bins - 1);
        const int hi = std::clamp(std::max(static_cast<int>(std::ceil(k * f)), k + minBins), 1, bins - 1);
        out[k] = static_cast<float>((sums[static_cast<size_t>(hi + 1)] - sums[static_cast<size_t>(lo)]) /
                                    (hi - lo + 1));
    }
}
} // namespace

void SpectralDynamics::Scratch::prepare(int maxBins)
{
    const auto n = static_cast<size_t>(std::max(2, maxBins));
    sums.assign(n + 1, 0.0);
    work.assign(4 * n, 0.0f);
    tiltDb.assign(n, 0.0f);
    step.assign(n, 0);
    tunedRate = tunedFrame = 0.0;
    tunedSize = tunedBins = 0;
}

double SpectralDynamics::Scratch::timeScale(int step) noexcept
{
    return std::pow(2.0, -1.0 + 2.0 * step / (timeSteps - 1)); // 0.5 ... 2
}

void SpectralDynamics::Scratch::tune(double sampleRate, int fftSize, double frameSeconds, int bins) noexcept
{
    if (sampleRate == tunedRate && fftSize == tunedSize && bins == tunedBins && frameSeconds == tunedFrame)
        return;
    const double binHz = sampleRate / fftSize;
    for (int k = 0; k < bins; ++k)
    {
        const double hz = std::max(binHz, k * binHz);
        tiltDb[static_cast<size_t>(k)] = static_cast<float>(kTiltPerOctave * std::log2(hz / 1000.0));
        // Faster in the highs, slower in the lows (the resonance suppressors' way), within 2x, in
        // steps (a twelfth of an octave in time apart).
        const double scale = std::clamp(std::sqrt(1000.0 / hz), 0.5, 2.0);
        step[static_cast<size_t>(k)] =
            static_cast<std::uint8_t>(std::lround((std::log2(scale) + 1.0) * 0.5 * (timeSteps - 1)));
    }
    for (int i = 0; i < timeSteps; ++i)
        follow[static_cast<size_t>(i)] = coefficient(kTriggerMs * timeScale(i), frameSeconds);
    tunedRate = sampleRate;
    tunedSize = fftSize;
    tunedFrame = frameSeconds;
    tunedBins = bins;
}

float SpectralDynamics::Scratch::coefficient(double ms, double frameSeconds) noexcept
{
    return static_cast<float>(1.0 - std::exp(-frameSeconds / std::max(1e-7, ms * 0.001)));
}

void SpectralDynamics::prepare(int maxBins)
{
    const auto n = static_cast<size_t>(std::max(2, maxBins));
    smoothed_.assign(n, 0.0f);
    gain_.assign(n, 0.0f);
    tunedAttack_ = tunedRelease_ = -1.0f;
    primed_ = false;
}

void SpectralDynamics::reset() noexcept
{
    std::fill(smoothed_.begin(), smoothed_.end(), 0.0f);
    std::fill(gain_.begin(), gain_.end(), 0.0f);
    primed_ = false;
}

void SpectralDynamics::process(const float* power, const float* weight, int bins, const Settings& s,
                               Scratch& scratch, float* gainDb) noexcept
{
    bins = std::clamp(bins, 2, static_cast<int>(std::min(gain_.size(), scratch.tiltDb.size())));
    scratch.tune(s.sampleRate, s.fftSize, s.frameSeconds, bins);
    if (tunedAttack_ != s.attackMs || tunedRelease_ != s.releaseMs || tunedFrame_ != s.frameSeconds)
    {
        for (int i = 0; i < Scratch::timeSteps; ++i)
        {
            attack_[static_cast<size_t>(i)] =
                Scratch::coefficient(s.attackMs * Scratch::timeScale(i), s.frameSeconds);
            release_[static_cast<size_t>(i)] =
                Scratch::coefficient(s.releaseMs * Scratch::timeScale(i), s.frameSeconds);
        }
        tunedAttack_ = s.attackMs;
        tunedRelease_ = s.releaseMs;
        tunedFrame_ = s.frameSeconds;
    }

    const auto nb = static_cast<size_t>(bins);
    float* level = scratch.work.data();
    float* across = scratch.work.data() + nb;
    float* surround = scratch.work.data() + 2 * nb;
    float* target = scratch.work.data() + 3 * nb;
    auto& sums = scratch.sums;

    // The trigger: smoothed across neighbouring bins (in power; at least the frame's own
    // resolution, which the padding interpolates), then over time, then tilted (in dB).
    windowMean(power, bins, kNeighbours, std::max(1, s.padding / 2), sums, across);
    for (int k = 0; k < bins; ++k)
    {
        auto& p = smoothed_[static_cast<size_t>(k)];
        p = primed_ ? p + scratch.follow[scratch.step[static_cast<size_t>(k)]] * (across[k] - p) : across[k];
        level[k] = 10.0f * std::log10(std::max(p, 1e-20f)) + scratch.tiltDb[static_cast<size_t>(k)];
    }
    primed_ = true;

    const float depth = std::abs(s.rangeDb), sign = s.rangeDb < 0.0f ? -1.0f : 1.0f;
    if (s.autoThreshold)
    {
        // Its surroundings, four times: each time with the last one's peaks clipped, so a
        // resonance doesn't raise its own baseline.
        const double octaves =
            kWidest * std::pow(kNarrowing, -std::clamp(static_cast<double>(s.detail), 0.0, 1.0));
        windowMean(level, bins, octaves, s.padding, sums, surround);
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int k = 0; k < bins; ++k)
                across[k] = std::min(level[k], surround[k] + kClip);
            windowMean(across, bins, octaves, s.padding, sums, surround);
        }
        // What sticks out comes down to its surroundings, up to the range. Noise sticks out a
        // little, by chance: a bin starts to move only past that, and is brought all the way down
        // once it's sure to be a peak.
        for (int k = 0; k < bins; ++k)
        {
            const float over = level[k] - surround[k];
            const float sure = std::clamp((over - kNoise) / kSure, 0.0f, 1.0f);
            const float excess = std::max(0.0f, over) * sure * sure * (3.0f - 2.0f * sure);
            target[k] = sign * std::min(depth, excess) * std::clamp(weight[k], 0.0f, 1.0f);
        }
    }
    else
    {
        // What's over the threshold comes down by as much, through a soft knee, up to the range.
        for (int k = 0; k < bins; ++k)
        {
            const float over = level[k] - s.thresholdDb;
            float excess = 0.0f;
            if (over >= kKnee)
                excess = over;
            else if (over > -kKnee)
                excess = (over + kKnee) * (over + kKnee) / (4.0f * kKnee);
            target[k] = sign * std::min(depth, excess) * std::clamp(weight[k], 0.0f, 1.0f);
        }
    }

    // Each bin's gain follows: away from rest at the attack, back at the release.
    for (int k = 0; k < bins; ++k)
    {
        auto& g = gain_[static_cast<size_t>(k)];
        const bool away = std::abs(target[k]) > std::abs(g);
        g += (away ? attack_ : release_)[scratch.step[static_cast<size_t>(k)]] * (target[k] - g);
    }
    // Smoothed across frequency: filters, not holes.
    windowMean(gain_.data(), bins, kGainSmoothing, 0, sums, gainDb);
}
} // namespace fd::dsp
