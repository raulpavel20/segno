#include "fd/device/Device.h"
#include "fd/device/State.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace fd;

namespace
{
constexpr std::array kParams{
    ParamSpec{.id = "gain", .name = "Gain", .min = -24.0f, .max = 24.0f, .def = 0.0f, .unit = Unit::Decibels},
    ParamSpec{.id = "mix", .name = "Mix", .min = 0.0f, .max = 100.0f, .def = 100.0f, .unit = Unit::Percent},
};
constexpr DeviceInfo kInfo{.id = "test-device", .name = "Test", .params = kParams, .stateVersion = 2};

/// Version 1 stored "level" instead of "gain".
class TestDevice final : public Device
{
public:
    TestDevice() : Device(kInfo) {}
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(ProcessContext&) noexcept override {}
    void writeExtraState(nlohmann::json& extra) const override { extra["sample"] = samplePath; }
    void readExtraState(const nlohmann::json& extra) override { samplePath = extra.value("sample", ""); }
    void migrateState(DeviceState& s) const override
    {
        if (s.stateVersion < 2 && s.params.contains("level"))
        {
            s.params["gain"] = s.params["level"];
            s.params.erase("level");
        }
    }
    std::string samplePath;
};
} // namespace

TEST_CASE("state round trip through JSON", "[fast]")
{
    TestDevice a;
    a.params().set(0, -6.0f);
    a.params().set(1, 50.0f);
    a.samplePath = "kick.wav";

    TestDevice b;
    REQUIRE(deserializeState(b, serializeState(a)) == ApplyResult::Ok);
    CHECK(b.params().get(0) == -6.0f);
    CHECK(b.params().get(1) == 50.0f);
    CHECK(b.samplePath == "kick.wav");
}

TEST_CASE("missing parameters reset to default; unknown ones are ignored", "[fast]")
{
    TestDevice d;
    d.params().set(1, 10.0f);
    REQUIRE(deserializeState(d, R"({"device":"test-device","stateVersion":2,"params":{"gain":3,"bogus":1}})") ==
            ApplyResult::Ok);
    CHECK(d.params().get(0) == 3.0f);
    CHECK(d.params().get(1) == 100.0f);
}

TEST_CASE("older state is migrated before applying", "[fast]")
{
    TestDevice d;
    REQUIRE(deserializeState(d, R"({"device":"test-device","stateVersion":1,"params":{"level":-12}})") ==
            ApplyResult::Ok);
    CHECK(d.params().get(0) == -12.0f);
}

TEST_CASE("wrong device, newer version and garbage are rejected", "[fast]")
{
    TestDevice d;
    d.params().set(0, 5.0f);
    CHECK(deserializeState(d, R"({"device":"other","params":{}})") == ApplyResult::WrongDevice);
    CHECK(deserializeState(d, R"({"device":"test-device","stateVersion":9,"params":{}})") == ApplyResult::NewerVersion);
    CHECK(deserializeState(d, "not json") == ApplyResult::Invalid);
    CHECK(deserializeState(d, R"([1,2,3])") == ApplyResult::Invalid);
    CHECK(d.params().get(0) == 5.0f); // untouched
}

TEST_CASE("out-of-range saved values are clamped", "[fast]")
{
    TestDevice d;
    REQUIRE(deserializeState(d, R"({"device":"test-device","stateVersion":2,"params":{"gain":99}})") == ApplyResult::Ok);
    CHECK(d.params().get(0) == 24.0f);
}
