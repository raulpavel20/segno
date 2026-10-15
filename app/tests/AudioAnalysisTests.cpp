// What's found in an audio file (M3 step P6b, docs/design/audio-editor.md §7.2–§7.4): its
// transients, found stretch by stretch with none missed or found twice at the joins; a clip's
// transients with those added and removed by hand; what's a loop.

#include "model/AudioAnalysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fd/audiofile/AudioFile.h>
#include <filesystem>

using Catch::Approx;
using namespace fd::model;

TEST_CASE("audio analysis: a long file's transients, none missed or doubled where its stretches join",
          "[fast][audio-analysis]")
{
    constexpr double rate = 22050.0;
    const double seconds = 65.0;
    std::vector<float> mono(static_cast<size_t>(seconds * rate), 0.0f);
    std::vector<double> clicks;
    // A click every 0.5 s, one just before and after each 30 s join.
    for (double t = 0.25; t < seconds - 0.1; t += 0.5)
        clicks.push_back(t);
    clicks.push_back(29.98);
    clicks.push_back(30.02);
    clicks.push_back(59.97);
    std::sort(clicks.begin(), clicks.end());
    for (double t : clicks)
        for (int i = 0; i < 40; ++i)
            mono[static_cast<size_t>(t * rate) + static_cast<size_t>(i)] =
                0.7f * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 2000.0 * i / rate));
    const auto path = (std::filesystem::temp_directory_path() / "fd-analysis-clicks.wav").string();
    REQUIRE(fd::audiofile::writeWav(path, {mono, mono}, rate));
    const auto found = analyseFile(path);
    std::filesystem::remove(path);
    REQUIRE(found);
    CHECK(found->length == Approx(seconds).margin(0.001));
    const auto times = clipTransients(*found, 0.5, {}, {});
    REQUIRE(times.size() == clicks.size());
    for (size_t i = 0; i < clicks.size(); ++i)
    {
        INFO("click " << i);
        CHECK(times[i] == Approx(clicks[i]).margin(0.003));
    }
}

TEST_CASE("audio analysis: a clip's transients, with those added and removed by hand",
          "[fast][audio-analysis]")
{
    FileAnalysis file;
    file.onsets = {{0.5, 0.9f}, {1.0, 0.9f}, {1.5, 0.05f}, {2.0, 0.9f}};
    CHECK(clipTransients(file, 0.5, {}, {}) == std::vector<double>{0.5, 1.0, 2.0});
    CHECK(clipTransients(file, 1.0, {}, {}) == std::vector<double>{0.5, 1.0, 1.5, 2.0});
    CHECK(clipTransients(file, 0.5, {0.75, 1.0004}, {2.0005}) == std::vector<double>{0.5, 0.75, 1.0});
}

TEST_CASE("audio analysis: a loop is a whole number of bars at its tempo", "[fast][audio-analysis]")
{
    CHECK(isLoopAt(4.0, 120.0));         // 2 bars
    CHECK(isLoopAt(240.0 / 92.0, 92.0)); // 1 bar
    CHECK(isLoopAt(8.02, 120.0));        // 4 bars, within 1 %
    CHECK_FALSE(isLoopAt(6.0, 120.0));   // 3 bars
    CHECK_FALSE(isLoopAt(1.0, 120.0));   // half a bar
    CHECK_FALSE(isLoopAt(4.0, 0.0));
    FileAnalysis drums;
    drums.percussiveness = 0.8;
    CHECK(startsInBeats(drums));
    CHECK_FALSE(startsInBeats(FileAnalysis{}));
}
