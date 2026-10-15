#include "fd/dsp/PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fd::dsp
{
namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Rounds `n` up to a whole number of hops.
int roundUpToHop(int n)
{
    return (n + PitchDetector::hop - 1) / PitchDetector::hop * PitchDetector::hop;
}

/// The FFT order for a window of `w`: at least twice its length, zero-padded.
int fftIndexFor(int w)
{
    return w <= 512 ? 0 : w <= 1024 ? 1 : 2;
}
} // namespace

void PitchDetector::prepare(double sampleRate)
{
    // As fast as the longest window still holds two periods of the lowest note: 25.6 kHz at most.
    factor_ = std::max(1, static_cast<int>(std::ceil(sampleRate / 25600.0)));
    rate_ = sampleRate / factor_;
    // Two Butterworth sections (24 dB/oct) under the decimated Nyquist.
    const double cutoff = std::min(0.42 * rate_, 0.45 * sampleRate);
    antiAlias_[0].setCoeffs(designBiquad(BiquadType::Lowpass, sampleRate, cutoff, 0.5412));
    antiAlias_[1].setCoeffs(designBiquad(BiquadType::Lowpass, sampleRate, cutoff, 1.3066));
    spectrum_.assign(4096, {});
    nsdf_.assign(kMaxWindow + 8, 0.0f);
    window_.assign(kMaxWindow, 0.0f);
    reset();
}

void PitchDetector::reset() noexcept
{
    for (auto& f : antiAlias_)
        f.reset();
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    write_ = 0;
    phase_ = 0;
    sinceHop_ = 0;
    fresh_ = 0;
    lastHz_ = 0.0;
    reading_ = {};
}

bool PitchDetector::nsdf(int w, int maxLag) noexcept
{
    if (maxLag < 3 || maxLag >= w)
        return false;
    // The newest `w` samples, less their mean.
    const auto start = write_ - w;
    double mean = 0.0;
    for (int j = 0; j < w; ++j)
        mean += sample(start + j);
    mean /= w;
    for (int j = 0; j < w; ++j)
        window_[static_cast<size_t>(j)] = static_cast<float>(sample(start + j) - mean);

    // Autocorrelation through the FFT: zero-padded to twice the window, its power spectrum
    // transformed again (real and symmetric, so a forward transform is the inverse times N).
    const auto& fft = ffts_[static_cast<size_t>(fftIndexFor(w))];
    const int n = fft.size();
    for (int j = 0; j < n; ++j)
        spectrum_[static_cast<size_t>(j)] = {j < w ? window_[static_cast<size_t>(j)] : 0.0f, 0.0f};
    fft.forward(spectrum_.data());
    for (int j = 0; j < n; ++j)
        spectrum_[static_cast<size_t>(j)] = {std::norm(spectrum_[static_cast<size_t>(j)]), 0.0f};
    fft.forward(spectrum_.data());

    // m(τ): the sum of both halves' energy, taken down a sample at each end per lag.
    double m = 0.0;
    for (int j = 0; j < w; ++j)
        m += 2.0 * static_cast<double>(window_[static_cast<size_t>(j)]) * window_[static_cast<size_t>(j)];
    for (int tau = 0; tau <= maxLag + 5 && tau < w; ++tau)
    {
        if (tau > 0)
        {
            const double a = window_[static_cast<size_t>(tau - 1)], b = window_[static_cast<size_t>(w - tau)];
            m -= a * a + b * b;
        }
        const double r = spectrum_[static_cast<size_t>(tau)].real() / n;
        nsdf_[static_cast<size_t>(tau)] = m > 1e-12 ? static_cast<float>(2.0 * r / m) : 0.0f;
    }
    return true;
}

double PitchDetector::pickPeriod(int minLag, int maxLag, float& clarity) const noexcept
{
    // McLeod's peak picking: past the lobe around lag 0, each positive lobe's maximum is a key
    // maximum; the first within 0.9 of the highest is the period.
    std::array<std::pair<int, float>, 64> keys{};
    int count = 0;
    float highest = 0.0f;
    int t = 1;
    while (t <= maxLag && nsdf_[static_cast<size_t>(t)] > 0.0f)
        ++t;
    while (t <= maxLag && count < static_cast<int>(keys.size()))
    {
        while (t <= maxLag && nsdf_[static_cast<size_t>(t)] <= 0.0f)
            ++t;
        int peak = -1;
        float value = 0.0f;
        while (t <= maxLag && nsdf_[static_cast<size_t>(t)] > 0.0f)
        {
            if (nsdf_[static_cast<size_t>(t)] > value)
            {
                value = nsdf_[static_cast<size_t>(t)];
                peak = t;
            }
            ++t;
        }
        // A lobe cut off by the end of the lags can't be judged. Each peak is judged by its
        // parabola's top, not its nearest sample: a high note's few samples a period can miss
        // the top of its first lobe by more than the 0.9 allows.
        if (peak >= std::max(1, minLag) && peak < maxLag)
        {
            const auto top = parabola(peak);
            keys[static_cast<size_t>(count++)] = {peak, top.second};
            highest = std::max(highest, top.second);
        }
    }
    for (int i = 0; i < count; ++i)
    {
        const auto [lag, value] = keys[static_cast<size_t>(i)];
        if (value < 0.9f * highest)
            continue;
        const auto top = parabola(lag);
        clarity = std::min(1.0f, top.second);
        return top.first;
    }
    clarity = 0.0f;
    return 0.0;
}

double PitchDetector::peakOf(int lag, int limit) const noexcept
{
    // The NSDF is band-limited (the decimated sound is), so between its samples it's what a
    // Lanczos kernel (a = 4) says; the peak's top is found by a golden-section search within a
    // sample either side. A parabola's guess is off by up to a fifth of a cent on sharp peaks
    // (a saw's).
    const auto at = [&](double x)
    {
        // The weights normalised: a short kernel's don't quite sum to one, and near a flat top
        // that ripple would outweigh the peak's own curve.
        const auto sinc = [](double v)
        { return std::abs(v) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * v) / (std::numbers::pi * v); };
        const int base = static_cast<int>(std::floor(x));
        double sum = 0.0, weights = 0.0;
        for (int i = base - 3; i <= base + 4; ++i)
        {
            if (i < 0 || i > limit)
                continue;
            const double d = x - i;
            const double weight = sinc(d) * (std::abs(d) < 4.0 ? sinc(d / 4.0) : 0.0);
            sum += nsdf_[static_cast<size_t>(i)] * weight;
            weights += weight;
        }
        return weights > 1e-12 ? sum / weights : 0.0;
    };
    if (lag < 4 || lag + 4 > limit)
        return parabola(lag).first;
    constexpr double golden = 0.6180339887498949;
    double lo = lag - 1.0, hi = lag + 1.0;
    double x1 = hi - golden * (hi - lo), x2 = lo + golden * (hi - lo);
    double f1 = at(x1), f2 = at(x2);
    for (int i = 0; i < 40; ++i)
    {
        if (f1 < f2)
        {
            lo = x1;
            x1 = x2;
            f1 = f2;
            x2 = lo + golden * (hi - lo);
            f2 = at(x2);
        }
        else
        {
            hi = x2;
            x2 = x1;
            f2 = f1;
            x1 = hi - golden * (hi - lo);
            f1 = at(x1);
        }
    }
    return 0.5 * (lo + hi);
}

std::pair<double, float> PitchDetector::parabola(int lag) const noexcept
{
    const double a = nsdf_[static_cast<size_t>(lag - 1)], b = nsdf_[static_cast<size_t>(lag)],
                 c = nsdf_[static_cast<size_t>(lag + 1)];
    const double denominator = a - 2.0 * b + c;
    const double delta =
        std::abs(denominator) > 1e-12 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0.0;
    return {lag + delta, static_cast<float>(b - 0.25 * (a - c) * delta)};
}

std::complex<double> PitchDetector::partial(double hz, int w, std::int64_t start) const noexcept
{
    const double omega = kTwoPi * hz / rate_;
    const std::complex<double> step(std::cos(-omega), std::sin(-omega));
    std::complex<double> sum, turn(1.0, 0.0);
    for (int j = 0; j < w; ++j)
    {
        const double hann = 0.5 - 0.5 * std::cos(kTwoPi * j / w);
        sum += hann * static_cast<double>(sample(start + j)) * turn;
        turn *= step;
    }
    return sum;
}

int PitchDetector::strongestHarmonic(double hz, int w) const noexcept
{
    // Read where the window holds six of its periods or more, so its own mirror image doesn't
    // pull its phase; a tone can lack its fundamental (a voice through a phone, some basses), so
    // the loudest of four harmonics from there, if it's a real part of the sound (a fifth of its
    // loudest of eight). None (0): a low sine, whose NSDF reading is already exact.
    const int lowest = std::max(1, static_cast<int>(std::ceil(6.0 * rate_ / (hz * w))));
    int best = 0;
    double loudest = 0.0, reference = 0.0;
    for (int k = 1; k <= 8 && hz * k < 0.4 * rate_; ++k)
    {
        const double m = std::abs(partial(hz * k, w, write_ - w));
        reference = std::max(reference, m);
        if (k >= lowest && k < lowest + 4 && m > loudest)
        {
            loudest = m;
            best = k;
        }
    }
    return best > 0 && loudest >= 0.2 * reference && loudest > 1e-9 ? best : 0;
}

double PitchDetector::refine(double hz, int w, int apart) const noexcept
{
    // The phase in the newest window and in one `apart` samples earlier, each Hann-windowed; how
    // far it turned beyond what `hz` turns is how far `hz` is off.
    const double omega = kTwoPi * hz / rate_;
    const double earlier = std::arg(partial(hz, w, write_ - w - apart)),
                 later = std::arg(partial(hz, w, write_ - w));
    double drift = later - earlier - omega * apart;
    drift -= kTwoPi * std::round(drift / kTwoPi);
    return hz + drift * rate_ / (kTwoPi * apart);
}

void PitchDetector::analyse() noexcept
{
    // The last hop's level: the gate, and how long the sound has been there.
    double energy = 0.0;
    for (int j = 1; j <= hop; ++j)
    {
        const double s = sample(write_ - j);
        energy += s * s;
    }
    Reading r;
    r.levelDb = static_cast<float>(10.0 * std::log10(energy / hop + 1e-20));
    if (r.levelDb < gateDb)
    {
        fresh_ = 0;
        lastHz_ = 0.0;
        reading_ = r;
        return;
    }
    fresh_ = std::min(fresh_ + hop, kRing);

    // The period from the longest window the sound allows (up to 2048), so a low note is never
    // mistaken for a higher one...
    const int longest = std::min(kMaxWindow, fresh_ / hop * hop);
    const int minLag = std::max(2, static_cast<int>(std::floor(rate_ / maxHz)));
    const int maxLag = std::min(longest / 2, static_cast<int>(rate_ / minHz));
    float clarity = 0.0f;
    double period =
        longest >= kMinWindow && nsdf(longest, maxLag) ? pickPeriod(minLag, maxLag, clarity) : 0.0;
    if (period <= 0.0 || clarity < clarityGate)
    {
        lastHz_ = 0.0;
        r.clarity = clarity;
        reading_ = r;
        return;
    }
    // ...and from about four of its periods, so it follows the pitch closely (vibrato, slides).
    int w = std::clamp(roundUpToHop(static_cast<int>(std::ceil(4.0 * period))), kMinWindow, longest);
    if (w < longest && nsdf(w, std::min(w / 2, static_cast<int>(period * 1.07) + 1)))
    {
        const int from = std::max(minLag, static_cast<int>(period * 0.94));
        const int to = std::min(w / 2 - 1, static_cast<int>(std::ceil(period * 1.06)));
        int best = -1;
        for (int t = from; t <= to; ++t)
            if (best < 0 || nsdf_[static_cast<size_t>(t)] > nsdf_[static_cast<size_t>(best)])
                best = t;
        if (best > minLag && best < to && nsdf_[static_cast<size_t>(best)] > 0.5f)
            period = peakOf(best, std::min(w / 2, static_cast<int>(period * 1.07) + 1));
        else
        {
            // Back to the long window's own, its peak found closely.
            (void)nsdf(longest, maxLag);
            period = peakOf(static_cast<int>(std::lround(period)), maxLag);
        }
    }
    else
        period = peakOf(static_cast<int>(std::lround(period)), maxLag);
    double hz = rate_ / period;

    // Steady (within 10 cents of the last reading), and with its harmonics apart in a window
    // (six periods or more: from about 70 Hz): the phase of its loudest harmonic refines it, in
    // three steps each four times further apart, each well inside what the last one left. Lower,
    // the NSDF's peak, found by band-limited interpolation, is as close as it gets.
    const bool steady = lastHz_ > 0.0 && std::abs(1200.0 * std::log2(hz / lastHz_)) < 10.0;
    const int wr = std::min(
        std::clamp(roundUpToHop(static_cast<int>(std::ceil(8.0 * rate_ / hz))), kMinWindow, kMaxWindow),
        fresh_ / 2 / hop * hop);
    const bool resolved = wr >= kMinWindow && wr * hz / rate_ >= 6.0;
    const int k = steady && resolved ? strongestHarmonic(hz, wr) : 0;
    if (k > 0)
    {
        double f = hz * k;
        bool ok = true;
        for (const int apart : {std::max(8, wr / 16), std::max(16, wr / 4), wr})
        {
            const double next = refine(f, wr, apart);
            // The step can't be trusted beyond half of what it can tell apart.
            ok = ok && std::abs(next - f) < 0.25 * rate_ / apart;
            f = next;
        }
        if (ok && std::abs(1200.0 * std::log2(f / k / hz)) < 25.0)
        {
            hz = f / k;
            r.refined = true;
        }
    }
    lastHz_ = hz;
    r.hz = hz;
    r.clarity = clarity;
    r.valid = true;
    reading_ = r;
}
} // namespace fd::dsp
