// Where sounds start (the audio editor's transients, tempo and mode, docs/design/audio-editor.md §7,
// §19): onsets within 5 ms in drum patterns, more as sensitivity rises; the tempo of loops within
// 0.1 BPM, and their downbeat; drums told from pads and held notes.

#include "fd/dsp/Onsets.h"

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

struct Noise
{
    std::uint32_t x = 12345;
    float next()
    {
        x = x * 1664525u + 1013904223u;
        return static_cast<float>(x >> 8) / 8388608.0f - 1.0f;
    }
};

/// A kick: a falling sine, 150 ms.
void kick(std::vector<float>& out, double at, float level)
{
    const auto start = static_cast<size_t>(at * kRate);
    double phase = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(0.15 * kRate) && start + i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate;
        phase += 2.0 * kPi * (50.0 + 100.0 * std::exp(-t * 30.0)) / kRate;
        out[start + i] += level * static_cast<float>(std::exp(-t * 20.0) * std::sin(phase));
    }
}

/// A snare or a hat: a burst of noise, `decay` seconds to fade by e.
void burst(std::vector<float>& out, double at, float level, double decay, Noise& noise)
{
    const auto start = static_cast<size_t>(at * kRate);
    for (size_t i = 0; i < static_cast<size_t>(6.0 * decay * kRate) && start + i < out.size(); ++i)
        out[start + i] +=
            level * static_cast<float>(std::exp(-static_cast<double>(i) / kRate / decay)) * noise.next();
}

/// A drum pattern, `bars` bars at `bpm`: a kick on 1 (softer on 3), a snare on 2 and 4, a hat on
/// every beat, and on every eighth when asked. The onsets are where a hat is.
std::vector<float> drums(double bpm, int bars, std::vector<double>* onsets = nullptr, double ghostsDb = 0.0,
                         bool eighths = true)
{
    const double beat = 60.0 / bpm;
    std::vector<float> out(static_cast<size_t>(bars * 4 * beat * kRate) + 1, 0.0f);
    Noise noise;
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t = b * beat;
        if (b % 2 == 0)
            kick(out, t, b % 4 == 0 ? 0.8f : 0.4f);
        else
            burst(out, t, 0.6f, 0.06, noise);
        burst(out, t, 0.2f, 0.015, noise);
        if (eighths)
            burst(out, t + 0.5 * beat, 0.2f, 0.015, noise);
        if (onsets != nullptr)
        {
            onsets->push_back(t);
            if (eighths)
                onsets->push_back(t + 0.5 * beat);
        }
        // Ghost notes, quiet, on the sixteenth after each beat.
        if (ghostsDb < 0.0)
            burst(out, t + 0.25 * beat, 0.2f * static_cast<float>(std::pow(10.0, ghostsDb / 20.0)), 0.015,
                  noise);
    }
    return out;
}

/// A pad: a chord of three slow-rising sines, held.
std::vector<float> pad(double seconds)
{
    std::vector<float> out(static_cast<size_t>(seconds * kRate));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate;
        const double rise = std::min(1.0, t / 0.4);
        out[i] = static_cast<float>(rise * 0.2 *
                                    (std::sin(2.0 * kPi * 220.0 * t) + std::sin(2.0 * kPi * 277.2 * t) +
                                     std::sin(2.0 * kPi * 329.6 * t)));
    }
    return out;
}

/// Held notes: a melody of sines, each a second long, with harmonics.
std::vector<float> heldNotes(double seconds)
{
    std::vector<float> out(static_cast<size_t>(seconds * kRate));
    const double notes[] = {220.0, 246.9, 261.6, 293.7, 329.6, 293.7, 261.6, 246.9};
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate;
        const auto n = static_cast<size_t>(t) % 8;
        const double inNote = t - std::floor(t);
        const double env = std::min(1.0, inNote / 0.05) * std::min(1.0, (1.0 - inNote) / 0.05);
        const double f = notes[n];
        out[i] = static_cast<float>(env * 0.3 *
                                    (std::sin(2.0 * kPi * f * t) + 0.5 * std::sin(4.0 * kPi * f * t) +
                                     0.25 * std::sin(6.0 * kPi * f * t)));
    }
    return out;
}
} // namespace

TEST_CASE("onsets: every hit of a drum pattern, within 5 ms, nothing else", "[fast][onsets]")
{
    std::vector<double> truth;
    const auto audio = drums(120.0, 2, &truth);
    const auto found = findOnsets(audio, kRate, 0.5);
    REQUIRE(found.size() == truth.size());
    for (size_t i = 0; i < truth.size(); ++i)
    {
        INFO("hit " << i);
        CHECK(found[i] == Approx(truth[i]).margin(0.005));
    }
}

TEST_CASE("onsets: more of them as sensitivity rises; none in silence", "[fast][onsets]")
{
    const auto audio = drums(100.0, 2, nullptr, -30.0); // with ghost notes 30 dB down
    const auto few = findOnsets(audio, kRate, 0.1).size();
    const auto some = findOnsets(audio, kRate, 0.5).size();
    const auto many = findOnsets(audio, kRate, 0.95).size();
    CHECK(few <= some);
    CHECK(some <= many);
    CHECK(many > few);
    CHECK(many >= 16 + 4); // the eighths and the ghosts not under a snare's tail
    CHECK(findOnsets(std::vector<float>(48000, 0.0f), kRate, 1.0).empty());
    // A held tone starts once; where the file cuts it off is no start.
    std::vector<float> tone(4 * 48000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
    const auto held = findOnsets(tone, kRate, 1.0);
    REQUIRE(held.size() == 1);
    CHECK(held[0] == Approx(0.0).margin(0.005));
}

TEST_CASE("onsets: the tempo of loops from 60 to 180 BPM, within 0.1 BPM, and their downbeat",
          "[fast][onsets]")
{
    for (const double bpm : {62.0, 75.0, 92.0, 100.0, 128.0, 140.0, 174.0})
    {
        INFO(bpm << " BPM");
        const auto audio = drums(bpm, bpm > 120.0 ? 4 : 2, nullptr, 0.0, false);
        const auto t = estimateTempo(audio, kRate);
        REQUIRE(t);
        CHECK(t->bpm == Approx(bpm).margin(0.1));
        CHECK(t->firstDownbeat == Approx(0.0).margin(0.01));
        CHECK(t->confidence > 0.3);
    }
    // A long take, not a whole number of bars, at a tempo between whole ones.
    auto take = drums(97.3, 16, nullptr, 0.0, false);
    take.resize(take.size() - 7000);
    const auto t = estimateTempo(take, kRate);
    REQUIRE(t);
    CHECK(t->bpm == Approx(97.3).margin(0.1));
    CHECK(t->firstDownbeat == Approx(0.0).margin(0.01));
    // Nothing rhythmic: none. A held tone starts once, from its first sample: no beat repeats.
    CHECK_FALSE(estimateTempo(std::vector<float>(5 * 48000, 0.0f), kRate));
    std::vector<float> tone(4 * 48000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
    CHECK_FALSE(estimateTempo(tone, kRate));
}

TEST_CASE("onsets: with hats on every eighth, a slow groove reads as itself or twice as fast",
          "[fast][onsets]")
{
    // Where the eighths are as strong as the beats, a listener taps either: the Tempo tool's ×2
    // and ÷2 are for this (audio-editor.md §7.8).
    for (const double bpm : {62.0, 84.0})
    {
        INFO(bpm << " BPM");
        const auto t = estimateTempo(drums(bpm, 2), kRate);
        REQUIRE(t);
        INFO("found " << t->bpm);
        const bool itself = std::abs(t->bpm - bpm) < 0.1, twice = std::abs(t->bpm - 2.0 * bpm) < 0.2;
        CHECK((itself || twice));
    }
}

TEST_CASE("onsets: drums are percussive, pads and held notes aren't", "[fast][onsets]")
{
    const double hits = percussiveness(drums(110.0, 2), kRate);
    const double held = percussiveness(pad(4.0), kRate);
    const double notes = percussiveness(heldNotes(4.0), kRate);
    CHECK(hits > 0.5);
    CHECK(held < 0.5);
    CHECK(notes < 0.5);
}
