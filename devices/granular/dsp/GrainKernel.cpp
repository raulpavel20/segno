#include "GrainKernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>

// The kernel is built twice on x86-64 Linux, for the baseline and for x86-64-v3 (AVX2, FMA), and
// the machine's picked at load (SPEC, "Performance": an SSE2-only build costs about 1.5x).
#if defined(__x86_64__) && defined(__linux__) && (defined(__clang__) || defined(__GNUC__))
#define FD_GRAIN_CLONES __attribute__((target_clones("default", "arch=x86-64-v3")))
#else
#define FD_GRAIN_CLONES
#endif
#if defined(__clang__) || defined(__GNUC__)
#define FD_GRAIN_INLINE [[gnu::always_inline]] inline
#else
#define FD_GRAIN_INLINE inline
#endif

namespace fd::devices::granular
{
namespace
{
constexpr int kMaxTaps = 8;

/// The plain pyramid's interpolator: an 8-tap Kaiser-windowed sinc (cut at 0.42 of the level's
/// rate, β 8.6: images under -64 dB, the band kept to 20 kHz at 48 kHz) in 256 phases, read
/// between phases.
struct SincTable
{
    static constexpr int phases = 256;
    std::array<std::array<float, kMaxTaps>, phases + 1> rows{};
    SincTable()
    {
        const auto i0 = [](double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr double cut = 0.42, beta = 8.6;
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = static_cast<double>(p) / phases;
            double sum = 0.0;
            for (int j = 0; j < kMaxTaps; ++j)
            {
                const double t = (j - 3) - frac;
                const double u = t / 4.5;
                const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0(beta);
                const double x = 2.0 * cut * t;
                const double s =
                    std::abs(x) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                rows[static_cast<size_t>(p)][static_cast<size_t>(j)] = static_cast<float>(s * w);
                sum += s * w;
            }
            for (auto& c : rows[static_cast<size_t>(p)])
                c = static_cast<float>(c / sum);
        }
    }
};
const SincTable& sincTable()
{
    static const SincTable t;
    return t;
}

template <typename T> FD_GRAIN_INLINE float toFloat(T x) noexcept
{
    if constexpr (std::is_same_v<T, std::int16_t>)
        return static_cast<float>(x) * (1.0f / 32768.0f);
    else
        return x;
}

FD_GRAIN_INLINE float opt4(float ym1, float y0, float y1, float y2, float x) noexcept
{
    // Optimal 2x, 4 points, 3rd order (Niemitalo, "Polynomial Interpolators for High-Quality
    // Resampling of Oversampled Audio", 2001), in the z form.
    const float z = x - 0.5f;
    const float even1 = y1 + y0, odd1 = y1 - y0, even2 = y2 + ym1, odd2 = y2 - ym1;
    const float c0 = even1 * 0.45868970870461956f + even2 * 0.04131401926395584f;
    const float c1 = odd1 * 0.48068024766578432f + odd2 * 0.17577925564495955f;
    const float c2 = even1 * -0.246185007019907091f + even2 * 0.24614027139700284f;
    const float c3 = odd1 * -0.36030925263849456f + odd2 * 0.10174985775982505f;
    return ((c3 * z + c2) * z + c1) * z + c0;
}

FD_GRAIN_INLINE float opt6(float ym2, float ym1, float y0, float y1, float y2, float y3, float x) noexcept
{
    // Optimal 2x, 6 points, 5th order (the same paper).
    const float z = x - 0.5f;
    const float even1 = y1 + y0, odd1 = y1 - y0, even2 = y2 + ym1, odd2 = y2 - ym1, even3 = y3 + ym2,
                odd3 = y3 - ym2;
    const float c0 =
        even1 * 0.40513396007145713f + even2 * 0.09251794438424393f + even3 * 0.00234806603570670f;
    const float c1 = odd1 * 0.28342806338906690f + odd2 * 0.21703277024054901f + odd3 * 0.01309294748731515f;
    const float c2 =
        even1 * -0.191337682540351941f + even2 * 0.16187844487943592f + even3 * 0.02946017143111912f;
    const float c3 =
        odd1 * -0.16471626190554542f + odd2 * -0.00154547203542499f + odd3 * 0.03399271444851909f;
    const float c4 =
        even1 * 0.03845798729588149f + even2 * -0.05712936104242644f + even3 * 0.01866750929921070f;
    const float c5 = odd1 * 0.04317950185225609f + odd2 * -0.01802814255926417f + odd3 * 0.00152170021558204f;
    return ((((c5 * z + c4) * z + c3) * z + c2) * z + c1) * z + c0;
}

/// A sine of `turns` (cycles), by an even polynomial on a quarter: the ring's oscillator.
FD_GRAIN_INLINE float sineTurns(float turns) noexcept
{
    float x = turns - std::floor(turns); // 0 … 1
    x = x < 0.5f ? x : 1.0f - x;         // sin is symmetric about a quarter … from 0 to a half
    const float y = 4.0f * x - 1.0f;     // -1 … 1 over the half cycle, 0 at its peak
    // cos(pi/2 * y): the same polynomial as the Hann window's.
    const float y2 = y * y;
    const float c = 1.0f + y2 * (-1.2336979f + y2 * (0.2536695f - 0.0208621f * y2));
    return turns - std::floor(turns) < 0.5f ? c : -c;
}

/// Pass 1: each sample's taps (tap-major: tap t of channel c at taps[(t * C + c) * cap + s]) and
/// fraction, for `count` samples from `g`'s position. The only random reads.
template <Interp I, int C, typename T>
FD_GRAIN_INLINE void fetch(const Grain& g, const T* base, int count, GrainScratch& sc) noexcept
{
    constexpr int before = tapsBefore(I), taps = before + tapsAfter(I) + 1;
    const int cap = sc.capacity;
    float* tp = sc.taps.data();
    float* fr = sc.frac.data();
    const auto read = [&](int s, std::int64_t pos)
    {
        const auto idx = static_cast<std::int32_t>(pos >> 32);
        // The fraction's top 24 bits: exact in a float, and an integer conversion (no doubles).
        fr[s] = static_cast<float>(static_cast<std::uint32_t>(pos) >> 8) * (1.0f / 16777216.0f);
        const T* at = base + static_cast<std::ptrdiff_t>(idx - before) * C;
        for (int t = 0; t < taps; ++t)
            for (int c = 0; c < C; ++c)
                tp[(t * C + c) * cap + s] = toFloat(at[t * C + c]);
    };
    if (const int hold = g.crushHold; hold > 1)
    {
        // Crushed: each held sample read where its hold began (from the grain's age, so any block
        // split gives the same), one division a block.
        int phase = g.age % hold;
        std::int64_t held = g.posAt(g.age - phase);
        for (int s = 0; s < count; ++s)
        {
            read(s, held);
            if (++phase == hold)
            {
                phase = 0;
                held = g.posAt(static_cast<std::int64_t>(g.age) + s + 1);
            }
        }
        return;
    }
    std::int64_t pos = g.pos, inc = g.inc;
    for (int s = 0; s < count; ++s)
    {
        read(s, pos);
        pos += inc;
        inc += g.dinc;
    }
}

/// The plain pyramid's read: the 8-tap polyphase sinc straight from the level, each output one
/// 8-wide dot product (the taps are contiguous), summed as a fixed tree so it's the same always.
template <int C, typename T>
FD_GRAIN_INLINE void fetchSinc(const Grain& g, const T* base, int count, GrainScratch& sc) noexcept
{
    const auto& table = sincTable();
    float* __restrict out[2] = {sc.y0.data(), sc.y1.data()};
    const auto one = [&](int s, std::int64_t pos)
    {
        const auto idx = static_cast<std::int32_t>(pos >> 32);
        const float ph =
            static_cast<float>(static_cast<std::uint32_t>(pos) >> 8) * (SincTable::phases / 16777216.0f);
        const int p = std::min(SincTable::phases - 1, static_cast<int>(ph));
        const float w = ph - static_cast<float>(p);
        const float* r0 = table.rows[static_cast<size_t>(p)].data();
        const float* r1 = table.rows[static_cast<size_t>(p + 1)].data();
        const T* at = base + static_cast<std::ptrdiff_t>(idx - 3) * C;
        for (int c = 0; c < C; ++c)
        {
            float q[kMaxTaps];
            for (int t = 0; t < kMaxTaps; ++t)
                q[t] = (r0[t] + (r1[t] - r0[t]) * w) * toFloat(at[t * C + c]);
            out[c][s] = ((q[0] + q[4]) + (q[1] + q[5])) + ((q[2] + q[6]) + (q[3] + q[7]));
        }
    };
    if (const int hold = g.crushHold; hold > 1)
    {
        int phase = g.age % hold;
        std::int64_t held = g.posAt(g.age - phase);
        for (int s = 0; s < count; ++s)
        {
            one(s, held);
            if (++phase == hold)
            {
                phase = 0;
                held = g.posAt(static_cast<std::int64_t>(g.age) + s + 1);
            }
        }
        return;
    }
    std::int64_t pos = g.pos, inc = g.inc;
    for (int s = 0; s < count; ++s)
    {
        one(s, pos);
        pos += inc;
        inc += g.dinc;
    }
}

/// Pass 2a: the interpolated samples of channel `c`, into `out`. Vectorises across samples.
template <Interp I, int C>
FD_GRAIN_INLINE void interpolate(int c, int count, const GrainScratch& sc, float* __restrict out) noexcept
{
    const int cap = sc.capacity;
    const float* __restrict fr = sc.frac.data();
    const auto tap = [&](int t) -> const float* { return sc.taps.data() + (t * C + c) * cap; };
    if constexpr (I == Interp::Linear)
    {
        const float* __restrict a = tap(0);
        const float* __restrict b = tap(1);
        for (int s = 0; s < count; ++s)
            out[s] = a[s] + (b[s] - a[s]) * fr[s];
    }
    else if constexpr (I == Interp::Opt4)
    {
        const float* __restrict t0 = tap(0), * __restrict t1 = tap(1), * __restrict t2 = tap(2), * __restrict t3 = tap(3);
        for (int s = 0; s < count; ++s)
            out[s] = opt4(t0[s], t1[s], t2[s], t3[s], fr[s]);
    }
    else // Opt6 (Sinc8 reads in fetchSinc)
    {
        const float* __restrict t0 = tap(0), * __restrict t1 = tap(1), * __restrict t2 = tap(2), * __restrict t3 = tap(3), * __restrict t4 = tap(4), * __restrict t5 = tap(5);
        for (int s = 0; s < count; ++s)
            out[s] = opt6(t0[s], t1[s], t2[s], t3[s], t4[s], t5[s], fr[s]);
    }
}

/// The window, the level's fade (when stolen), the colour: per sample, on both channels.
template <int C>
FD_GRAIN_INLINE void shape(Grain& g, int count, float* __restrict y0, float* __restrict y1) noexcept
{
    constexpr bool stereo = C == 2;
    const float inv = g.length > 1 ? 1.0f / static_cast<float>(g.length - 1) : 0.0f;
    // The phase from the grain's age: whole samples, exact in a float, so any block split agrees.
    const auto age = static_cast<float>(g.age);
    if (g.window.shape == Window::Hann && g.window.skew == 0.0f && g.window.flatness == 0.0f)
    {
        // The common case, vectorised: cos² as the square of an even polynomial.
        for (int s = 0; s < count; ++s)
        {
            const float u = 2.0f * ((age + static_cast<float>(s)) * inv) - 1.0f, u2 = u * u;
            const float c = std::max(0.0f, 1.0f + u2 * (-1.2336979f + u2 * (0.2536695f - 0.0208621f * u2)));
            y0[s] *= c * c;
            if constexpr (stereo)
                y1[s] *= c * c;
        }
    }
    else
        for (int s = 0; s < count; ++s)
        {
            const float w = g.window.at((age + static_cast<float>(s)) * inv);
            y0[s] *= w;
            if constexpr (stereo)
                y1[s] *= w;
        }
    if (g.ringMix > 0.0f)
        for (int s = 0; s < count; ++s)
        {
            const float ring =
                (1.0f - g.ringMix) + g.ringMix * sineTurns(static_cast<float>(g.age + s) * g.ringInc);
            y0[s] *= ring;
            if (stereo)
                y1[s] *= ring;
        }
    if (g.crushStep > 0.0f)
    {
        const float q = 1.0f / g.crushStep;
        for (int s = 0; s < count; ++s)
        {
            y0[s] = std::nearbyint(y0[s] * q) * g.crushStep;
            if (stereo)
                y1[s] = std::nearbyint(y1[s] * q) * g.crushStep;
        }
    }
    if (g.fadeLength > 0)
        for (int s = 0; s < count; ++s)
        {
            const float f = static_cast<float>(std::clamp(g.fadeLeft - s, 0, g.fadeLength)) /
                            static_cast<float>(g.fadeLength);
            y0[s] *= f;
            if (stereo)
                y1[s] *= f;
        }
}

/// One grain over `n` samples: each stretch it plays (its repeats restart back to back) read,
/// interpolated and shaped, then handed to `sink(start, count, y0, y1)`; the grain moves on.
template <Interp I, int C, typename T, typename Sink>
FD_GRAIN_INLINE void play(Grain& g, const T* base, int n, GrainScratch& sc, Sink&& sink) noexcept
{
    float* __restrict y0 = sc.y0.data();
    float* __restrict y1 = sc.y1.data();
    for (;;)
    {
        if (g.delay >= n)
        {
            g.delay -= n;
            return;
        }
        const int start = std::max(0, g.delay);
        g.delay = 0;
        int count = std::min({n - start, g.length - g.age, sc.capacity});
        if (g.fadeLength > 0)
            count = std::min(count, g.fadeLeft);
        if (count <= 0)
        {
            g.age = g.length;
            return;
        }
        if constexpr (I == Interp::Sinc8)
            fetchSinc<C, T>(g, base, count, sc);
        else
        {
            fetch<I, C, T>(g, base, count, sc);
            interpolate<I, C>(0, count, sc, y0);
            if constexpr (C == 2)
                interpolate<I, C>(1, count, sc, y1);
        }
        shape<C>(g, count, y0, y1);
        sink(start, count, y0, y1);
        // On by `count` samples: the position by its (gliding) speed, exactly.
        const auto steps = static_cast<std::int64_t>(count);
        g.pos += g.inc * steps + g.dinc * (steps * (steps - 1) / 2);
        g.inc += g.dinc * steps;
        g.age += count;
        if (g.fadeLength > 0)
            g.fadeLeft -= count;
        // Repeats: again from its start, at once (a grain stepping aside goes on fading).
        if (g.age < g.length || g.repeatsLeft <= 0 || (g.fadeLength > 0 && g.fadeLeft <= 0))
            return;
        --g.repeatsLeft;
        g.age = 0;
        g.pos = g.pos0;
        g.inc = g.inc0;
        g.delay = start + count;
    }
}

/// Calls `fn` with the grain's build: its interpolator, channels and sample type.
template <typename Fn> FD_GRAIN_INLINE void dispatch(const Grain& g, Fn&& fn) noexcept
{
    const auto byInterp = [&](auto channels, auto sample)
    {
        using T = typename decltype(sample)::type;
        constexpr int C = decltype(channels)::value;
        switch (g.interp)
        {
        case Interp::Linear:
            fn.template operator()<Interp::Linear, C, T>();
            break;
        case Interp::Opt4:
            fn.template operator()<Interp::Opt4, C, T>();
            break;
        case Interp::Opt6:
            fn.template operator()<Interp::Opt6, C, T>();
            break;
        case Interp::Sinc8:
            fn.template operator()<Interp::Sinc8, C, T>();
            break;
        }
    };
    struct F32
    {
        using type = float;
    };
    struct I16
    {
        using type = std::int16_t;
    };
    if (g.i16 != nullptr)
        g.channels == 2 ? byInterp(std::integral_constant<int, 2>{}, I16{})
                        : byInterp(std::integral_constant<int, 1>{}, I16{});
    else
        g.channels == 2 ? byInterp(std::integral_constant<int, 2>{}, F32{})
                        : byInterp(std::integral_constant<int, 1>{}, F32{});
}

template <typename T> const T* samplesOf(const Grain& g) noexcept
{
    if constexpr (std::is_same_v<T, float>)
        return g.f32;
    else
        return g.i16;
}

/// A grain without a filter: straight into the bus, through its pan.
FD_GRAIN_INLINE void renderPlain(Grain& g, float* left, float* right, int n, GrainScratch& sc) noexcept
{
    dispatch(g,
             [&]<Interp I, int C, typename T>()
             {
                 play<I, C, T>(
                     g, samplesOf<T>(g), n, sc,
                     [&](int start, int count, const float* __restrict y0, const float* __restrict y1)
                     {
                         float* __restrict l = left + start;
                         float* __restrict r = right + start;
                         if constexpr (C == 2)
                         {
                             const float ll = g.gLL, rl = g.gRL, lr = g.gLR, rr = g.gRR;
                             for (int s = 0; s < count; ++s)
                             {
                                 l[s] += ll * y0[s] + rl * y1[s];
                                 r[s] += lr * y0[s] + rr * y1[s];
                             }
                         }
                         else
                         {
                             const float ll = g.gLL, lr = g.gLR;
                             for (int s = 0; s < count; ++s)
                             {
                                 l[s] += ll * y0[s];
                                 r[s] += lr * y0[s];
                             }
                         }
                     });
             });
}

/// Up to eight grains with filters, together: each shaped into its lane, the filters run across
/// the lanes (SIMD over the grains: a recursion can't run along the samples), then each grain into
/// the bus in turn.
FD_GRAIN_INLINE void renderFiltered(Grain* const* grains, int count, float* left, float* right, int n,
                                    GrainScratch& sc) noexcept
{
    constexpr int L = GrainScratch::laneCount;
    float* lane0 = sc.lanes.data();
    float* lane1 = lane0 + static_cast<size_t>(L) * static_cast<size_t>(sc.capacity);
    std::array<int, L> from{}, to{};
    bool stereo = false;
    int lo = n, hi = 0;
    for (int j = 0; j < count; ++j)
    {
        Grain& g = *grains[j];
        stereo = stereo || g.channels == 2;
        from[static_cast<size_t>(j)] = n;
        to[static_cast<size_t>(j)] = 0;
        dispatch(g,
                 [&]<Interp I, int C, typename T>()
                 {
                     play<I, C, T>(g, samplesOf<T>(g), n, sc,
                                   [&](int start, int length, const float* y0, const float* y1)
                                   {
                                       auto& f = from[static_cast<size_t>(j)];
                                       auto& t = to[static_cast<size_t>(j)];
                                       // Clear the gap since the lane's last stretch (none: repeats
                                       // restart at once), then the stretch.
                                       for (int s = std::max(t, std::min(f, start)); s < start; ++s)
                                           lane0[s * L + j] = lane1[s * L + j] = 0.0f;
                                       for (int s = 0; s < length; ++s)
                                           lane0[(start + s) * L + j] = y0[s];
                                       if constexpr (C == 2)
                                           for (int s = 0; s < length; ++s)
                                               lane1[(start + s) * L + j] = y1[s];
                                       f = std::min(f, start);
                                       t = std::max(t, start + length);
                                   });
                 });
        lo = std::min(lo, from[static_cast<size_t>(j)]);
        hi = std::max(hi, to[static_cast<size_t>(j)]);
    }
    if (lo >= hi)
        return;
    // Outside its stretch, a lane hears silence (a grain starting later: its state is zero, so it
    // stays zero; one that has ended: thrown away).
    for (int j = 0; j < L; ++j)
    {
        const bool used = j < count;
        const int f = used ? from[static_cast<size_t>(j)] : hi, t = used ? to[static_cast<size_t>(j)] : hi;
        for (int s = lo; s < std::min(f, hi); ++s)
            lane0[s * L + j] = lane1[s * L + j] = 0.0f;
        for (int s = std::max(t, lo); s < hi; ++s)
            lane0[s * L + j] = lane1[s * L + j] = 0.0f;
        if (used && grains[j]->channels == 1)
            for (int s = f; s < t; ++s)
                lane1[s * L + j] = 0.0f;
    }
    // The coefficients and states, a lane each; the type's output chosen by weights (0 or 1).
    alignas(32) float a1[L]{}, a2[L]{}, a3[L]{}, k[L]{}, wLp[L]{}, wBp[L]{}, wHp[L]{};
    alignas(32) float s1[2][L]{}, s2[2][L]{};
    for (int j = 0; j < count; ++j)
    {
        const Grain& g = *grains[j];
        a1[j] = g.fA1;
        a2[j] = g.fA2;
        a3[j] = g.fA3;
        k[j] = g.fK;
        wLp[j] = g.filterType == 1 ? 1.0f : 0.0f;
        wHp[j] = g.filterType == 3 ? 1.0f : 0.0f;
        wBp[j] = g.filterType == 2 || g.filterType == 4 ? g.fGain : 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            s1[c][j] = g.fIc1[c];
            s2[c][j] = g.fIc2[c];
        }
    }
    // One sample of the eight filters on one channel's lanes.
    const auto step = [&](float* __restrict x, float* __restrict ic1, float* __restrict ic2)
    {
        for (int j = 0; j < L; ++j)
        {
            const float v0 = x[j];
            const float v3 = v0 - ic2[j];
            const float v1 = a1[j] * ic1[j] + a2[j] * v3;
            const float v2 = ic2[j] + a2[j] * ic1[j] + a3[j] * v3;
            ic1[j] = 2.0f * v1 - ic1[j];
            ic2[j] = 2.0f * v2 - ic2[j];
            x[j] = wLp[j] * v2 + wBp[j] * v1 + wHp[j] * (v0 - k[j] * v1 - v2);
        }
    };
    // Both channels in one loop: their recursions' latencies overlap.
    if (stereo)
        for (int s = lo; s < hi; ++s)
        {
            step(lane0 + s * L, s1[0], s2[0]);
            step(lane1 + s * L, s1[1], s2[1]);
        }
    else
        for (int s = lo; s < hi; ++s)
            step(lane0 + s * L, s1[0], s2[0]);
    // Each grain into the bus, in turn.
    for (int j = 0; j < count; ++j)
    {
        Grain& g = *grains[j];
        for (int c = 0; c < 2; ++c)
        {
            g.fIc1[c] = s1[c][j];
            g.fIc2[c] = s2[c][j];
        }
        const float ll = g.gLL, rl = g.gRL, lr = g.gLR, rr = g.gRR;
        for (int s = from[static_cast<size_t>(j)]; s < to[static_cast<size_t>(j)]; ++s)
        {
            const float x0 = lane0[s * L + j], x1 = lane1[s * L + j];
            left[s] += ll * x0 + rl * x1;
            right[s] += lr * x0 + rr * x1;
        }
    }
}
} // namespace

void GrainScratch::prepare(int maxBlock)
{
    capacity = std::max(1, maxBlock);
    taps.assign(static_cast<size_t>(kMaxTaps * 2 * capacity), 0.0f);
    frac.assign(static_cast<size_t>(capacity), 0.0f);
    y0.assign(static_cast<size_t>(capacity), 0.0f);
    y1.assign(static_cast<size_t>(capacity), 0.0f);
    this->lanes.assign(static_cast<size_t>(2 * GrainScratch::laneCount * capacity), 0.0f);
}

void setGrainFilter(Grain& g, int type, double cutoff, double resonance, double sampleRate) noexcept
{
    g.filterType = static_cast<std::uint8_t>(type);
    if (type == 0)
        return;
    const auto c = grainFilterShape(type, resonance);
    const double w = std::tan(std::numbers::pi * std::clamp(cutoff, 10.0, 0.49 * sampleRate) / sampleRate);
    const double a1 = 1.0 / (1.0 + w * (w + c.k));
    g.fK = static_cast<float>(c.k);
    g.fA1 = static_cast<float>(a1);
    g.fA2 = static_cast<float>(w * a1);
    g.fA3 = static_cast<float>(w * w * a1);
    g.fGain = static_cast<float>(c.k); // a band pass at unity at its peak
    g.fIc1[0] = g.fIc1[1] = g.fIc2[0] = g.fIc2[1] = 0.0f;
}

GrainFilterShape grainFilterShape(int type, double resonance) noexcept
{
    resonance = std::clamp(resonance, 0.0, 1.0);
    // Low, band and high pass: Q from 0.5 to about 25; the resonator rings: Q from 3 to 60.
    return {type == 4 ? 1.0 / (3.0 + 57.0 * resonance) : 2.0 * (1.0 - 0.98 * resonance)};
}

double grainFilterMagnitude(int type, double hz, double cutoff, double resonance, double sampleRate) noexcept
{
    if (type == 0)
        return 1.0;
    const double k = grainFilterShape(type, resonance).k;
    // The digital filter's response, exactly: the analog prototype at the pre-warped frequency.
    const double w = std::tan(std::numbers::pi * std::min(hz, 0.4999 * sampleRate) / sampleRate) /
                     std::tan(std::numbers::pi * std::clamp(cutoff, 10.0, 0.49 * sampleRate) / sampleRate);
    const double re = 1.0 - w * w, im = w * k, d = std::sqrt(re * re + im * im);
    switch (type)
    {
    case 1:
        return 1.0 / d;
    case 3:
        return w * w / d;
    default:
        return k * w / d; // band pass and resonator: unity at the peak
    }
}

FD_GRAIN_CLONES void renderGrains(std::span<Grain> grains, float* left, float* right, int n,
                                  GrainScratch& sc) noexcept
{
    // The plain grains first, then the filtered ones eight at a time; each kind in the order the
    // grains started, so the sum at any sample is the same however the block is cut.
    std::array<Grain*, GrainScratch::laneCount> group{};
    int inGroup = 0;
    for (auto& g : grains)
        if (g.filterType == 0)
            renderPlain(g, left, right, n, sc);
    for (auto& g : grains)
        if (g.filterType != 0)
        {
            group[static_cast<size_t>(inGroup++)] = &g;
            if (inGroup == GrainScratch::laneCount)
            {
                renderFiltered(group.data(), inGroup, left, right, n, sc);
                inGroup = 0;
            }
        }
    if (inGroup > 0)
        renderFiltered(group.data(), inGroup, left, right, n, sc);
}

void renderGrain(Grain& g, float* left, float* right, int n, GrainScratch& sc) noexcept
{
    renderGrains({&g, 1}, left, right, n, sc);
}
} // namespace fd::devices::granular
