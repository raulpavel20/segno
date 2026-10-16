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

namespace
{
constexpr double kKeyRate = 48000.0;

/// Chords a second each (MIDI pitches), as tones with five harmonics.
std::vector<float> chordAudio(const std::vector<std::vector<int>>& progression)
{
    std::vector<float> out(static_cast<size_t>(progression.size() * kKeyRate), 0.0f);
    for (size_t k = 0; k < progression.size(); ++k)
        for (const int pitch : progression[k])
            for (int h = 1; h <= 5; ++h)
            {
                const double f = 440.0 * std::pow(2.0, (pitch - 69) / 12.0) * h;
                for (size_t i = 0; i < static_cast<size_t>(kKeyRate); ++i)
                {
                    const double t = static_cast<double>(i) / kKeyRate;
                    const double env = std::min(1.0, t / 0.01) * std::min(1.0, (1.0 - t) / 0.01);
                    out[k * static_cast<size_t>(kKeyRate) + i] +=
                        static_cast<float>(0.08 / h * env * std::sin(2.0 * 3.141592653589793 * f * t));
                }
            }
    return out;
}

std::optional<FileAnalysis> analysed(const std::vector<float>& mono, const char* name)
{
    const auto path = (std::filesystem::temp_directory_path() / name).string();
    REQUIRE(fd::audiofile::writeWav(path, {mono, mono}, kKeyRate));
    auto found = analyseFile(path);
    std::filesystem::remove(path);
    return found;
}
} // namespace

TEST_CASE("audio analysis: the key of chord progressions; none for drums, noise or a held note",
          "[fast][audio-analysis]")
{
    using fd::music::Key;
    // I–IV–V–I in C major, with the scale's other chords.
    const auto cMajor = analysed(chordAudio({{48, 60, 64, 67},
                                             {41, 60, 65, 69},
                                             {43, 59, 62, 67},
                                             {45, 60, 64, 69},
                                             {38, 62, 65, 69},
                                             {43, 59, 62, 67},
                                             {48, 60, 64, 67},
                                             {48, 60, 64, 67}}),
                                 "fd-key-c-major.wav");
    REQUIRE(cMajor);
    CHECK(cMajor->pitchClasses.tonal > 0.8);
    CHECK(cMajor->key == Key{0, "major"});
    // i–iv–V–i in A minor, the raised seventh (G♯) in its V.
    const auto aMinor = analysed(chordAudio({{45, 57, 60, 64},
                                             {38, 57, 62, 65},
                                             {40, 56, 59, 64},
                                             {45, 57, 60, 64},
                                             {45, 57, 60, 64},
                                             {38, 57, 62, 65},
                                             {40, 56, 59, 64},
                                             {45, 57, 60, 69}}),
                                 "fd-key-a-minor.wav");
    REQUIRE(aMinor);
    CHECK(aMinor->key == Key{9, "minor"});
    // F♯ major, to be sure it isn't C's.
    const auto fSharp = analysed(chordAudio({{42, 54, 58, 61},
                                             {47, 59, 63, 66},
                                             {49, 56, 61, 65},
                                             {42, 54, 58, 61},
                                             {47, 59, 63, 66},
                                             {49, 61, 65, 68},
                                             {42, 54, 58, 61},
                                             {42, 54, 58, 61}}),
                                 "fd-key-f-sharp.wav");
    REQUIRE(fSharp);
    CHECK(fSharp->key == Key{6, "major"});

    // A held A: A major and A minor alike.
    std::vector<float> held(static_cast<size_t>(4 * kKeyRate));
    for (size_t i = 0; i < held.size(); ++i)
        held[i] = 0.3f * static_cast<float>(
                             std::sin(2.0 * 3.141592653589793 * 220.0 * static_cast<double>(i) / kKeyRate));
    const auto tone = analysed(held, "fd-key-held.wav");
    REQUIRE(tone);
    CHECK_FALSE(tone->key);
    // Drums: a kick and noise bursts on every beat.
    std::vector<float> drums(static_cast<size_t>(4 * kKeyRate), 0.0f);
    std::uint32_t x = 5;
    for (int beat = 0; beat < 8; ++beat)
        for (size_t i = 0; i < static_cast<size_t>(0.2 * kKeyRate); ++i)
        {
            const double t = static_cast<double>(i) / kKeyRate;
            x = x * 1664525u + 1013904223u;
            const double hit =
                std::exp(-t * 25.0) *
                (0.5 * std::sin(2.0 * 3.141592653589793 * (55.0 + 90.0 * std::exp(-t * 30.0)) * t) +
                 0.4 * (static_cast<double>(x >> 8) / 8388608.0 - 1.0));
            drums[static_cast<size_t>(beat * 0.5 * kKeyRate) + i] += static_cast<float>(hit);
        }
    const auto beat = analysed(drums, "fd-key-drums.wav");
    REQUIRE(beat);
    INFO("tonal " << beat->pitchClasses.tonal);
    CHECK_FALSE(beat->key);
}

TEST_CASE("audio analysis: a file's key from its metadata, its name, then its sound",
          "[fast][audio-analysis]")
{
    using fd::music::Key;
    fd::dsp::PitchClasses aMinor;
    aMinor.weights = {0.12, 0.0, 0.08, 0.0, 0.17, 0.1, 0.0, 0.05, 0.03, 0.3, 0.0, 0.15};
    aMinor.tonal = 0.9;
    REQUIRE(keyOfSound(aMinor) == Key{9, "minor"});
    // The metadata's root, in the mode the sound fits better there.
    CHECK(fileKey(9, std::nullopt, &aMinor) == Key{9, "minor"});
    CHECK(fileKey(0, std::nullopt, &aMinor) == Key{0, "major"});
    // The name's mode where it names the same root.
    CHECK(fileKey(9, Key{9, "major"}, &aMinor) == Key{9, "major"});
    // A root alone with nothing to say its mode: major.
    CHECK(fileKey(2, std::nullopt, nullptr) == Key{2, "major"});
    // No root: the name, then the sound.
    CHECK(fileKey(std::nullopt, Key{4, "minor"}, &aMinor) == Key{4, "minor"});
    CHECK(fileKey(std::nullopt, std::nullopt, &aMinor) == Key{9, "minor"});
    auto noise = aMinor;
    noise.tonal = 0.3;
    CHECK_FALSE(fileKey(std::nullopt, std::nullopt, &noise));
    CHECK_FALSE(fileKey(std::nullopt, std::nullopt, nullptr));
}
