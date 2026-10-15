#include "fd/dsp/PitchClasses.h"

#include "fd/dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace fd::dsp
{
namespace
{
constexpr double kFrameSeconds = 0.3; // long enough to tell a bass's semitones apart
constexpr double kLowHz = 50.0, kHighHz = 5000.0;
constexpr double kPeakFloor = 1e-4; // −80 dBFS: quieter peaks are noise
constexpr double kPeakRange = 0.01; // −40 dB under the frame's loudest: the rest is leakage
constexpr int kPerSemitone = PitchClassCounter::kBins / 12;

int orderFor(double sampleRate)
{
    int order = 8;
    while ((1 << order) < sampleRate * kFrameSeconds && order < 16)
        ++order;
    return order;
}
} // namespace

void PitchClassCounter::add(std::span<const float> mono, double sampleRate)
{
    if (mono.empty() || !(sampleRate > 0.0))
        return;
    Fft fft(orderFor(sampleRate));
    const int n = fft.size(), hop = n / 2, bins = n / 2 + 1;
    const double binHz = sampleRate / n;
    const int low = std::max(1, static_cast<int>(std::ceil(kLowHz / binHz)));
    const int high = std::min(bins - 2, static_cast<int>(std::floor(kHighHz / binHz)));
    if (high <= low)
        return;
    std::vector<float> block(static_cast<size_t>(n)), mags(static_cast<size_t>(bins));
    std::vector<std::complex<float>> scratch;
    const auto size = static_cast<long>(mono.size());
    // A stretch shorter than a frame is one frame, padded with silence.
    for (long start = 0; start == 0 || start + n <= size; start += hop)
    {
        for (int i = 0; i < n; ++i)
            block[static_cast<size_t>(i)] = start + i < size ? mono[static_cast<size_t>(start + i)] : 0.0f;
        fft.magnitudes(block.data(), mags.data(), scratch);
        const float loudest = *std::max_element(mags.begin() + low, mags.begin() + high + 1);
        const double floor = std::max(kPeakFloor, kPeakRange * loudest);
        for (int b = low; b <= high; ++b)
        {
            const double m = mags[static_cast<size_t>(b)];
            if (m < floor || !(m > mags[static_cast<size_t>(b - 1)]) || m < mags[static_cast<size_t>(b + 1)])
                continue;
            // The peak's frequency between bins, from the parabola through its log magnitudes.
            const double a = std::log(mags[static_cast<size_t>(b - 1)] + 1e-12), c0 = std::log(m),
                         c = std::log(mags[static_cast<size_t>(b + 1)] + 1e-12);
            const double curve = a - 2.0 * c0 + c;
            const double delta = curve < 0.0 ? std::clamp(0.5 * (a - c) / curve, -0.5, 0.5) : 0.0;
            const double hz = (b + delta) * binHz;
            const double level = std::exp(c0 - 0.25 * (a - c) * delta);
            const double pitch = 69.0 + 12.0 * std::log2(hz / 440.0);
            const double inOctave = std::fmod(pitch, 12.0);
            const int bin = static_cast<int>(std::floor(inOctave * kPerSemitone + 0.5)) % kBins;
            bins_[static_cast<size_t>(bin)] += level;
        }
        if (start + n >= size)
            break;
    }
}

PitchClasses PitchClassCounter::result() const
{
    PitchClasses out;
    double total = 0.0;
    for (const double b : bins_)
        total += b;
    if (!(total > 0.0))
        return out;
    const auto at = [this](int i) { return bins_[static_cast<size_t>(((i % kBins) + kBins) % kBins)]; };
    // Within ±15 cents of each semitone, `t` tenths of one from A = 440 Hz's; `centre` weighs the
    // tenths either side half.
    const auto near = [&](int t, bool centre)
    {
        double sum = 0.0;
        for (int pc = 0; pc < 12; ++pc)
            sum += at(pc * kPerSemitone + t) +
                   (centre ? 0.5 : 1.0) * (at(pc * kPerSemitone + t - 1) + at(pc * kPerSemitone + t + 1));
        return sum;
    };
    // The tuning: where in each semitone the energy gathers, the same for all twelve.
    int tuning = 0;
    double best = -1.0;
    for (int t = -4; t <= 5; ++t)
        if (const double sum = near(t, true);
            sum > best + 1e-12 * total ||
            (std::abs(sum - best) <= 1e-12 * total && std::abs(t) < std::abs(tuning)))
        {
            best = sum;
            tuning = t;
        }
    out.tuningCents = tuning * 100.0 / kPerSemitone;
    out.tonal = std::clamp(near(tuning, false) / total, 0.0, 1.0);
    for (int pc = 0; pc < 12; ++pc)
    {
        double sum = 0.0;
        for (int d = -kPerSemitone / 2; d < kPerSemitone / 2; ++d)
            sum += at(pc * kPerSemitone + tuning + d);
        out.weights[static_cast<size_t>(pc)] = sum / total;
    }
    return out;
}

PitchClasses pitchClasses(std::span<const float> mono, double sampleRate)
{
    PitchClassCounter counter;
    counter.add(mono, sampleRate);
    return counter.result();
}
} // namespace fd::dsp
