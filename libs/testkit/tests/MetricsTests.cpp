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
