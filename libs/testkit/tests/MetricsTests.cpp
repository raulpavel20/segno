#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace fd::testkit;
using Catch::Approx;

TEST_CASE("peak and RMS of a sine", "[fast]")
{
    const auto b = sine(1000.0, 48000.0, 48000, 0.5f, 1);
    CHECK(peakDb(b) == Approx(-6.0206).margin(0.01));
    CHECK(rms(b) == Approx(0.5 / std::sqrt(2.0)).margin(1e-4));
}

TEST_CASE("null residual reports the size of the difference", "[fast]")
{
    const auto a = whiteNoise(4096, 1);
    auto b = a;
    CHECK(nullResidualDb(a, b) == silenceDb);
    b.at(1, 100) += 0.001f; // -60 dBFS
    CHECK(nullResidualDb(a, b) == Approx(-60.0).margin(0.1));
}

TEST_CASE("latency estimate finds a known delay", "[fast]")
{
    const auto ref = whiteNoise(8192, 7);
    AudioBuffer delayed(2, 8192);
    for (int c = 0; c < 2; ++c)
        for (int i = 37; i < 8192; ++i)
            delayed.at(c, i) = ref.at(c, i - 37);
    CHECK(estimateLatency(ref, delayed, 256) == 37);
    CHECK(estimateLatency(ref, ref, 256) == 0);
}

TEST_CASE("click score separates smooth signals from discontinuities", "[fast]")
{
    auto smooth = sine(100.0, 48000.0, 48000, 0.5f, 1);
    CHECK(clickScore(smooth, 0) < 2.0);

    // Same sine with an instant 1 dB gain step at a peak (sample 24120 = a quarter period
    // after a zero crossing): a click.
    auto clicked = smooth;
    for (int i = 24120; i < 48000; ++i)
        clicked.at(0, i) *= 1.122f;
    CHECK(clickScore(clicked, 0) > 50.0);

    // The same gain change ramped over 20 ms: no click.
    auto ramped = smooth;
    for (int i = 24120; i < 48000; ++i)
    {
        const float t = std::min(1.0f, static_cast<float>(i - 24120) / 960.0f);
        ramped.at(0, i) *= 1.0f + 0.122f * t;
    }
    CHECK(clickScore(ramped, 0) < 2.0);
}

TEST_CASE("FFT magnitude spectrum finds a bin-centred sine", "[fast]")
{
    constexpr int size = 4096;
    constexpr double sr = 48000.0;
    const double binHz = sr / size;
    const auto b = sine(100 * binHz, sr, size, 0.5f, 1);
    const auto mags = magnitudeSpectrum(b, 0, size);
    const auto peakBin = std::distance(mags.begin(), std::max_element(mags.begin(), mags.end()));
    CHECK(peakBin == 100);
    CHECK(mags[100] == Approx(0.5).margin(0.01));
}

TEST_CASE("response of a unit impulse is flat", "[fast]")
{
    const auto b = impulse(16, 1);
    for (double f : {20.0, 1000.0, 15000.0})
        CHECK(std::abs(responseAt(b.channel(0), 16, f, 48000.0)) == Approx(1.0));
}

TEST_CASE("finite and subnormal checks", "[fast]")
{
    AudioBuffer b(1, 4);
    CHECK(allFinite(b));
    CHECK(countSubnormals(b) == 0);
    b.at(0, 1) = std::numeric_limits<float>::denorm_min();
    CHECK(countSubnormals(b) == 1);
    b.at(0, 2) = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(allFinite(b));
}

TEST_CASE("harmonic levels and THD are exact for bin-centred tones", "[fast]")
{
    constexpr int size = 1 << 14;
    constexpr int cycles = 101;
    const double f0 = binCentredFrequency(cycles, size, 48000.0);
    // x = A sin + a A^3 sin^3  ->  3rd harmonic amplitude = a A^3 / 4, fundamental A + 3aA^3/4
    AudioBuffer b(1, size);
    const double A = 0.5, a = 0.2;
    for (int i = 0; i < size; ++i)
    {
        const double x = A * std::sin(2.0 * 3.141592653589793 * f0 * i / 48000.0);
        b.at(0, i) = static_cast<float>(x + a * x * x * x);
    }
    const auto h = harmonicLevelsDb(b, 0, 0, size, cycles, 5);
    CHECK(h[0] == Approx(toDb(A + 3.0 * a * A * A * A / 4.0)).margin(0.01));
    CHECK(h[2] == Approx(toDb(a * A * A * A / 4.0)).margin(0.01));
    CHECK(h[1] < -120.0); // no even harmonics
    CHECK(thdPercent(b, 0, 0, size, cycles) == Approx(100.0 * (a * A * A * A / 4.0) / (A + 3.0 * a * A * A * A / 4.0)).epsilon(1e-3));
}

TEST_CASE("aliasing ratio separates naive and band-limited sawtooths", "[fast]")
{
    constexpr int size = 1 << 14;
    constexpr int cycles = 711; // ~2.08 kHz at 48 kHz (around C7)
    const double f0 = binCentredFrequency(cycles, size, 48000.0);

    AudioBuffer naive(1, size), bandLimited(1, size);
    for (int i = 0; i < size; ++i)
    {
        const double phase = std::fmod(f0 * i / 48000.0, 1.0);
        naive.at(0, i) = static_cast<float>(2.0 * phase - 1.0);
        double sum = 0.0; // additive saw: harmonics below Nyquist only
        for (int k = 1; k * f0 < 24000.0; ++k)
            sum += std::sin(2.0 * 3.141592653589793 * k * f0 * i / 48000.0) / k;
        bandLimited.at(0, i) = static_cast<float>(-2.0 / 3.141592653589793 * sum);
    }
    CHECK(aliasingRatioDb(naive, 0, 0, size, cycles) > -40.0);
    CHECK(aliasingRatioDb(bandLimited, 0, 0, size, cycles) < -100.0);
}
