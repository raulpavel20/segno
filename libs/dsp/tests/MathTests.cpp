#include "fd/dsp/Math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::dsp;
using Catch::Approx;

TEST_CASE("dB and gain conversions", "[fast]")
{
    CHECK(dbToGain(0.0f) == 1.0f);
    CHECK(dbToGain(-6.0206f) == Approx(0.5f).margin(1e-5));
    CHECK(dbToGain(20.0f) == Approx(10.0f));
    CHECK(dbToGain(minusInfinityDb) == 0.0f);
    CHECK(dbToGain(-70.0f, -70.0f) == 0.0f);

    CHECK(gainToDb(1.0f) == Approx(0.0f).margin(1e-6));
    CHECK(gainToDb(0.5f) == Approx(-6.0206f).margin(1e-4));
    CHECK(gainToDb(0.0f) == minusInfinityDb);
}

TEST_CASE("milliseconds to samples", "[fast]")
{
    CHECK(msToSamples(20.0, 48000.0) == 960);
    CHECK(msToSamples(0.0, 48000.0) == 0);
}
