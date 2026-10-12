// Musical time as text (docs/design/mcp.md §3, clip-loops.md §7): what the inspector, the value tags
// and the assistants' tools read and write.
#include "model/MusicalTime.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

TEST_CASE("musical time: lengths and positions as text, read back as written", "[fast]")
{
    const TempoMap tempo; // 4/4
    CHECK(lengthText(4.0, tempo) == "1 bar");
    CHECK(lengthText(16.0, tempo) == "4 bars");
    CHECK(lengthText(6.0, tempo) == "1 bar 2 beats");
    CHECK(lengthText(1.0, tempo) == "1 beat");
    CHECK(lengthText(1.5, tempo) == "1.5 beats");
    CHECK(positionText(0.0, tempo) == "1.1.1");
    CHECK(positionText(5.25, tempo) == "2.2.2");
    for (const double beats : {0.25, 1.0, 1.5, 4.0, 6.0, 16.0})
        CHECK(*lengthFromText(lengthText(beats, tempo), tempo) == Approx(beats));
    for (const double beats : {0.0, 1.0, 5.25, 17.0})
        CHECK(*positionFromText(positionText(beats, tempo), tempo) == Approx(beats));
    CHECK(*lengthFromText("2 bars", tempo) == Approx(8.0));
    CHECK(*lengthFromText("1/16", tempo) == Approx(0.25));
    CHECK(*lengthFromText("1.0.0", tempo) == Approx(4.0));
    CHECK(*lengthFromText("3", tempo) == Approx(3.0));
    CHECK_FALSE(lengthFromText("0", tempo));
    CHECK_FALSE(lengthFromText("soon", tempo));
    CHECK(*positionFromText("5", tempo) == Approx(16.0));
    CHECK(*positionFromText("2 bars", tempo) == Approx(8.0));
    // 6/8: a bar is three quarter notes; beats in positions are the signature's eighths.
    const TempoMap sixEight{.numerator = 6, .denominator = 8};
    CHECK(lengthText(3.0, sixEight) == "1 bar");
    CHECK(*positionFromText("1.2.1", sixEight) == Approx(0.5));
}
