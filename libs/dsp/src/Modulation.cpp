#include "fd/dsp/Modulation.h"

#include "fd/dsp/Math.h"
#include "fd/dsp/Waveshapers.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
namespace
{
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-15 * sum)
            break;
    }
    return sum;
}

/// The delay kernel: rows for phases 0..phases (the last is the next sample's first), each with
/// the coefficients and the step to the next row, so a read interpolates between phases with
/// one pass.
struct DelayKernel
{
    static constexpr int taps = ModulatedDelay::taps;
    static constexpr int phases = ModulatedDelay::phases;
    static constexpr double beta = 10.0; // ~100 dB: deviations below 1e-5 in the pass band

    std::vector<float> h, step; // (phases + 1) * taps

    DelayKernel() : h(static_cast<size_t>((phases + 1) * taps)), step(h.size())
    {
        const double half = taps / 2;
        const double i0 = besselI0(beta);
        std::vector<double> row(taps);
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = static_cast<double>(p) / phases;
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                // Tap k reads the sample (half - 1 - k) + frac later than the read position's floor.
                const double d = static_cast<double>(k - (taps / 2 - 1)) - frac;
                const double x = d / half;
                const double window = std::abs(x) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - x * x)) / i0;
                const double sinc = std::abs(d) < 1e-12 ? 1.0 : std::sin(pi * d) / (pi * d);
                row[static_cast<size_t>(k)] = sinc * window;
                sum += row[static_cast<size_t>(k)];
            }
            for (int k = 0; k < taps; ++k)
                h[static_cast<size_t>(p * taps + k)] = static_cast<float>(row[static_cast<size_t>(k)] / sum);
        }
        // Whole samples are exact: the first and last rows are a single 1.
        for (const int p : {0, phases})
            for (int k = 0; k < taps; ++k)
                h[static_cast<size_t>(p * taps + k)] = 0.0f;
        h[static_cast<size_t>(taps / 2 - 1)] = 1.0f;
        h[static_cast<size_t>(phases * taps + taps / 2)] = 1.0f;
        for (int p = 0; p < phases; ++p)
            for (int k = 0; k < taps; ++k)
                step[static_cast<size_t>(p * taps + k)] =
                    h[static_cast<size_t>((p + 1) * taps + k)] - h[static_cast<size_t>(p * taps + k)];
    }

    static const DelayKernel& instance()
    {
        static const DelayKernel kernel;
        return kernel;
    }
};

/// A value from -1 to 1 for each cycle: the same cycle, the same value.
float cycleValue(std::int64_t cycle) noexcept
{
    auto x = static_cast<std::uint64_t>(cycle) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(static_cast<double>(x >> 11) / static_cast<double>(1ull << 52) - 1.0);
}
} // namespace

// ---- the delay line ------------------------------------------------------------------------

void ModulatedDelay::prepare(double maxDelaySamples)
{
    maxDelay_ = std::max(static_cast<double>(minimumDelay), maxDelaySamples);
    int size = 1;
    while (size < static_cast<int>(std::ceil(maxDelay_)) + taps + 2)
        size <<= 1;
    buffer_.assign(static_cast<size_t>(size), 0.0f);
    mask_ = size - 1;
    write_ = 0;
    (void)DelayKernel::instance(); // built here, never on the audio thread
}

void ModulatedDelay::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    write_ = 0;
}

float ModulatedDelay::read(double delay) const noexcept
{
    const auto& kernel = DelayKernel::instance();
    delay = std::clamp(delay, static_cast<double>(minimumDelay), maxDelay_);
    // The read position, `delay` before the newest sample; taps run from its floor back half the
    // kernel and forward half.
    const double whole = std::floor(delay);
    const double frac = delay - whole; // how far before sample (write - whole)
    const double ph = (1.0 - frac) * phases;
    int p = static_cast<int>(ph);
    int base = write_ - static_cast<int>(whole) - 1; // the sample just before the position
    if (frac == 0.0)
    {
        p = 0;
        base += 1;
    }
    const float mix = frac == 0.0 ? 0.0f : static_cast<float>(ph - p);
    const float* h = kernel.h.data() + static_cast<size_t>(p) * taps;
    const float* s = kernel.step.data() + static_cast<size_t>(p) * taps;
    float sum = 0.0f;
    int index = base - (taps / 2 - 1);
    for (int k = 0; k < taps; ++k, ++index)
        sum += buffer_[static_cast<size_t>(index & mask_)] * (h[k] + mix * s[k]);
    return sum;
}

// ---- the feedback path ---------------------------------------------------------------------

void FeedbackPath::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    dc_.prepare(sampleRate_);
    reset();
}

void FeedbackPath::reset() noexcept
{
    lowCut_.reset();
    highCut_.reset();
    dc_.reset();
}

void FeedbackPath::set(double lowCutHz, double highCutHz, double saturation) noexcept
{
    lowOn_ = lowCutHz > 0.0;
    highOn_ = highCutHz > 0.0 && highCutHz < 0.45 * sampleRate_;
    if (lowOn_)
        lowCut_.setCoeffs(designBiquad(BiquadType::Highpass, sampleRate_, lowCutHz, 1.0 / std::sqrt(2.0)));
    if (highOn_)
        highCut_.setCoeffs(designBiquad(BiquadType::Lowpass, sampleRate_, highCutHz, 1.0 / std::sqrt(2.0)));
    drive_ = static_cast<float>(std::clamp(saturation, 0.0, 1.0));
}

float FeedbackPath::process(float x) noexcept
{
    if (lowOn_)
        x = lowCut_.process(x);
    if (highOn_)
        x = highCut_.process(x);
    if (drive_ > 0.0f)
    {
        // Soft clip, unity for small signals: tanh(g x) / g, harder as the drive rises; the
        // ceiling (1 / g) stays above full scale so the loop keeps its level, never runs away.
        const float g = 1.0f + 1.0f * drive_; // 1..2: tanh's knee from 0 dBFS down to -6 dBFS
        x = static_cast<float>(shape(ShaperCurve::Soft, static_cast<double>(g * x))) / g;
    }
    else
        x = std::clamp(x, -4.0f, 4.0f); // clean, but bounded (+12 dBFS)
    return dc_.process(x);
}

double FeedbackPath::magnitude(double freqHz, double lowCutHz, double highCutHz, double sampleRate) noexcept
{
    double m = 1.0;
    if (lowCutHz > 0.0)
        m *= biquadMagnitude(designBiquad(BiquadType::Highpass, sampleRate, lowCutHz, 1.0 / std::sqrt(2.0)),
                             freqHz, sampleRate);
    if (highCutHz > 0.0 && highCutHz < 0.45 * sampleRate)
        m *= biquadMagnitude(designBiquad(BiquadType::Lowpass, sampleRate, highCutHz, 1.0 / std::sqrt(2.0)),
                             freqHz, sampleRate);
    return m;
}

// ---- the LFO -------------------------------------------------------------------------------

void ModulationLfo::reset(double phase) noexcept
{
    const double f = std::floor(phase);
    cycle_ = static_cast<std::int64_t>(f);
    phase_ = phase - f;
}

void ModulationLfo::setRate(double hz) noexcept
{
    increment_ = std::max(0.0, hz) / sampleRate_;
}

void ModulationLfo::sync(double ppq, double tempoBpm, double beats, bool playing) noexcept
{
    beats = std::max(1e-6, beats);
    increment_ = std::max(0.0, tempoBpm) / 60.0 / beats / sampleRate_;
    if (playing)
        reset(ppq / beats + offset_);
}

ModulationLfo::Stereo ModulationLfo::next() noexcept
{
    const double at = position();
    Stereo out{value(shape_, at), value(shape_, at + stereo_)};
    phase_ += increment_;
    if (phase_ >= 1.0)
    {
        const double f = std::floor(phase_);
        cycle_ += static_cast<std::int64_t>(f);
        phase_ -= f;
    }
    return out;
}

float ModulationLfo::value(Shape shape, double position) noexcept
{
    const double f = std::floor(position);
    const auto cycle = static_cast<std::int64_t>(f);
    const double p = position - f;
    switch (shape)
    {
    case Shape::Sine:
        return static_cast<float>(std::sin(2.0 * pi * p));
    case Shape::Triangle:
        return static_cast<float>(p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    case Shape::SawUp:
        return static_cast<float>(2.0 * p - 1.0);
    case Shape::SawDown:
        return static_cast<float>(1.0 - 2.0 * p);
    case Shape::Square:
        return p < 0.5 ? 1.0f : -1.0f;
    case Shape::SampleAndHold:
        return cycleValue(cycle);
    case Shape::Wander:
    {
        // From this cycle's value to the next's, eased so it never steps.
        const double t = p * p * (3.0 - 2.0 * p);
        return static_cast<float>(cycleValue(cycle) + t * (cycleValue(cycle + 1) - cycleValue(cycle)));
    }
    }
    return 0.0f;
}
} // namespace fd::dsp
