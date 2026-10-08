#include "fd/device/Param.h"
#include "fd/device/ParamStore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace fd;
using Catch::Approx;

namespace
{
constexpr std::string_view kModes[] = {"Clean", "Warm", "Hot"};

constexpr ParamSpec kGain{.id = "gain", .name = "Gain", .min = -70.0f, .max = 35.0f, .def = 0.0f,
                          .skewCentre = 0.0f, .unit = Unit::Decibels, .flags = ParamFlags::Automatable | ParamFlags::MinusInfinityAtMin};
constexpr ParamSpec kFreq{.id = "freq", .name = "Frequency", .min = 20.0f, .max = 20000.0f, .def = 1000.0f,
                          .skewCentre = 632.0f, .unit = Unit::Hertz, .decimals = 0};
constexpr ParamSpec kPan{.id = "pan", .name = "Pan", .min = -1.0f, .max = 1.0f, .def = 0.0f, .unit = Unit::Pan};
constexpr ParamSpec kMode{.id = "mode", .name = "Mode", .kind = ParamKind::Choice, .min = 0.0f, .max = 2.0f, .def = 0.0f,
                          .choices = kModes};
constexpr ParamSpec kOn{.id = "on", .name = "On", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f, .def = 1.0f};
} // namespace

TEST_CASE("skewed range puts the centre value at normalised 0.5", "[fast]")
{
    CHECK(kGain.toNormalized(0.0f) == Approx(0.5f).margin(1e-6));
    CHECK(kGain.fromNormalized(0.5f) == Approx(0.0f).margin(1e-4));
    CHECK(kFreq.fromNormalized(0.5f) == Approx(632.0f).epsilon(1e-4));
    CHECK(kGain.fromNormalized(0.0f) == -70.0f);
    CHECK(kGain.fromNormalized(1.0f) == 35.0f);
}

TEST_CASE("normalised round trip", "[fast]")
{
    for (float v : {-70.0f, -24.0f, -0.5f, 0.0f, 12.0f, 35.0f})
        CHECK(kGain.fromNormalized(kGain.toNormalized(v)) == Approx(v).margin(1e-3));
    for (float v : {20.0f, 100.0f, 1000.0f, 19999.0f})
        CHECK(kFreq.fromNormalized(kFreq.toNormalized(v)) == Approx(v).epsilon(1e-4));
}

TEST_CASE("sanitize clamps, rounds stepped kinds and rejects NaN", "[fast]")
{
    CHECK(kGain.sanitize(100.0f) == 35.0f);
    CHECK(kMode.sanitize(1.4f) == 1.0f);
    CHECK(kMode.sanitize(7.0f) == 2.0f);
    CHECK(kOn.sanitize(0.6f) == 1.0f);
    CHECK(kGain.sanitize(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(kMode.numSteps() == 3);
    CHECK(kOn.numSteps() == 2);
}

TEST_CASE("display text", "[fast]")
{
    CHECK(kGain.toText(-6.0f) == "-6.0 dB");
    CHECK(kGain.toText(-70.0f) == "-inf dB");
    CHECK(kGain.toText(-0.01f) == "0.0 dB");
    CHECK(kFreq.toText(440.0f) == "440 Hz");
    CHECK(kFreq.toText(1200.0f) == "1.20 kHz");
    CHECK(kPan.toText(0.0f) == "C");
    CHECK(kPan.toText(-0.5f) == "25L");
    CHECK(kPan.toText(1.0f) == "50R");
    CHECK(kMode.toText(1.0f) == "Warm");
    CHECK(kOn.toText(1.0f) == "On");
}

TEST_CASE("text parsing", "[fast]")
{
    CHECK(kGain.fromText("-6") == -6.0f);
    CHECK(kGain.fromText(" -6 dB ") == -6.0f);
    CHECK(kGain.fromText("-inf") == -70.0f);
    CHECK(kGain.fromText("+50") == 35.0f);
    CHECK(kFreq.fromText("1.2k") == Approx(1200.0f));
    CHECK(kFreq.fromText("440 Hz") == 440.0f);
    CHECK(kPan.fromText("C") == 0.0f);
    CHECK(kPan.fromText("25L") == Approx(-0.5f));
    CHECK(kPan.fromText("50R") == 1.0f);
    CHECK(kMode.fromText("hot") == 2.0f);
    CHECK(kOn.fromText("off") == 0.0f);
    CHECK_FALSE(kGain.fromText("loud").has_value());
    CHECK_FALSE(kOn.fromText("maybe").has_value());
}

TEST_CASE("param store holds sanitised plain values", "[fast]")
{
    static constexpr std::array specs{kGain, kMode, kOn};
    ParamStore store(specs);
    CHECK(store.get(0) == 0.0f);
    CHECK(store.getBool(2));
    store.set(1, 5.0f);
    CHECK(store.getInt(1) == 2);
    store.setNormalized(0, 0.5f);
    CHECK(store.get(0) == Approx(0.0f).margin(1e-4));
    CHECK(store.indexOf("mode") == 1u);
    CHECK_FALSE(store.indexOf("nope").has_value());
    store.resetToDefaults();
    CHECK(store.getInt(1) == 0);
}
