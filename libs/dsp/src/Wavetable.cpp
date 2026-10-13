#include "fd/dsp/Wavetable.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

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

/// One periodic cycle of a power-of-two size, read at `position` samples (any real value).
inline float readCycle(const float* in, int size, double position) noexcept
{
    const int mask = size - 1;
    const double floor = std::floor(position);
    const auto t = static_cast<float>(position - floor);
    const int i = static_cast<int>(floor);
    return cubic(in[(i - 1) & mask], in[i & mask], in[(i + 1) & mask], in[(i + 2) & mask], t);
}

double frac(double x) noexcept
{
    return x - std::floor(x);
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
    // The rest read the cycle somewhere else: a map from where the output is to where it reads.
    const double ratio = 1.0 + 15.0 * a;     // Sync, Window Sync
    const double bend = std::exp2(-3.0 * a); // Bend +, Bend ±
    const double pivot = warp == Warp::AsymPlus ? 0.5 - 0.45 * a : 0.5 + 0.45 * a;
    const double pulse = 1.0 / (1.0 - 0.95 * a);
    const double steps = n * std::exp2(-a * std::log2(n / 2.0)); // Quantize: size ... 2 steps
    for (int i = 0; i < size; ++i)
    {
        const double x = i / n;
        double phase = x, gain = 1.0;
        switch (warp)
        {
        case Warp::Sync:
            phase = frac(x * ratio);
            break;
        case Warp::WindowSync:
            phase = frac(x * ratio);
            gain = std::sin(pi * x);
            break;
        case Warp::BendPlus:
            phase = std::pow(x, bend);
            break;
        case Warp::BendMinus:
            phase = std::pow(x, 1.0 / bend);
            break;
        case Warp::BendBoth:
        {
            const double u = 2.0 * x - 1.0;
            phase = 0.5 + 0.5 * std::copysign(std::pow(std::abs(u), bend), u);
            break;
        }
        case Warp::Squeeze:
        {
            // Each half pinched towards its middle.
            const double half = std::floor(2.0 * x), u = 2.0 * (2.0 * x - half) - 1.0;
            phase = (half + 0.5 + 0.5 * std::copysign(std::pow(std::abs(u), 1.0 / bend), u)) * 0.5;
            break;
        }
        case Warp::Pulse:
            phase = x * pulse;
            gain = phase < 1.0 ? 1.0 : 0.0;
            break;
        case Warp::AsymPlus:
        case Warp::AsymMinus:
            phase = x < pivot ? 0.5 * x / pivot : 0.5 + 0.5 * (x - pivot) / (1.0 - pivot);
            break;
        case Warp::Quantize:
            phase = std::floor(x * steps) / steps;
            break;
        case Warp::Remap:
            phase = x + a * ((curve != nullptr ? lookup(*curve, x) : x) - x);
            break;
        default:
            break;
        }
        out[i] = gain == 0.0 ? 0.0f : static_cast<float>(gain * read(phase));
    }
}

// ---- rendering -------------------------------------------------------------------------------

void CycleRenderer::prepare()
{
    for (int order = 6; order <= 13; ++order)
        ffts_[static_cast<size_t>(order)].prepare(order);
    bins_.assign(2 * maxSize + 1, {});
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

    // The two frames around the position, interpolated harmonic by harmonic.
    const double at = std::clamp(position, 0.0, 1.0) * (table.frames() - 1);
    const int f0 = static_cast<int>(at);
    const int f1 = std::min(f0 + 1, table.frames() - 1);
    const auto t = static_cast<float>(at - f0);
    const auto* a = table.spectrum(f0);
    const auto* b = table.spectrum(f1);
    const bool warped = isCycleWarp(warps[0].warp) || isCycleWarp(warps[1].warp);

    if (!warped)
    {
        auto& fft = ffts_[static_cast<size_t>(orderOf(n))];
        const int top = std::min(limit, n / 2 - 1);
        const auto scale = static_cast<float>(n);
        bins_[0] = {};
        for (int h = 1; h <= n / 2; ++h)
            bins_[static_cast<size_t>(h)] =
                h <= top ? scale * (a[h] + t * (b[h] - a[h])) : std::complex<float>{};
        fft.inverse(bins_.data(), out);
    }
    else
    {
        // Twice the size, so the warps read a cycle well below its Nyquist; its sources up to
        // twice the limit, since warps move harmonics both ways.
        const int wide = 2 * n;
        auto& wideFft = ffts_[static_cast<size_t>(orderOf(wide))];
        const int sources = std::min(Wavetable::maxHarmonic, n / 2);
        const auto scale = static_cast<float>(wide);
        bins_[0] = {};
        for (int h = 1; h <= wide / 2; ++h)
            bins_[static_cast<size_t>(h)] =
                h <= sources ? scale * (a[h] + t * (b[h] - a[h])) : std::complex<float>{};
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

float readCubic(const float* c, int size, double phase) noexcept
{
    const double at = phase * size;
    const auto i = static_cast<int>(at);
    const auto t = static_cast<float>(at - i);
    const float x0 = c[i - 1], x1 = c[i], x2 = c[i + 1], x3 = c[i + 2];
    return x1 +
           0.5f * t *
               (x2 - x0 + t * (2.0f * x0 - 5.0f * x1 + 4.0f * x2 - x3 + t * (3.0f * (x1 - x2) + x3 - x0)));
}

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
