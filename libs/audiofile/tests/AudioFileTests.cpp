#include "fd/audiofile/AudioFile.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace fd::audiofile;

namespace
{
std::string tempPath(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}
} // namespace

TEST_CASE("WAV round trip keeps samples, rate and root key", "[fast]")
{
    std::vector<std::vector<float>> channels(2, std::vector<float>(1000));
    for (int i = 0; i < 1000; ++i)
    {
        channels[0][static_cast<size_t>(i)] = std::sin(0.01f * static_cast<float>(i));
        channels[1][static_cast<size_t>(i)] = -0.5f * channels[0][static_cast<size_t>(i)];
    }
    const auto path = tempPath("fd-audiofile-test.wav");
    REQUIRE(writeWav(path, channels, 44100.0, 57));

    std::string error;
    const auto decoded = decode(path, &error);
    INFO(error);
    REQUIRE(decoded);
    CHECK(decoded->sampleRate == 44100.0);
    CHECK(decoded->numChannels() == 2);
    CHECK(decoded->numFrames() == 1000);
    CHECK(decoded->channels == channels); // 32-bit float: exact
    REQUIRE(decoded->rootKey);
    CHECK(*decoded->rootKey == 57);

    REQUIRE(writeWav(path, channels, 48000.0)); // no root key
    CHECK_FALSE(decode(path)->rootKey);
    std::filesystem::remove(path);
}

TEST_CASE("missing or invalid files fail with a message", "[fast]")
{
    std::string error;
    CHECK_FALSE(decode(tempPath("fd-does-not-exist.wav"), &error));
    CHECK_FALSE(error.empty());

    const auto path = tempPath("fd-audiofile-garbage.bin");
    std::ofstream(path) << "this is not audio at all, not even a little";
    error.clear();
    CHECK_FALSE(decode(path, &error));
    CHECK_FALSE(error.empty());
    std::filesystem::remove(path);
}
