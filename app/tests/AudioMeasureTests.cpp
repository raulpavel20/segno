// Measurements of rendered audio (the MCP render tool): levels, clipping, stereo, spectrum.

#include "model/AudioMeasure.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Catch::Approx;
using fd::model::measureAudio;

namespace
{
std::vector<float> sine(double hz, double amplitude, double seconds, double rate = 48000.0)
{
    std::vector<float> out(static_cast<size_t>(seconds * rate));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    return out;
}

double band(const fd::model::AudioMeasurements& m, const std::string& name)
{
    for (const auto& b : m.bands)
        if (b.name == name)
            return b.levelDb;
    FAIL("no band " << name);
    return 0.0;
}
} // namespace

TEST_CASE("audio measurements: levels of a sine", "[fast]")
{
    const auto s = sine(1000.0, 0.5, 1.0);
    const auto m = measureAudio({s, s}, 48000.0);
    CHECK(m.seconds == Approx(1.0));
    CHECK(m.channels == 2);
    CHECK(m.peakDb == Approx(-6.02).margin(0.01));
    CHECK(m.rmsDb == Approx(-9.03).margin(0.01));
    CHECK(m.crestDb == Approx(3.01).margin(0.02));
    CHECK(m.channelPeakDb.size() == 2);
    CHECK(m.clippedSamples == 0);
    CHECK_FALSE(m.silent);
    REQUIRE(m.correlation);
    CHECK(*m.correlation == Approx(1.0));
    CHECK(band(m, "mids") == Approx(-6.0).margin(0.3)); // relative to a full-scale sine
    CHECK(band(m, "bass") < -50.0);
    CHECK(band(m, "air") < -50.0);
}

TEST_CASE("audio measurements: stereo, clipping, silence", "[fast]")
{
    auto l = sine(100.0, 1.2, 0.5);
    auto r = l;
    for (auto& x : r)
        x = -x;
    const auto inverted = measureAudio({l, r}, 48000.0);
    CHECK(*inverted.correlation == Approx(-1.0));
    CHECK(inverted.clippedSamples > 0);
    CHECK(band(inverted, "bass") > band(inverted, "mids") + 40.0);

    const auto silence = measureAudio({std::vector<float>(1000, 0.0f), std::vector<float>(1000, 0.0f)}, 48000.0);
    CHECK(silence.silent);
    CHECK(std::isinf(silence.peakDb));
    CHECK_FALSE(silence.correlation);
    CHECK(silence.crestDb == 0.0);
    CHECK(std::isinf(band(silence, "mids")));

    const auto mono = measureAudio({sine(50.0, 0.25, 0.2)}, 48000.0);
    CHECK_FALSE(mono.correlation);
    CHECK(band(mono, "sub") == Approx(-12.0).margin(0.5));

    const auto empty = measureAudio({}, 48000.0);
    CHECK(empty.silent);
    CHECK(empty.seconds == 0.0);
}

TEST_CASE("audio measurements: 16-bit WAV in memory", "[fast]")
{
    const auto bytes = fd::model::wav16({{0.0f, 1.0f, -1.0f}, {0.5f, 0.0f, 2.0f}}, 48000.0);
    REQUIRE(bytes.size() == 44 + 3 * 2 * 2);
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF");
    CHECK(std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE");
    CHECK(bytes[22] == 2);    // channels
    CHECK(bytes[24] == 0x80); // 48000 = 0xBB80
    CHECK(bytes[25] == 0xBB);
    CHECK(bytes[34] == 16);   // bits
    const auto sample = [&](size_t i) { return static_cast<std::int16_t>(bytes[44 + i * 2] | (bytes[45 + i * 2] << 8)); };
    CHECK(sample(0) == 0);
    CHECK(sample(1) == 16384);
    CHECK(sample(2) == 32767);
    CHECK(sample(4) == -32767);
    CHECK(sample(5) == 32767); // clamped
}
