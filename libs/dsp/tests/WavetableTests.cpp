// The Wavetable Synth's tables and cycles (devices/wavetable/SPEC.md, "DSP design").
#include "fd/dsp/Math.h"
#include "fd/dsp/Wavetable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
/// `frames` frames of 2 048 samples: frame f an additive saw of `1 + f` harmonics... or as given.
template <typename Fn> std::vector<float> cycles(int length, int frames, Fn&& sample)
{
    std::vector<float> x(static_cast<size_t>(length * frames));
    for (int f = 0; f < frames; ++f)
        for (int n = 0; n < length; ++n)
            x[static_cast<size_t>(f * length + n)] =
                static_cast<float>(sample(f, static_cast<double>(n) / length));
    return x;
}

double saw(double phase, int harmonics)
{
    double v = 0.0;
    for (int h = 1; h <= harmonics; ++h)
        v += std::sin(twoPi * h * phase) / h;
    return v;
}

/// The magnitude of each harmonic (0 ... size/2) of one cycle of `size` samples.
std::vector<double> harmonicsOf(const float* x, int size)
{
    std::vector<double> out(static_cast<size_t>(size / 2 + 1));
    for (int h = 0; h <= size / 2; ++h)
    {
        std::complex<double> sum;
        for (int n = 0; n < size; ++n)
            sum += static_cast<double>(x[n]) * std::polar(1.0, -twoPi * h * n / size);
        out[static_cast<size_t>(h)] = std::abs(sum) / size;
    }
    return out;
}
} // namespace

TEST_CASE("real FFT: forward matches a direct transform, and inverse undoes it", "[fast]")
{
    for (const int order : {2, 6, 11})
    {
        RealFft fft(order);
        const int n = fft.size();
        std::mt19937 rng(static_cast<unsigned>(order));
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> x(static_cast<size_t>(n)), back(static_cast<size_t>(n));
        for (auto& v : x)
            v = u(rng);
        std::vector<std::complex<float>> bins(static_cast<size_t>(n / 2 + 1));
        fft.forward(x.data(), bins.data());
        for (int k = 0; k <= n / 2; ++k)
        {
            std::complex<double> sum;
            for (int i = 0; i < n; ++i)
                sum += static_cast<double>(x[static_cast<size_t>(i)]) * std::polar(1.0, -twoPi * k * i / n);
            CHECK(std::abs(std::complex<double>(bins[static_cast<size_t>(k)]) - sum) < 1e-3 * std::sqrt(n));
        }
        fft.inverse(bins.data(), back.data());
        for (int i = 0; i < n; ++i)
            CHECK(back[static_cast<size_t>(i)] == Approx(x[static_cast<size_t>(i)]).margin(1e-5));
    }
}

TEST_CASE("wavetable: cycles of any length become 2 048-sample frames, without DC, peaking at 1", "[fast]")
{
    // A saw of 40 harmonics over 2 048 samples, and the same over 600 (an AKWF single cycle's length).
    for (const int length : {2048, 600})
    {
        const auto x = cycles(length, 1, [](int, double p) { return 0.3 + 0.5 * saw(p, 40); });
        const auto t = Wavetable::fromCycles(x.data(), length, 1);
        REQUIRE(t.frames() == 1);
        const auto* c = t.spectrum(0);
        CHECK(std::abs(c[0]) == 0.0f);
        for (int h = 2; h <= 40; ++h)
            CHECK(std::abs(c[h]) / std::abs(c[1]) == Approx(1.0 / h).epsilon(1e-3));
        CHECK(std::abs(c[41]) < 1e-5f * std::abs(c[1]));
        CHECK(t.highestHarmonic() == 40);
        float peak = 0.0f;
        for (int n = 0; n < Wavetable::frameSize; ++n)
            peak = std::max(peak, std::abs(t.samples(0)[n]));
        CHECK(peak == Approx(1.0f).margin(1e-5));
        // The resampled cycle is the same wave: a sine at the 3rd harmonic stays one.
    }
    const auto sine = cycles(600, 1, [](int, double p) { return std::sin(twoPi * 3.0 * p); });
    const auto t = Wavetable::fromCycles(sine.data(), 600, 1);
    double worst = 0.0;
    for (int n = 0; n < Wavetable::frameSize; ++n)
        worst = std::max(worst, std::abs(t.samples(0)[n] - std::sin(twoPi * 3.0 * n / Wavetable::frameSize)));
    CHECK(20.0 * std::log10(worst) < -100.0);
}

TEST_CASE("cycle renderer: harmonics cut to the note's limit, the cycle sized for it", "[fast]")
{
    const auto x = cycles(2048, 1, [](int, double p) { return saw(p, 1000); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 1);
    CycleRenderer r;
    r.prepare();
    std::vector<float> buffer(CycleRenderer::bufferSize);
    for (const int limit : {5, 30, 200, 1000})
    {
        const int n = r.render(table, 0.0, {}, limit, buffer.data());
        CHECK(n == CycleRenderer::sizeFor(limit));
        CHECK(n >= std::min(32 * limit, CycleRenderer::maxSize));
        const auto h = harmonicsOf(CycleRenderer::cycle(buffer.data()), n);
        for (int k = 1; k < static_cast<int>(h.size()); ++k)
        {
            INFO("limit " << limit << ", harmonic " << k);
            if (k <= std::min(limit, n / 2 - 1))
                CHECK(h[static_cast<size_t>(k)] / h[1] == Approx(1.0 / k).epsilon(2e-3));
            else
                CHECK(h[static_cast<size_t>(k)] < 1e-5 * h[1]);
        }
        // The guards wrap the cycle round.
        const float* c = CycleRenderer::cycle(buffer.data());
        CHECK(c[-1] == c[n - 1]);
        CHECK(c[-4] == c[n - 4]);
        CHECK(c[n] == c[0]);
        CHECK(c[n + 3] == c[3]);
    }
}

TEST_CASE("cycle renderer: the position moves between frames, harmonic by harmonic", "[fast]")
{
    // Frame 0 the fundamental alone, frame 1 the 2nd harmonic alone.
    const auto x = cycles(2048, 2, [](int f, double p) { return std::sin(twoPi * (f + 1) * p); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 2, false);
    CycleRenderer r;
    r.prepare();
    std::vector<float> buffer(CycleRenderer::bufferSize);
    const int n = r.render(table, 0.25, {}, 100, buffer.data());
    const auto h = harmonicsOf(CycleRenderer::cycle(buffer.data()), n);
    CHECK(h[1] == Approx(0.75 * 0.5).margin(1e-4)); // a sine of 1 has harmonic magnitudes of 1/2
    CHECK(h[2] == Approx(0.25 * 0.5).margin(1e-4));
}

TEST_CASE("cycle warps: none at zero, always band-limited, and each one changes the wave", "[fast]")
{
    const auto x = cycles(2048, 1, [](int, double p) { return saw(p, 60); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 1);
    CycleRenderer r;
    r.prepare();
    std::vector<float> plain(CycleRenderer::bufferSize), warped(CycleRenderer::bufferSize);
    CurveTable curve{};
    for (size_t i = 0; i < curve.size(); ++i)
        curve[i] = static_cast<float>(std::pow(static_cast<double>(i) / (curve.size() - 1), 2.0));
    const int limit = 40;
    const int n = r.render(table, 0.0, {}, limit, plain.data());
    for (int w = static_cast<int>(Warp::Sync); w <= static_cast<int>(Warp::Remap); ++w)
    {
        const auto warp = static_cast<Warp>(w);
        INFO("warp " << w);
        CycleRenderer::WarpSetting none{warp, 0.0f, &curve};
        r.render(table, 0.0, {none, {}}, limit, warped.data());
        double worst = 0.0, change = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, static_cast<double>(std::abs(CycleRenderer::cycle(warped.data())[i] -
                                                                 CycleRenderer::cycle(plain.data())[i])));
        if (warp != Warp::WindowSync) // always windowed
            CHECK(worst < 1e-3);
        CycleRenderer::WarpSetting full{warp, 0.7f, &curve};
        CHECK(r.render(table, 0.0, {full, {}}, limit, warped.data()) == n);
        const auto h = harmonicsOf(CycleRenderer::cycle(warped.data()), n);
        for (int k = limit + 1; k < static_cast<int>(h.size()); ++k)
            CHECK(h[static_cast<size_t>(k)] < 1e-5);
        for (int i = 0; i < n; ++i)
            change = std::max(change, static_cast<double>(std::abs(CycleRenderer::cycle(warped.data())[i] -
                                                                   CycleRenderer::cycle(plain.data())[i])));
        CHECK(change > 0.05);
    }
}

// ---- the oscillator --------------------------------------------------------------------------

#include "fd/dsp/WavetableOscillator.h"

namespace
{
/// Plays `seconds` of an oscillator at `hz` on `table`, rendering every 64 samples, and returns
/// its left channel.
std::vector<float> play(const Wavetable& table, double hz, double seconds,
                        const std::array<CycleRenderer::WarpSetting, 2>& warps = {},
                        WavetableOscillator::Unison unison = {}, bool fine = false,
                        double sampleRate = 48000.0)
{
    CycleRenderer renderer;
    renderer.prepare();
    WavetableOscillator osc;
    osc.setFineReads(fine);
    osc.setUnison(unison);
    osc.start(7, 0.0f, 0.0f);
    const auto n = static_cast<size_t>(seconds * sampleRate);
    std::vector<float> out(n);
    const double inc = hz / sampleRate;
    for (size_t i = 0; i < n; ++i)
    {
        if (i % 64 == 0)
            osc.render(renderer, table, 0.0, warps, inc, sampleRate, 64);
        float l = 0.0f, r = 0.0f;
        osc.next(inc, 1.0, 0.0, l, r);
        out[i] = l;
    }
    return out;
}

/// Energy (dB, relative to all of it) that isn't at a multiple of `hz`: aliasing. `hz` sits on a
/// bin of the 8 192-point transform (`onBin`) and the signal is periodic, so a rectangular window
/// leaks nothing from the harmonics.
double inharmonicDb(const std::vector<float>& x, double hz, double sampleRate = 48000.0)
{
    const int n = 8192;
    const size_t from = x.size() - n;
    std::vector<std::complex<float>> buf(n);
    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t>(i)] = {x[from + static_cast<size_t>(i)], 0.0f};
    Fft fft(13);
    fft.forward(buf.data());
    const auto step = static_cast<int>(std::lround(hz * n / sampleRate));
    double all = 0.0, stray = 0.0;
    for (int k = 1; k < n / 2; ++k)
    {
        const double p = std::norm(buf[static_cast<size_t>(k)]);
        all += p;
        if (k % step != 0)
            stray += p;
    }
    return 10.0 * std::log10(std::max(stray, 1e-30) / all);
}

/// The frequency of the 8 192-point bin nearest `hz`, at 48 kHz.
double onBin(double hz)
{
    const double bin = 48000.0 / 8192;
    return std::round(hz / bin) * bin;
}
} // namespace

TEST_CASE("wavetable oscillator: high notes and warps without aliasing", "[fast]")
{
    const auto x = cycles(2048, 1, [](int, double p) { return saw(p, 1000); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 1);
    // A bright saw from low to high, plain and with the harshest warps of the cycle: cubic reads
    // (Normal) and 8-point reads (High).
    for (const bool fine : {false, true})
        for (const double hz : {onBin(55.0), onBin(110.0), onBin(1046.5), onBin(3520.0)})
        {
            INFO((fine ? "8-point" : "cubic") << ", " << hz << " Hz");
            const double plainLimit = fine ? -100.0 : -80.0, warpLimit = fine ? -90.0 : -70.0;
            const double plain = inharmonicDb(play(table, hz, 0.3, {}, {}, fine), hz);
            WARN((fine ? "8-point " : "cubic ") << hz << " Hz plain: " << plain << " dB");
            CHECK(plain < plainLimit);
            for (const auto warp : {Warp::Sync, Warp::Pulse, Warp::Quantize, Warp::Fold, Warp::BendBoth})
            {
                INFO("warp " << static_cast<int>(warp));
                const double warped =
                    inharmonicDb(play(table, hz, 0.3, {{{warp, 0.9f, nullptr}, {}}}, {}, fine), hz);
                CHECK(warped < warpLimit);
            }
        }
}

TEST_CASE("wavetable oscillator: spread cycles take turns, and still move without a jump", "[fast]")
{
    // Frame 0 a sine, frame 1 a saw; eight copies spread over the table: four cycles.
    const auto x = cycles(2048, 2, [](int f, double p) { return f == 0 ? std::sin(twoPi * p) : saw(p, 40); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 2);
    CycleRenderer renderer;
    renderer.prepare();
    WavetableOscillator osc;
    WavetableOscillator::Unison u;
    u.count = 8;
    u.detune = 0.0f;
    u.width = 0.0f;
    u.positionSpread = 0.5f;
    osc.setUnison(u);
    osc.start(1, 0.0f, 0.0f);
    std::vector<float> out(48000);
    int calls = 0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (i % 64 == 0)
        {
            osc.render(renderer, table, 0.25 + 0.5 * static_cast<double>(i) / out.size(), {}, 200.0 / 48000.0,
                       48000.0, 64);
            ++calls;
        }
        float l = 0.0f, r = 0.0f;
        osc.next(200.0 / 48000.0, 1.0, 0.0, l, r);
        out[i] = l;
    }
    // All four at the start, then one a call.
    CHECK(osc.cyclesRendered() == 4 + (calls - 1));
    double worstStep = 0.0;
    for (size_t i = 1; i < out.size(); ++i)
        worstStep = std::max(worstStep, static_cast<double>(std::abs(out[i] - out[i - 1])));
    // Never steeper than the same copies held on the saw frame: no jumps between renders.
    WavetableOscillator held;
    held.setUnison(u);
    held.start(1, 0.0f, 0.0f);
    double heldStep = 0.0;
    float last = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        if (i % 64 == 0)
            held.render(renderer, table, 1.0, {}, 200.0 / 48000.0, 48000.0, 64);
        float l = 0.0f, r = 0.0f;
        held.next(200.0 / 48000.0, 1.0, 0.0, l, r);
        if (i > 0)
            heldStep = std::max(heldStep, static_cast<double>(std::abs(l - last)));
        last = l;
    }
    CHECK(worstStep < 1.05 * heldStep);
}

TEST_CASE("wavetable oscillator: vibrato doesn't render a cycle every time", "[fast]")
{
    const auto x = cycles(2048, 1, [](int, double p) { return saw(p, 1000); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 1);
    CycleRenderer renderer;
    renderer.prepare();
    WavetableOscillator osc;
    osc.start(1, 0.0f, 0.0f);
    // 5 Hz vibrato of a quarter tone either side, 2 seconds: 1 500 calls on a 64-sample grid.
    int calls = 0;
    for (int i = 0; i < 96000; i += 64, ++calls)
    {
        const double hz = 220.0 * std::exp2(0.25 / 12.0 * std::sin(twoPi * 5.0 * i / 48000.0));
        osc.render(renderer, table, 0.0, {}, hz / 48000.0 * 1.1225, 48000.0, 64);
    }
    INFO(calls << " calls");
    CHECK(osc.cyclesRendered() < 10);
}

TEST_CASE("wavetable oscillator: unison keeps its level from 1 to 16 copies, each copy where it should be",
          "[fast]")
{
    const auto x = cycles(2048, 1, [](int, double p) { return std::sin(twoPi * p); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 1);
    const auto rms = [](const std::vector<float>& v)
    {
        double s = 0.0;
        for (size_t i = v.size() / 2; i < v.size(); ++i)
            s += static_cast<double>(v[i]) * v[i];
        return 10.0 * std::log10(s / static_cast<double>(v.size() / 2));
    };
    const double one = rms(play(table, 220.0, 2.0));
    for (const int count : {2, 7, 16})
    {
        WavetableOscillator::Unison u;
        u.count = count;
        u.detune = 0.6f;
        u.blend = 1.0f;
        u.width = 0.0f;
        INFO(count << " copies");
        CHECK(rms(play(table, 220.0, 2.0, {}, u)) == Approx(one).margin(1.0));
    }
    // A stack of an octave: half the copies an octave up.
    WavetableOscillator::Unison octave;
    octave.count = 2;
    octave.detune = 0.0f;
    octave.stack = WavetableOscillator::Stack::Octave;
    WavetableOscillator osc;
    osc.setUnison(octave);
    CHECK(osc.widestRatio() == Approx(2.0));
}

TEST_CASE("wavetable oscillator: a moving position crossfades, a still one renders once", "[fast]")
{
    // Frame 0 a sine, frame 1 a saw: sweeping between them never clicks.
    const auto x = cycles(2048, 2, [](int f, double p) { return f == 0 ? std::sin(twoPi * p) : saw(p, 40); });
    const auto table = Wavetable::fromCycles(x.data(), 2048, 2);
    CycleRenderer renderer;
    renderer.prepare();
    WavetableOscillator osc;
    osc.start(1, 0.0f, 0.0f);
    std::vector<float> out(48000);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (i % 64 == 0)
            osc.render(renderer, table, static_cast<double>(i) / out.size(), {}, 200.0 / 48000.0, 48000.0,
                       64);
        float l = 0.0f, r = 0.0f;
        osc.next(200.0 / 48000.0, 1.0, 0.0, l, r);
        out[i] = l;
    }
    double worstStep = 0.0;
    for (size_t i = 1; i < out.size(); ++i)
        worstStep = std::max(worstStep, static_cast<double>(std::abs(out[i] - out[i - 1])));
    // Never steeper than the saw frame itself is at its edge: no jumps between renders.
    double sawStep = 0.0;
    for (int i = 1; i < 240; ++i)
        sawStep = std::max(sawStep, std::abs(saw(i / 240.0, 40) - saw((i - 1) / 240.0, 40)));
    const auto* frame = table.samples(1);
    double peak = 0.0;
    for (int i = 0; i < Wavetable::frameSize; ++i)
        peak = std::max(peak, static_cast<double>(std::abs(frame[i])));
    CHECK(worstStep < 1.05 * sawStep * (peak / 1.85));
}
