#include "fd/dsp/Oscillator.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace fd::dsp;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;
constexpr int size = 1 << 15;

AudioBuffer render(int cycles, Oscillator::Mix mix)
{
    Oscillator osc;
    osc.prepare(sr);
    const double f = binCentredFrequency(cycles, size, sr) / sr;
    AudioBuffer out(1, 2 * size);
    for (int i = 0; i < 2 * size; ++i)
        out.at(0, i) = osc.process(f, mix);
    return out;
}
} // namespace

TEST_CASE("band-limited sawtooth meets the aliasing target at C7", "[fast]")
{
    // Spec: sawtooth aliasing below -70 dB at C7 (~2.09 kHz) at 48 kHz, in the audible band.
    constexpr int cycles = 1429; // 2093 Hz
    const auto saw = render(cycles, Oscillator::morph(0.0f, 0.5f));
    CHECK(aliasingRatioDb(saw, 0, size, size, cycles, sr) < -70.0);
    // And it is a sawtooth: harmonic k at about 1/k.
    const auto h = harmonicLevelsDb(saw, 0, size, size, cycles, 3);
    CHECK(h[1] - h[0] == Approx(-6.02).margin(0.3));
}

TEST_CASE("every waveform and the morph path stay band-limited", "[fast]")
{
    constexpr int cycles = 1429;
    for (float shape : {0.0f, 0.2f, 0.33f, 0.5f, 0.66f, 0.8f, 1.0f})
        for (float width : {0.5f, 0.15f})
        {
            const auto out = render(cycles, Oscillator::morph(shape, width));
            INFO("shape " << shape << " width " << width);
            CHECK(aliasingRatioDb(out, 0, size, size, cycles, sr) < -70.0);
            CHECK(peak(out) < 1.6); // band-limited edges overshoot (Gibbs), more with a min-phase BLEP
        }
}

TEST_CASE("oscillator frequency is exact", "[fast]")
{
    constexpr int cycles = 301;
    const auto saw = render(cycles, Oscillator::morph(1.0f, 0.5f)); // sine
    const auto h = harmonicLevelsDb(saw, 0, size, size, cycles, 2);
    CHECK(h[0] == Approx(0.0).margin(0.05));
    CHECK(h[1] < -100.0);
}

TEST_CASE("discontinuities just after a sample stay inside the BLEP table", "[fast]")
{
    // The offset of a discontinuity is computed in double and handed to the BLEP table as a
    // float in [0, 1). An edge just after the previous sample gives an offset a hair below 1,
    // which rounds up to 1.0f unless clamped (out-of-range table read).
    struct Case { double startPhase; Oscillator::Mix mix; };
    const Oscillator::Mix saw{1.0f, 0.0f, 0.0f, 0.0f, 0.5f}, square{0.0f, 1.0f, 0.0f, 0.0f, 0.5f},
        triangle{0.0f, 0.0f, 1.0f, 0.0f, 0.5f};
    for (const auto& c : {Case{1.0 - 1e-12, saw}, Case{0.5 - 1e-12, square}, Case{1.0 - 1e-12, square},
                          Case{0.5 - 1e-12, triangle}, Case{1.0 - 1e-12, triangle}})
    {
        Oscillator osc;
        osc.prepare(sr);
        osc.reset(c.startPhase);
        for (int i = 0; i < 64; ++i)
            REQUIRE(std::isfinite(osc.process(0.25, c.mix)));
    }
}

TEST_CASE("oscillator survives any frequency and width (fuzz)", "[fast]")
{
    // Settings jump per 32-sample control tick. Real synths ramp them; jumps are the stress case
    // (uncorrected mix changes leave some BLEP residue), so this bounds instability, not level.
    Oscillator osc;
    osc.prepare(sr);
    Rng rng(7);
    float peakOut = 0.0f;
    for (int tick = 0; tick < 20000; ++tick)
    {
        const double u = rng.next01();
        const double f = u < 0.1 ? 0.0 : u < 0.3 ? 1e-12 * rng.next01() : std::pow(10.0, -9.0 + 8.7 * rng.next01());
        const auto mix = Oscillator::morph(static_cast<float>(rng.next01()), static_cast<float>(rng.next01()));
        for (int i = 0; i < 32; ++i)
        {
            const float y = osc.process(f, mix);
            REQUIRE(std::isfinite(y));
            peakOut = std::max(peakOut, std::abs(y));
        }
    }
    CHECK(peakOut < 16.0f);
}

TEST_CASE("fast shape modulation stays near unity level", "[fast]")
{
    // Shape swept by a 20 Hz sine, ramped per sample, at a high pitch: the realistic worst case.
    Oscillator osc;
    osc.prepare(sr);
    float peakOut = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        const double shape = 0.5 + 0.5 * std::sin(2.0 * 3.14159265358979 * 20.0 * i / sr);
        const float y = osc.process(4186.0 / sr, Oscillator::morph(static_cast<float>(shape), 0.5f));
        peakOut = std::max(peakOut, std::abs(y));
    }
    CHECK(peakOut < 2.0f);
}
