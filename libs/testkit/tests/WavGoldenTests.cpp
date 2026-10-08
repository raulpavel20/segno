#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace fd::testkit;
namespace fs = std::filesystem;

namespace
{
fs::path tempPath(const std::string& name)
{
    const auto dir = fs::temp_directory_path() / "fd_testkit_tests";
    fs::create_directories(dir);
    return dir / name;
}
} // namespace

TEST_CASE("WAV round trip is bit exact for 32-bit float", "[fast]")
{
    const auto original = whiteNoise(1000, 3, 0.9f, 2);
    const auto path = tempPath("roundtrip.wav").string();
    REQUIRE(writeWav(path, original, 44100.0));
    const auto loaded = readWav(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->sampleRate == 44100.0);
    CHECK(bitIdentical(original, loaded->buffer));
}

TEST_CASE("reading a missing WAV fails cleanly", "[fast]")
{
    CHECK_FALSE(readWav(tempPath("does-not-exist.wav").string()).has_value());
}

TEST_CASE("golden comparison applies per-metric tolerances", "[fast]")
{
    if (goldenUpdateRequested())
        SKIP("golden update mode would rewrite the fixture");

    const auto path = tempPath("golden.json").string();
    {
        std::ofstream out(path);
        out << R"({ "gain_db": { "value": -6.0, "tol": 0.1 }, "latency": { "value": 0, "tol": 0 } })";
    }

    CHECK(checkGolden(path, {{"gain_db", -6.05}, {"latency", 0}}, 0.0).passed);

    const auto outOfTolerance = checkGolden(path, {{"gain_db", -5.5}, {"latency", 0}}, 0.0);
    CHECK_FALSE(outOfTolerance.passed);
    CHECK(outOfTolerance.report.find("gain_db") != std::string::npos);

    CHECK_FALSE(checkGolden(path, {{"gain_db", -6.0}}, 0.0).passed);                           // missing metric
    CHECK_FALSE(checkGolden(path, {{"gain_db", -6.0}, {"latency", 0}, {"x", 1}}, 0.0).passed); // new metric
    CHECK_FALSE(checkGolden(tempPath("absent.json").string(), {{"a", 1}}, 0.0).passed);
}
