// Measuring the round trip (recording.md §22.2): three tries judged, the failures named (RL7).
#include "model/RoundTrip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

TEST_CASE("round trip: three tries that agree give the middle one, and the offset beyond what's reported",
          "[fast]")
{
    // At 48 kHz: 12.4 ms is 595.2 samples; the interface reports 485 (10.1 ms).
    const auto r = judgeRoundTrip({.samples = {595, 596, 595}, .inputPeak = 0.25f}, 485, 48000.0);
    REQUIRE(r.ok());
    CHECK(r.roundTripMs == Approx(595.0 / 48.0));
    CHECK(r.reportedMs == Approx(485.0 / 48.0));
    CHECK(r.offsetMs == Approx(110.0 / 48.0));
    CHECK(r.spreadMs == Approx(1.0 / 48.0));
    CHECK(roundTripText(r) == "Round trip 12.4 ms \xc2\xb7 the interface reports 10.1 ms");
    CHECK(offsetText(r.offsetMs) == "+2.3 ms");
}

TEST_CASE("round trip: the failures, named: too loud first, then nothing back, then tries that disagree",
          "[fast]")
{
    using P = RoundTripResult::Problem;
    // Clipping makes everything else doubtful.
    CHECK(judgeRoundTrip({.samples = {std::nullopt, 600, 600}, .inputPeak = 0.9f}, 0, 48000.0).problem ==
          P::TooLoud);
    CHECK(roundTripText(judgeRoundTrip({.samples = {600}, .inputPeak = 1.0f}, 0, 48000.0)) ==
          "Too loud: turn the input down");
    // Just below -3 dBFS is fine.
    CHECK(judgeRoundTrip({.samples = {600, 600, 600}, .inputPeak = 0.70f}, 0, 48000.0).ok());
    // One try finding nothing is enough to doubt it.
    const auto nothing =
        judgeRoundTrip({.samples = {600, std::nullopt, 600}, .inputPeak = 0.01f}, 0, 48000.0);
    CHECK(nothing.problem == P::NothingCameBack);
    CHECK(roundTripText(nothing) == "Nothing came back: check the cable and the channels");
    CHECK(judgeRoundTrip({}, 0, 48000.0).problem == P::NothingCameBack);
    // 0.4 ms apart at 48 kHz: 19.2 samples.
    const auto apart = judgeRoundTrip({.samples = {600, 619, 610}, .inputPeak = 0.25f}, 0, 48000.0);
    CHECK(apart.problem == P::Disagree);
    CHECK(roundTripText(apart) == "The three tries disagree by 0.4 ms: try again, with nothing else playing");
    // Within a quarter of a millisecond, they agree.
    CHECK(judgeRoundTrip({.samples = {600, 612, 606}, .inputPeak = 0.25f}, 0, 48000.0).ok());
}

TEST_CASE("round trip: an offset as Settings shows it", "[fast]")
{
    CHECK(offsetText(0.0) == "0 ms");
    CHECK(offsetText(0.04) == "0 ms");
    CHECK(offsetText(-1.0) == "-1 ms");
    CHECK(offsetText(2.34) == "+2.3 ms");
    CHECK(offsetText(-0.25) == "-0.3 ms");
    CHECK(offsetText(12.0) == "+12 ms");
}
