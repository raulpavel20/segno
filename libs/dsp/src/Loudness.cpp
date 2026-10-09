#include "fd/dsp/Loudness.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fd::dsp
{
namespace
{
constexpr float kMinusInf = -std::numeric_limits<float>::infinity();

/// Loudness of a (channel-summed) mean square, in LUFS (BS.1770's -0.691 offset).
double lufs(double meanSquare) noexcept
{
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : -std::numeric_limits<double>::infinity();
}
} // namespace

std::array<BiquadCoeffs, 2> kWeighting(double sampleRate) noexcept
{
    // The analogue prototypes behind BS.1770's 48 kHz coefficients, so other rates match.
    std::array<BiquadCoeffs, 2> out{};
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(pi * f0 / sampleRate);
        const double vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        out[0] = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                  2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;
        out[1] = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    return out;
}

// ---- LoudnessMeter -----------------------------------------------------------------------------

void LoudnessMeter::prepare(double sampleRate, int numChannels)
{
    numChannels_ = std::clamp(numChannels, 1, maxChannels);
    const auto coeffs = kWeighting(sampleRate);
    for (auto& channel : filters_)
        for (size_t i = 0; i < channel.size(); ++i)
            channel[i].setCoeffs(coeffs[i]);
    stepLength_ = std::max(1, static_cast<int>(std::lround(sampleRate * 0.1)));
    binEnergy_.assign(histogramBins, 0.0);
    binCount_.assign(histogramBins, 0);
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& channel : filters_)
        for (auto& f : channel)
            f.reset();
    stepPos_ = 0;
    stepSum_ = 0.0;
    steps_.fill(0.0);
    stepsDone_ = stepWrite_ = 0;
    maxMomentary_ = maxShortTerm_ = kMinusInf;
    std::fill(binEnergy_.begin(), binEnergy_.end(), 0.0);
    std::fill(binCount_.begin(), binCount_.end(), 0);
}

void LoudnessMeter::process(const float* const* channels, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int c = 0; c < numChannels_; ++c)
        {
            auto& f = filters_[static_cast<size_t>(c)];
            const double y = f[1].process(f[0].process(channels[c][i]));
            sum += y * y;
        }
        stepSum_ += sum;
        if (++stepPos_ == stepLength_)
            finishStep();
    }
}

void LoudnessMeter::finishStep() noexcept
{
    steps_[static_cast<size_t>(stepWrite_)] = stepSum_ / stepLength_;
    stepWrite_ = (stepWrite_ + 1) % blocksShortTerm;
    ++stepsDone_;
    stepSum_ = 0.0;
    stepPos_ = 0;

    if (stepsDone_ >= blocksMomentary)
    {
        // A 400 ms gating block (75 % overlap): kept by loudness for the integrated value.
        const double block = meanOfLast(blocksMomentary);
        const double loudness = lufs(block);
        maxMomentary_ = std::max(maxMomentary_, static_cast<float>(loudness));
        if (loudness > histogramFloor)
        {
            const auto bin = std::clamp(static_cast<int>((loudness - histogramFloor) / histogramStep), 0, histogramBins - 1);
            binEnergy_[static_cast<size_t>(bin)] += block;
            ++binCount_[static_cast<size_t>(bin)];
        }
    }
    if (stepsDone_ >= blocksShortTerm)
        maxShortTerm_ = std::max(maxShortTerm_, shortTerm());
}

double LoudnessMeter::meanOfLast(int steps) const noexcept
{
    double sum = 0.0;
    for (int i = 1; i <= steps; ++i)
        sum += steps_[static_cast<size_t>((stepWrite_ - i + blocksShortTerm) % blocksShortTerm)];
    return sum / steps;
}

float LoudnessMeter::momentary() const noexcept
{
    return stepsDone_ >= blocksMomentary ? static_cast<float>(lufs(meanOfLast(blocksMomentary))) : kMinusInf;
}

float LoudnessMeter::shortTerm() const noexcept
{
    return stepsDone_ >= blocksShortTerm ? static_cast<float>(lufs(meanOfLast(blocksShortTerm))) : kMinusInf;
}

float LoudnessMeter::integrated() const noexcept
{
    // Absolute gate (-70 LUFS): every block in the histogram. Relative gate: 10 LU below their
    // mean loudness.
    double energy = 0.0;
    long long count = 0;
    for (int b = 0; b < histogramBins; ++b)
    {
        energy += binEnergy_[static_cast<size_t>(b)];
        count += binCount_[static_cast<size_t>(b)];
    }
    if (count == 0)
        return kMinusInf;
    const double gate = lufs(energy / static_cast<double>(count)) - 10.0;
    energy = 0.0;
    count = 0;
    for (int b = 0; b < histogramBins; ++b)
        if (histogramFloor + (b + 0.5) * histogramStep > gate)
        {
            energy += binEnergy_[static_cast<size_t>(b)];
            count += binCount_[static_cast<size_t>(b)];
        }
    return count > 0 ? static_cast<float>(lufs(energy / static_cast<double>(count))) : kMinusInf;
}

// ---- TruePeakDetector ----------------------------------------------------------------------------

namespace
{
double besselI0(double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/// The interpolation filter, phase by phase: a Kaiser-windowed sinc cutting at the input's
/// Nyquist frequency, each phase normalised to unity gain at DC.
struct Phases
{
    std::array<std::array<float, TruePeakDetector::tapsPerPhase>, TruePeakDetector::factor> taps{};

    Phases()
    {
        constexpr int f = TruePeakDetector::factor, n = TruePeakDetector::tapsPerPhase, length = f * n;
        constexpr double beta = 8.0;
        const double centre = (length - 1) / 2.0;
        for (int p = 0; p < f; ++p)
        {
            double sum = 0.0;
            std::array<double, n> h{};
            for (int j = 0; j < n; ++j)
            {
                const int k = p + j * f;
                const double t = (k - centre) / f;
                const double sinc = std::abs(t) < 1e-12 ? 1.0 : std::sin(pi * t) / (pi * t);
                const double w = (k - centre) / centre;
                h[static_cast<size_t>(j)] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - w * w))) / besselI0(beta);
                sum += h[static_cast<size_t>(j)];
            }
            for (int j = 0; j < n; ++j)
                taps[static_cast<size_t>(p)][static_cast<size_t>(j)] = static_cast<float>(h[static_cast<size_t>(j)] / sum);
        }
    }
};

const Phases& phases()
{
    static const Phases p;
    return p;
}
} // namespace

void TruePeakDetector::prepare(int numChannels)
{
    (void)phases(); // the table, before the audio thread needs it
    history_.assign(static_cast<size_t>(std::clamp(numChannels, 1, maxChannels)), {});
    pos_.assign(history_.size(), 0);
    reset();
}

void TruePeakDetector::reset() noexcept
{
    for (auto& h : history_)
        h.fill(0.0f);
    std::fill(pos_.begin(), pos_.end(), 0);
    peak_ = 0.0f;
}

float TruePeakDetector::processSample(int channel, float x) noexcept
{
    auto& h = history_[static_cast<size_t>(channel)];
    auto& pos = pos_[static_cast<size_t>(channel)];
    pos = pos == 0 ? tapsPerPhase - 1 : pos - 1;
    h[static_cast<size_t>(pos)] = x; // h[pos + j] is x[n - j]
    const auto& table = phases().taps;
    float largest = 0.0f;
    for (const auto& taps : table)
    {
        float y = 0.0f;
        for (int j = 0; j < tapsPerPhase; ++j)
            y += taps[static_cast<size_t>(j)] * h[static_cast<size_t>((pos + j) % tapsPerPhase)];
        largest = std::max(largest, std::abs(y));
    }
    return largest;
}

void TruePeakDetector::process(const float* const* channels, int numChannels, int numSamples) noexcept
{
    const int n = std::min(numChannels, static_cast<int>(history_.size()));
    for (int c = 0; c < n; ++c)
        for (int i = 0; i < numSamples; ++i)
            peak_ = std::max(peak_, processSample(c, channels[c][i]));
}
} // namespace fd::dsp
