#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::testkit;
using Catch::Approx;

TEST_CASE("sine has the requested amplitude and channel count", "[fast]")
{
    const auto b = sine(1000.0, 48000.0, 4800, 0.5f, 2);
    REQUIRE(b.numChannels() == 2);
    REQUIRE(b.numSamples() == 4800);
    CHECK(peak(b) == Approx(0.5).margin(1e-4));
}

TEST_CASE("stereoSines differ between channels", "[fast]")
{
    const auto b = stereoSines(440.0, 660.0, 48000.0, 4800);
    CHECK(nullResidualDb(b.slice(0, 4800), b.slice(0, 4800)) == silenceDb);
    bool differ = false;
    for (int i = 0; i < b.numSamples(); ++i)
        differ |= b.at(0, i) != b.at(1, i);
    CHECK(differ);
}

TEST_CASE("noise is deterministic per seed and bounded", "[fast]")
{
    const auto a = whiteNoise(10000, 42);
    const auto b = whiteNoise(10000, 42);
    const auto c = whiteNoise(10000, 43);
    CHECK(bitIdentical(a, b));
    CHECK_FALSE(bitIdentical(a, c));
    CHECK(peak(a) <= 0.25);
    CHECK(std::abs(dcOffset(a, 0)) < 0.01);
}

TEST_CASE("impulse is a single sample at the requested position", "[fast]")
{
    const auto b = impulse(64, 1, 1.0f, 10);
    for (int i = 0; i < 64; ++i)
        CHECK(b.at(0, i) == (i == 10 ? 1.0f : 0.0f));
}

TEST_CASE("log sweep is finite and at the requested level", "[fast]")
{
    const auto b = logSweep(20.0, 20000.0, 48000.0, 48000, 0.5f);
    CHECK(allFinite(b));
    CHECK(peak(b) == Approx(0.5).margin(1e-3));
}
