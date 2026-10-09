// The conventions every MCP tool follows (docs/design/mcp.md §3): ids, musical time, note names.

#include "mcp/Conventions.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace fd::mcp;

TEST_CASE("mcp conventions: typed ids", "[fast][mcp]")
{
    CHECK(formatId(IdKind::Track, 12) == "t:12");
    CHECK(formatId(IdKind::Clip, 7) == "c:7");
    CHECK(formatId(IdKind::Module, 3) == "m:3");
    CHECK(parseId("t:12", IdKind::Track) == 12u);
    CHECK(parseId(" c:7 ", IdKind::Clip) == 7u);
    CHECK(parseId("12", IdKind::Track) == 12u);
    CHECK(parseId(12, IdKind::Track) == 12u);
    CHECK_FALSE(parseId("c:12", IdKind::Track)); // another kind
    CHECK_FALSE(parseId("t:", IdKind::Track));
    CHECK_FALSE(parseId("t:x", IdKind::Track));
    CHECK_FALSE(parseId("t:0", IdKind::Track));
    CHECK_FALSE(parseId(-3, IdKind::Track));
    CHECK_FALSE(parseId(true, IdKind::Track));
}

TEST_CASE("mcp conventions: positions and lengths in beats", "[fast][mcp]")
{
    const fd::model::TempoMap fourFour{120.0, 4, 4};
    const fd::model::TempoMap sixEight{120.0, 6, 8};
    CHECK(parsePosition(16.5, fourFour) == Approx(16.5));
    CHECK(parsePosition("1.1.1", fourFour) == Approx(0.0));
    CHECK(parsePosition("5.1.1", fourFour) == Approx(16.0));
    CHECK(parsePosition("5", fourFour) == Approx(16.0));
    CHECK(parsePosition("2.3", fourFour) == Approx(6.0));
    CHECK(parsePosition("2.1.3", fourFour) == Approx(4.5));
    CHECK(parsePosition("2.2.1", sixEight) == Approx(3.5)); // a bar of 6/8 is 3 quarters; a beat an eighth
    CHECK(parsePosition("4 bars", fourFour) == Approx(16.0));
    CHECK(parsePosition("4 Bars", sixEight) == Approx(12.0));
    std::string error;
    CHECK_FALSE(parsePosition("0.1.1", fourFour, &error));
    CHECK(error.find("5.1.1") != std::string::npos);
    CHECK_FALSE(parsePosition(-1, fourFour));
    CHECK_FALSE(parsePosition("soon", fourFour));
    CHECK_FALSE(parsePosition(Json::array(), fourFour));

    CHECK(parseLength(2, fourFour) == Approx(2.0));
    CHECK(parseLength("2 bars", fourFour) == Approx(8.0));
    CHECK(parseLength("1 bar", sixEight) == Approx(3.0));
    CHECK(parseLength("3 beats", fourFour) == Approx(3.0));
    CHECK(parseLength("2 sixteenths", fourFour) == Approx(0.5));
    CHECK(parseLength("1/16", fourFour) == Approx(0.25));
    CHECK(parseLength("3/8", fourFour) == Approx(1.5));
    CHECK(parseLength("1.0.0", fourFour) == Approx(4.0));
    CHECK(parseLength("0.2.0", fourFour) == Approx(2.0));
    CHECK(parseLength("1.5", fourFour) == Approx(1.5)); // a number of beats, not bars and beats
    CHECK_FALSE(parseLength(0, fourFour));
    CHECK_FALSE(parseLength("0 bars", fourFour));
    CHECK_FALSE(parseLength("1/0", fourFour));
}

TEST_CASE("mcp conventions: bar.beat.sixteenth", "[fast][mcp]")
{
    const fd::model::TempoMap fourFour{120.0, 4, 4};
    CHECK(formatPosition(0.0, fourFour) == "1.1.1");
    CHECK(formatPosition(16.0, fourFour) == "5.1.1");
    CHECK(formatPosition(4.75, fourFour) == "2.1.4");
    CHECK(formatPosition(4.875, fourFour) == "2.1.4.50");
    CHECK(formatPosition(3.5, {120.0, 6, 8}) == "2.2.1");
    for (double b : {0.0, 1.25, 7.5, 33.0})
        CHECK(parsePosition(formatPosition(b, fourFour), fourFour) == Approx(b));
    const auto j = positionJson(16.0, fourFour);
    CHECK(j["beats"] == 16.0);
    CHECK(j["bbt"] == "5.1.1");
}

TEST_CASE("mcp conventions: pitches with C3 = 60, velocities 1-127", "[fast][mcp]")
{
    CHECK(parsePitch(60) == 60);
    CHECK(parsePitch("C3") == 60);
    CHECK(parsePitch("c3") == 60);
    CHECK(parsePitch("D#4") == 75);
    CHECK(parsePitch("Eb2") == 51);
    CHECK(parsePitch("C-2") == 0);
    CHECK(parsePitch("G8") == 127);
    CHECK(parsePitch("64") == 64);
    CHECK_FALSE(parsePitch("G#8"));
    CHECK_FALSE(parsePitch(128));
    CHECK_FALSE(parsePitch("H2"));
    CHECK_FALSE(parsePitch("C"));
    CHECK_FALSE(parsePitch(60.5));
    CHECK(pitchName(60) == "C3");
    CHECK(pitchName(61) == "C#3");
    CHECK(pitchName(0) == "C-2");
    for (int p = 0; p < 128; ++p)
        CHECK(parsePitch(pitchName(p)) == p);

    CHECK(velocityToMidi(1.0f) == 127);
    CHECK(velocityToMidi(0.0f) == 1);
    CHECK(velocityFromMidi(127) == Approx(1.0));
    CHECK(velocityToMidi(velocityFromMidi(100)) == 100);
    CHECK(velocityToMidi(velocityFromMidi(300)) == 127);
}
