#include "fd/dsp/Oscillator.h"
#include "fd/testkit/Metrics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
