#include "fd/dsp/Wavetable.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace fd::dsp
{
// ---- the real FFT ----------------------------------------------------------------------------

void RealFft::prepare(int order)
{
    order = std::clamp(order, 2, 16);
    size_ = 1 << order;
    half_.prepare(order - 1);
    twiddles_.resize(static_cast<size_t>(size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
        twiddles_[static_cast<size_t>(k)] = std::polar(1.0f, static_cast<float>(-twoPi * k / size_));
    work_.assign(static_cast<size_t>(size_ / 2), {});
}

void RealFft::forward(const float* in, std::complex<float>* out) noexcept
{
    // Even samples real, odd imaginary, through the half-size transform; then split.
    const int m = size_ / 2;
    for (int i = 0; i < m; ++i)
        work_[static_cast<size_t>(i)] = {in[2 * i], in[2 * i + 1]};
    half_.forward(work_.data());
    const auto z0 = work_[0];
    out[0] = {z0.real() + z0.imag(), 0.0f};
    out[m] = {z0.real() - z0.imag(), 0.0f};
    for (int k = 1; k < m; ++k)
    {
        const auto zk = work_[static_cast<size_t>(k)];
        const auto zm = std::conj(work_[static_cast<size_t>(m - k)]);
        const std::complex<float> even = 0.5f * (zk + zm);
        const auto d = zk - zm;
        const std::complex<float> odd{0.5f * d.imag(), -0.5f * d.real()}; // (zk - zm) / 2i
        const auto w = twiddles_[static_cast<size_t>(k)];
        out[k] = {even.real() + w.real() * odd.real() - w.imag() * odd.imag(),
                  even.imag() + w.real() * odd.imag() + w.imag() * odd.real()};
    }
}

void RealFft::inverse(const std::complex<float>* in, float* out) noexcept
{
    // Back to the half-size spectrum of even + i odd, inverse-transformed through the forward
    // one (conjugated), scaled by 1 / size.
    const int m = size_ / 2;
    for (int k = 0; k < m; ++k)
    {
        const auto xk = in[k];
        const auto xm = std::conj(in[m - k]);
        const auto even = 0.5f * (xk + xm);
        const auto diff = 0.5f * (xk - xm);
        const auto w = std::conj(twiddles_[static_cast<size_t>(k)]);
        const std::complex<float> odd{diff.real() * w.real() - diff.imag() * w.imag(),
                                      diff.real() * w.imag() + diff.imag() * w.real()};
        // Z = E + i O, conjugated for the inverse through the forward transform.
        work_[static_cast<size_t>(k)] =
            std::conj(std::complex<float>{even.real() - odd.imag(), even.imag() + odd.real()});
    }
    half_.forward(work_.data());
    const float scale = 1.0f / static_cast<float>(m); // the half-size inverse's 1/m
    for (int i = 0; i < m; ++i)
    {
        const auto z = std::conj(work_[static_cast<size_t>(i)]);
        out[2 * i] = z.real() * scale;
        out[2 * i + 1] = z.imag() * scale;
    }
}

// ---- tables ----------------------------------------------------------------------------------

namespace
{
bool isPowerOfTwo(int n) noexcept
{
    return n > 0 && (n & (n - 1)) == 0;
}

int orderOf(int n) noexcept
{
    int order = 0;
    while ((1 << order) < n)
        ++order;
    return order;
}

/// Catmull-Rom between x1 and x2 at t.
inline float cubic(float x0, float x1, float x2, float x3, float t) noexcept
{
    return x1 +
           0.5f * t *
               (x2 - x0 + t * (2.0f * x0 - 5.0f * x1 + 4.0f * x2 - x3 + t * (3.0f * (x1 - x2) + x3 - x0)));
}

/// One periodic cycle of a power-of-two size, read at `position` samples (0 or more).
inline float readCycle(const float* in, int size, double position) noexcept
{
    const int mask = size - 1;
    const auto i = static_cast<int>(position);
    const auto t = static_cast<float>(position - i);
    return cubic(in[(i - 1) & mask], in[i & mask], in[(i + 1) & mask], in[(i + 2) & mask], t);
}

double frac(double x) noexcept
{
    return x - std::floor(x);
}

/// frac() of a value 0 or more.
double fracOf(double x) noexcept
{
    return x - static_cast<double>(static_cast<std::int64_t>(x));
}

/// x to the power e, for x in 0..1 (a bent phase), in single precision (a few times faster
/// than double, and finer than a 4 096-sample cycle needs).
double powUnit(double x, double e) noexcept
{
    return x <= 0.0
               ? 0.0
               : static_cast<double>(std::exp2f(static_cast<float>(e) * std::log2f(static_cast<float>(x))));
}
} // namespace

Wavetable Wavetable::fromCycles(const float* samples, int frameLength, int frames, bool normalise)
{
    Wavetable t;
    frameLength = std::max(2, frameLength);
    t.frames_ = std::clamp(frames, 1, maxFrames);
    t.bins_.assign(static_cast<size_t>(t.frames_) * (maxHarmonic + 1), {});
    // Up to the cycle's Nyquist, which itself is left out: a table never plays it.
    const int harmonics = std::min(maxHarmonic, (frameLength - 1) / 2);
    const bool fast = isPowerOfTwo(frameLength) && frameLength >= 4;
    RealFft fft(fast ? orderOf(frameLength) : 2);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(frameLength / 2 + 1));
    for (int f = 0; f < t.frames_; ++f)
    {
        const float* x = samples + static_cast<size_t>(f) * static_cast<size_t>(frameLength);
        auto* c = t.bins_.data() + static_cast<size_t>(f) * (maxHarmonic + 1);
        if (fast)
            fft.forward(x, spectrum.data());
        for (int h = 1; h <= harmonics; ++h)
        {
            std::complex<double> sum;
            if (fast)
                sum = {spectrum[static_cast<size_t>(h)].real(), spectrum[static_cast<size_t>(h)].imag()};
            else
                for (int n = 0; n < frameLength; ++n)
                    sum += static_cast<double>(x[n]) * std::polar(1.0, -twoPi * h * n / frameLength);
            const double scale = 1.0 / frameLength;
            c[h] = {static_cast<float>(sum.real() * scale), static_cast<float>(sum.imag() * scale)};
        }
    }
    t.finish(normalise);
    return t;
}

Wavetable Wavetable::fromSpectra(std::vector<std::complex<float>> bins, int frames, bool normalise)
{
    Wavetable t;
    t.frames_ = std::clamp(frames, 1, maxFrames);
    bins.resize(static_cast<size_t>(t.frames_) * (maxHarmonic + 1));
    t.bins_ = std::move(bins);
    t.finish(normalise);
    return t;
}

void Wavetable::finish(bool normalise)
{
    samples_.assign(static_cast<size_t>(frames_) * frameSize, 0.0f);
    RealFft fft(orderOf(frameSize));
    std::vector<std::complex<float>> spectrum(frameSize / 2 + 1);
    float peak = 0.0f, loudest = 0.0f;
    for (int f = 0; f < frames_; ++f)
    {
        auto* c = bins_.data() + static_cast<size_t>(f) * (maxHarmonic + 1);
        c[0] = {};
        spectrum[0] = {};
        c[maxHarmonic] = {}; // the frame's Nyquist: never played
        for (int h = 1; h <= maxHarmonic; ++h)
            spectrum[static_cast<size_t>(h)] = c[h] * static_cast<float>(frameSize);
        float* x = samples_.data() + static_cast<size_t>(f) * frameSize;
        fft.inverse(spectrum.data(), x);
        for (int n = 0; n < frameSize; ++n)
            peak = std::max(peak, std::abs(x[n]));
        for (int h = 1; h <= maxHarmonic; ++h)
            loudest = std::max(loudest, std::abs(c[h]));
    }
    const float gain = normalise && peak > 1e-9f ? 1.0f / peak : 1.0f;
    if (gain != 1.0f)
    {
        for (auto& c : bins_)
            c *= gain;
        for (auto& s : samples_)
            s *= gain;
        loudest *= gain;
    }
    highest_ = 0;
    for (int f = 0; f < frames_; ++f)
        for (int h = maxHarmonic; h > highest_; --h)
            if (std::abs(this->spectrum(f)[h]) > 1e-6f * loudest)
            {
                highest_ = h;
                break;
            }
}

// ---- warps -----------------------------------------------------------------------------------

float lookup(const CurveTable& curve, double x) noexcept
{
    const double at = std::clamp(x, 0.0, 1.0) * (curve.size() - 1);
    const auto i = std::min(static_cast<size_t>(at), curve.size() - 2);
    const auto t = static_cast<float>(at - static_cast<double>(i));
    return curve[i] + t * (curve[i + 1] - curve[i]);
}

void applyCycleWarp(Warp warp, float amount, const float* in, int size, float* out,
                    const CurveTable* curve) noexcept
{
    const double a = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    const double n = size;
    const auto read = [&](double phase) { return readCycle(in, size, phase * n); };
    switch (warp)
    {
    case Warp::Fold:
    {
        // Louder, folded back at the edges instead of clipped.
        const auto gain = static_cast<float>(1.0 + 7.0 * a);
        for (int i = 0; i < size; ++i)
        {
            const double v = (in[i] * gain + 1.0) * 0.25;
            out[i] = static_cast<float>(1.0 - 4.0 * std::abs(frac(v) - 0.5));
        }
        return;
    }
    case Warp::Mirror:
    case Warp::Flip:
        // The second half reflects the first (Mirror) or turns over (Flip), by the amount.
        for (int i = 0; i < size; ++i)
        {
            const double x = i / n;
            if (x < 0.5)
                out[i] = in[i];
            else if (warp == Warp::Mirror)
                out[i] = static_cast<float>((1.0 - a) * in[i] + a * read(1.0 - x));
            else
                out[i] = static_cast<float>((1.0 - 2.0 * a) * in[i]);
        }
        return;
    default:
        break;
    }
    // The rest read the cycle somewhere else: a map from where the output is to where it reads
    // (and a gain), one loop per warp so each runs straight through.
    const auto remap = [&](auto map)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto [phase, gain] = map(i / n);
            out[i] = gain == 0.0 ? 0.0f : static_cast<float>(gain * read(phase));
        }
    };
    const double ratio = 1.0 + 15.0 * a;     // Sync, Window Sync
    const double bend = std::exp2(-3.0 * a); // Bend +, Bend ±
    switch (warp)
    {
    case Warp::Sync:
        remap([ratio](double x) { return std::pair{fracOf(x * ratio), 1.0}; });
        break;
    case Warp::WindowSync:
        remap([ratio](double x) { return std::pair{fracOf(x * ratio), std::sin(pi * x)}; });
        break;
    case Warp::BendPlus:
        remap([bend](double x) { return std::pair{powUnit(x, bend), 1.0}; });
        break;
    case Warp::BendMinus:
        remap([bend](double x) { return std::pair{powUnit(x, 1.0 / bend), 1.0}; });
        break;
    case Warp::BendBoth:
        remap(
            [bend](double x)
            {
                const double u = 2.0 * x - 1.0;
                return std::pair{0.5 + 0.5 * std::copysign(powUnit(std::abs(u), bend), u), 1.0};
            });
        break;
    case Warp::Squeeze:
        // Each half pinched towards its middle.
        remap(
            [bend](double x)
            {
                const double half = x < 0.5 ? 0.0 : 1.0, u = 2.0 * (2.0 * x - half) - 1.0;
                return std::pair{
                    (half + 0.5 + 0.5 * std::copysign(powUnit(std::abs(u), 1.0 / bend), u)) * 0.5, 1.0};
            });
        break;
    case Warp::Pulse:
    {
        const double pulse = 1.0 / (1.0 - 0.95 * a);
        remap(
            [pulse](double x)
            {
                const double phase = x * pulse;
                return std::pair{phase, phase < 1.0 ? 1.0 : 0.0};
            });
        break;
    }
    case Warp::AsymPlus:
    case Warp::AsymMinus:
    {
        const double pivot = warp == Warp::AsymPlus ? 0.5 - 0.45 * a : 0.5 + 0.45 * a;
        remap(
            [pivot](double x)
            {
                return std::pair{x < pivot ? 0.5 * x / pivot : 0.5 + 0.5 * (x - pivot) / (1.0 - pivot), 1.0};
            });
        break;
    }
    case Warp::Quantize:
    {
        const double steps = n * std::exp2(-a * std::log2(n / 2.0)); // size ... 2 steps
        remap([steps](double x)
              { return std::pair{static_cast<double>(static_cast<int>(x * steps)) / steps, 1.0}; });
        break;
    }
    case Warp::Remap:
        remap([a, curve](double x)
              { return std::pair{x + a * ((curve != nullptr ? lookup(*curve, x) : x) - x), 1.0}; });
        break;
    default:
        std::copy(in, in + size, out);
        break;
    }
}

// ---- spectral warps ---------------------------------------------------------------------------

namespace
{
/// The level of the harmonics at a fractional harmonic `x`, linear between them (0 below the first).
double levelAt(const std::complex<float>* s, int count, double x) noexcept
{
    if (x <= 0.0 || x > static_cast<double>(count))
        return 0.0;
    const auto i = static_cast<int>(x);
    const double t = x - i;
    const double a = i >= 1 ? std::abs(s[i]) : 0.0;
    const double b = i + 1 <= count ? std::abs(s[i + 1]) : 0.0;
    return a + t * (b - a);
}

double powerOf(const std::complex<float>* s, int count) noexcept
{
    double p = 0.0;
    for (int h = 1; h <= count; ++h)
        p += std::norm(s[h]);
    return p;
}

/// Scales harmonics 1 ... count to `power`.
void keepPower(std::complex<float>* s, int count, double power) noexcept
{
    if (const double now = powerOf(s, count); now > 1e-30)
    {
        const auto g = static_cast<float>(std::sqrt(power / now));
        for (int h = 1; h <= count; ++h)
            s[h] *= g;
    }
}

/// `c` at level `level`, keeping its phase (a level from nothing takes phase 0).
std::complex<float> atLevel(std::complex<float> c, double level) noexcept
{
    const float m = std::abs(c);
    return m > 1e-20f ? c * static_cast<float>(level / m)
                      : std::complex<float>(static_cast<float>(level), 0.0f);
}

/// Adds `value` at fractional harmonic `x`, split between the two harmonics either side.
void addAt(std::complex<float>* s, int count, double x, std::complex<float> value) noexcept
{
    if (x < 1.0 || x > static_cast<double>(count))
        return;
    const auto i = static_cast<int>(x);
    const auto t = static_cast<float>(x - i);
    s[i] += value * (1.0f - t);
    if (t > 0.0f && i + 1 <= count)
        s[i + 1] += value * t;
}

/// Moves each harmonic h to `where(h)` (spread between the harmonics it lands on), keeping the power.
template <typename Where>
void moveHarmonics(std::complex<float>* s, int count, std::complex<float>* scratch, Where where) noexcept
{
    const double power = powerOf(s, count);
    std::fill(scratch, scratch + count + 1, std::complex<float>{});
    for (int h = 1; h <= count; ++h)
        addAt(scratch, count, where(h), s[h]);
    std::copy(scratch + 1, scratch + count + 1, s + 1);
    keepPower(s, count, power);
}

double unitHash(std::uint32_t seed, int h) noexcept
{
    std::uint32_t x = seed * 0x9E3779B9u + static_cast<std::uint32_t>(h) * 0x85EBCA6Bu;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return static_cast<double>(x >> 8) / static_cast<double>(1u << 24);
}
} // namespace

void applySpectralWarp(Warp warp, float amount, std::complex<float>* s, int count, std::uint32_t seed,
                       std::complex<float>* scratch) noexcept
{
    const double a = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    if (a <= 0.0 || count < 1)
        return;
    switch (warp)
    {
    case Warp::FormantShift:
    {
        // The levels read lower down: the envelope moves up, each harmonic keeps its phase.
        const double power = powerOf(s, count), scale = std::exp2(2.0 * a);
        std::copy(s, s + count + 1, scratch);
        for (int h = 1; h <= count; ++h)
            s[h] = atLevel(scratch[h], levelAt(scratch, count, h / scale));
        keepPower(s, count, power);
        return;
    }
    case Warp::HarmonicStretch:
        moveHarmonics(s, count, scratch, [a](int h) { return h * (1.0 + a); });
        return;
    case Warp::InharmonicStretch:
        moveHarmonics(s, count, scratch,
                      [a](int h) { return std::pow(static_cast<double>(h), 1.0 + 0.5 * a); });
        return;
    case Warp::Smear:
    {
        // A one-pole blur of the levels, up then down the harmonics.
        const double power = powerOf(s, count), c = 0.95 * a;
        double y = 0.0;
        for (int h = 1; h <= count; ++h)
            scratch[h] = static_cast<float>(y = (1.0 - c) * std::abs(s[h]) + c * y);
        y = 0.0;
        for (int h = count; h >= 1; --h)
        {
            y = (1.0 - c) * scratch[h].real() + c * y;
            s[h] = atLevel(s[h], y);
        }
        keepPower(s, count, power);
        return;
    }
    case Warp::SpectralLowPass:
    {
        const double top = std::pow(static_cast<double>(count), 1.0 - a);
        for (int h = 1; h <= count; ++h)
            s[h] *= static_cast<float>(std::clamp(top + 1.0 - h, 0.0, 1.0));
        return;
    }
    case Warp::SpectralHighPass:
    {
        const double bottom = std::pow(static_cast<double>(count), a);
        for (int h = 1; h <= count; ++h)
            s[h] *= static_cast<float>(std::clamp(h - bottom + 1.0, 0.0, 1.0));
        return;
    }
    case Warp::PhaseDisperse:
        for (int h = 2; h <= count; ++h)
            s[h] *= std::polar(1.0f, static_cast<float>(a * pi * (h - 1.0) * (h - 1.0) / 64.0));
        return;
    case Warp::RandomHarmonics:
        for (int h = 1; h <= count; ++h)
            s[h] *= static_cast<float>(1.0 - a + 2.0 * a * unitHash(seed, h));
        return;
    case Warp::Shepard:
    {
        // Up by `a` of an octave fading out, and from an octave below up by `a` fading in.
        const double power = powerOf(s, count);
        const auto out = static_cast<float>(std::sqrt(1.0 - a)), in = static_cast<float>(std::sqrt(a));
        std::fill(scratch, scratch + count + 1, std::complex<float>{});
        for (int h = 1; h <= count; ++h)
        {
            addAt(scratch, count, h * std::exp2(a), s[h] * out);
            addAt(scratch, count, h * std::exp2(a - 1.0), s[h] * in);
        }
        std::copy(scratch + 1, scratch + count + 1, s + 1);
        keepPower(s, count, power);
        return;
    }
    default:
        return;
    }
}

double frameSkewOffset(int h, int count, float amount) noexcept
{
    const double where =
        count > 1 ? std::log2(static_cast<double>(h)) / std::log2(static_cast<double>(count)) : 0.0;
    return std::clamp(static_cast<double>(amount), 0.0, 1.0) * (where - 0.5);
}

// ---- rendering -------------------------------------------------------------------------------

void CycleRenderer::prepare()
{
    for (int order = 6; order <= 13; ++order)
        ffts_[static_cast<size_t>(order)].prepare(order);
    bins_.assign(2 * maxSize + 1, {});
    spectrum_.assign(Wavetable::maxHarmonic + 1, {});
    scratch_.assign(Wavetable::maxHarmonic + 1, {});
    wide_.assign(2 * maxSize, 0.0f);
    warped_.assign(2 * maxSize, 0.0f);
}

int CycleRenderer::sizeFor(int harmonics) noexcept
{
    int size = 256;
    while (size < 32 * harmonics && size < maxSize)
        size <<= 1;
    return size;
}

int CycleRenderer::render(const Wavetable& table, double position, const std::array<WarpSetting, 2>& warps,
                          int harmonics, float* buffer) noexcept
{
    const int limit = std::clamp(harmonics, 1, Wavetable::maxHarmonic);
    const int n = sizeFor(limit);
    float* out = cycle(buffer);
    const bool warped = isCycleWarp(warps[0].warp) || isCycleWarp(warps[1].warp);
    const bool spectral = isSpectralWarp(warps[0].warp) || isSpectralWarp(warps[1].warp);
    // The warps in the cycle work on a cycle 16 times the harmonics (512 ... 4 096): what they
    // push past its top folds back onto harmonics, slightly changing the brightest, never between
    // them. Its sources go to a quarter of its size, where reads stay exact, since warps move
    // harmonics both ways.
    int wide = 512;
    while (wide < 16 * limit && wide < maxSize)
        wide <<= 1;
    const int top = std::min(limit, n / 2 - 1);
    // How many of the table's harmonics are needed: every one when the spectral warps may move them.
    const int needed =
        spectral ? Wavetable::maxHarmonic : (warped ? std::min(Wavetable::maxHarmonic, wide / 4) : top);

    // The frames around the position, interpolated harmonic by harmonic (under Frame Skew, each
    // harmonic from a position of its own).
    float skew = 0.0f;
    for (const auto& w : warps)
        if (w.warp == Warp::FrameSkew)
            skew += w.amount;
    const auto read = [&table](double where, int h)
    {
        const double at = std::clamp(where, 0.0, 1.0) * (table.frames() - 1);
        const int f0 = static_cast<int>(at), f1 = std::min(f0 + 1, table.frames() - 1);
        const auto t = static_cast<float>(at - f0);
        const auto a = table.spectrum(f0)[h], b = table.spectrum(f1)[h];
        return a + t * (b - a);
    };
    spectrum_[0] = {};
    if (skew > 0.0f && table.frames() > 1)
        for (int h = 1; h <= needed; ++h)
            spectrum_[static_cast<size_t>(h)] =
                read(position + frameSkewOffset(h, Wavetable::maxHarmonic, std::min(skew, 1.0f)), h);
    else
    {
        const double at = std::clamp(position, 0.0, 1.0) * (table.frames() - 1);
        const int f0 = static_cast<int>(at), f1 = std::min(f0 + 1, table.frames() - 1);
        const auto t = static_cast<float>(at - f0);
        const auto* a = table.spectrum(f0);
        const auto* b = table.spectrum(f1);
        for (int h = 1; h <= needed; ++h)
            spectrum_[static_cast<size_t>(h)] = a[h] + t * (b[h] - a[h]);
    }
    // The spectral warps, on the harmonics.
    for (const auto& w : warps)
        if (isSpectralWarp(w.warp))
            applySpectralWarp(w.warp, w.amount, spectrum_.data(), needed, w.seed, scratch_.data());

    if (!warped)
    {
        auto& fft = ffts_[static_cast<size_t>(orderOf(n))];
        const auto scale = static_cast<float>(n);
        bins_[0] = {};
        for (int h = 1; h <= n / 2; ++h)
            bins_[static_cast<size_t>(h)] =
                h <= top ? scale * spectrum_[static_cast<size_t>(h)] : std::complex<float>{};
        fft.inverse(bins_.data(), out);
    }
    else
    {
        auto& wideFft = ffts_[static_cast<size_t>(orderOf(wide))];
        const int sources = std::min(Wavetable::maxHarmonic, wide / 4);
        const auto scale = static_cast<float>(wide);
        bins_[0] = {};
        for (int h = 1; h <= wide / 2; ++h)
            bins_[static_cast<size_t>(h)] =
                h <= sources ? scale * spectrum_[static_cast<size_t>(h)] : std::complex<float>{};
        wideFft.inverse(bins_.data(), wide_.data());
        float* from = wide_.data();
        float* to = warped_.data();
        for (const auto& w : warps)
            if (isCycleWarp(w.warp))
            {
                applyCycleWarp(w.warp, w.amount, from, wide, to, w.curve);
                std::swap(from, to);
            }
        // Back to harmonics, cut to the limit, and down to the cycle's size.
        wideFft.forward(from, bins_.data());
        const float down = static_cast<float>(n) / static_cast<float>(wide);
        bins_[0] = {};
        for (int h = 1; h <= n / 2; ++h)
            bins_[static_cast<size_t>(h)] = h <= std::min(limit, n / 2 - 1)
                                                ? down * bins_[static_cast<size_t>(h)]
                                                : std::complex<float>{};
        ffts_[static_cast<size_t>(orderOf(n))].inverse(bins_.data(), out);
    }
    // Guards for 8-point reads either side.
    for (int g = 1; g <= guard; ++g)
    {
        out[-g] = out[n - g];
        out[n + g - 1] = out[g - 1];
    }
    return n;
}

namespace
{
/// The 8-point sinc's taps: 256 phases (and the next cycle's first), each a Kaiser-windowed sinc
/// (beta 9) normalised to unity gain. Built once, at load.
struct SincTable
{
    static constexpr int phases = 256, taps = 8;
    std::array<std::array<float, taps>, phases + 1> taps_{};

    SincTable()
    {
        const auto bessel = [](double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr double beta = 9.0;
        for (int p = 0; p <= phases; ++p)
        {
            const double t = static_cast<double>(p) / phases;
            double total = 0.0;
            std::array<double, taps> h{};
            for (int k = 0; k < taps; ++k)
            {
                const double d = (k - 3) - t; // taps at -3 ... +4 from the sample before the phase
                const double u = d / 4.0;
                const double window =
                    std::abs(u) >= 1.0 ? 0.0 : bessel(beta * std::sqrt(1.0 - u * u)) / bessel(beta);
                const double sinc = std::abs(d) < 1e-12 ? 1.0 : std::sin(pi * d) / (pi * d);
                h[static_cast<size_t>(k)] = sinc * window;
                total += h[static_cast<size_t>(k)];
            }
            for (int k = 0; k < taps; ++k)
                taps_[static_cast<size_t>(p)][static_cast<size_t>(k)] =
                    static_cast<float>(h[static_cast<size_t>(k)] / total);
        }
    }
};
const SincTable kSinc;
} // namespace

float readSinc(const float* c, int size, double phase) noexcept
{
    const double at = phase * size;
    const auto i = static_cast<int>(at);
    const double where = (at - i) * SincTable::phases;
    const auto p = static_cast<int>(where);
    const auto t = static_cast<float>(where - p);
    const auto& a = kSinc.taps_[static_cast<size_t>(p)];
    const auto& b = kSinc.taps_[static_cast<size_t>(p + 1)];
    float sum = 0.0f;
    for (int k = 0; k < SincTable::taps; ++k)
        sum += (a[static_cast<size_t>(k)] + t * (b[static_cast<size_t>(k)] - a[static_cast<size_t>(k)])) *
               c[i - 3 + k];
    return sum;
}
} // namespace fd::dsp
