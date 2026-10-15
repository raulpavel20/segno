// How much of each pitch class sounds (the clip's key, docs/design/audio-editor.md §8.2, §19): a
// chord progression's notes stand out from the rest, the tuning is read, and noise, drums and
// silence aren't pitched.

#include "fd/dsp/PitchClasses.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;
using namespace fd::dsp;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.141592653589793;

double hz(int pitch, double a4 = 440.0)
{
    return a4 * std::pow(2.0, (pitch - 69) / 12.0);
}

/// Chords, each a second long: MIDI pitches played as tones with five harmonics.
std::vector<float> chords(const std::vector<std::vector<int>>& progression, double a4 = 440.0)
{
    std::vector<float> out(static_cast<size_t>(progression.size() * kRate), 0.0f);
    for (size_t k = 0; k < progression.size(); ++k)
        for (const int pitch : progression[k])
            for (int h = 1; h <= 5; ++h)
            {
                const double f = hz(pitch, a4) * h;
                for (size_t i = 0; i < static_cast<size_t>(kRate); ++i)
                {
                    const double t = static_cast<double>(i) / kRate;
                    const double env = std::min(1.0, t / 0.01) * std::min(1.0, (1.0 - t) / 0.01);
                    out[k * static_cast<size_t>(kRate) + i] +=
                        static_cast<float>(0.1 / h * env * std::sin(2.0 * kPi * f * t));
                }
            }
    return out;
}

std::vector<float> noise(double seconds)
{
    std::vector<float> out(static_cast<size_t>(seconds * kRate));
    std::uint32_t x = 99;
    for (auto& s : out)
    {
        x = x * 1664525u + 1013904223u;
        s = 0.3f * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

bool inCMajor(int pc)
{
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
}
} // namespace

TEST_CASE("pitch classes: a chord progression's notes stand out; the profile sums to 1", "[fast][key]")
{
    // C, F, G, Am, Dm, G, Em, C: every note of C major, in octaves from 36 up.
    const auto audio = chords({{48, 60, 64, 67},
                               {41, 60, 65, 69},
                               {43, 59, 62, 67},
                               {45, 60, 64, 69},
                               {38, 62, 65, 69},
                               {43, 59, 62, 67},
                               {40, 59, 64, 67},
                               {36, 60, 64, 67}});
    const auto p = pitchClasses(audio, kRate);
    double sum = 0.0, in = 0.0, outside = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        sum += p.weights[static_cast<size_t>(pc)];
        (inCMajor(pc) ? in : outside) += p.weights[static_cast<size_t>(pc)];
    }
    CHECK(sum == Approx(1.0));
    CHECK(in > 0.9);
    for (int pc = 0; pc < 12; ++pc)
        if (!inCMajor(pc))
        {
            INFO("pitch class " << pc);
            CHECK(p.weights[static_cast<size_t>(pc)] < 0.03);
        }
    // C and G, in every chord's bass or on top, weigh most.
    const auto most = std::max_element(p.weights.begin(), p.weights.end()) - p.weights.begin();
    CHECK((most == 0 || most == 7));
    CHECK(p.tonal > 0.8);
    CHECK(p.tuningCents == Approx(0.0));
}

TEST_CASE("pitch classes: the tuning is read, and the notes are still themselves", "[fast][key]")
{
    // A minor at A = 432 Hz: 32 cents flat.
    const auto audio =
        chords({{45, 57, 60, 64}, {50, 57, 62, 65}, {52, 56, 59, 64}, {45, 57, 60, 64}}, 432.0);
    const auto p = pitchClasses(audio, kRate);
    CHECK(p.tuningCents == Approx(-30.0));
    const auto most = std::max_element(p.weights.begin(), p.weights.end()) - p.weights.begin();
    CHECK(most == 9); // A
    CHECK(p.weights[1] < 0.03);
    CHECK(p.weights[10] < 0.03);
    CHECK(p.tonal > 0.8);
}

TEST_CASE("pitch classes: noise isn't pitched, and silence has nothing", "[fast][key]")
{
    CHECK(pitchClasses(noise(4.0), kRate).tonal < 0.45);
    const auto none = pitchClasses(std::vector<float>(48000, 0.0f), kRate);
    CHECK(none.tonal == 0.0);
    CHECK(std::all_of(none.weights.begin(), none.weights.end(), [](double w) { return w == 0.0; }));
}

TEST_CASE("pitch classes: counted stretch by stretch, a file reads as a whole", "[fast][key]")
{
    const auto audio = chords({{48, 60, 64, 67}, {41, 60, 65, 69}, {43, 59, 62, 67}, {48, 60, 64, 67}});
    PitchClassCounter counter;
    const size_t half = audio.size() / 2;
    counter.add(std::span<const float>(audio.data(), half), kRate);
    counter.add(std::span<const float>(audio.data() + half, audio.size() - half), kRate);
    const auto whole = pitchClasses(audio, kRate), streamed = counter.result();
    for (int pc = 0; pc < 12; ++pc)
        CHECK(streamed.weights[static_cast<size_t>(pc)] ==
              Approx(whole.weights[static_cast<size_t>(pc)]).margin(0.02));
}
