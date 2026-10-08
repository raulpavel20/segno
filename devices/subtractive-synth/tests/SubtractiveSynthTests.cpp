#include "SubtractiveSynth.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using S = fd::devices::SubtractiveSynth;

namespace
{
constexpr double sr = 48000.0;

TimedEvent noteOn(std::int64_t at, int key, float vel = 0.8f)
{
    return {at, {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = vel}};
}
TimedEvent noteOff(std::int64_t at, int key)
{
    return {at, {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}};
}

/// A clean sine voice: osc 1 sine, no osc 2, open filter, no drift.
void sinePatch(Device& d)
{
    auto& p = d.params();
    p.set(S::Osc1Shape, 100.0f);
    p.set(S::MixOsc2, -70.0f);
    p.set(S::FilterType, 0.0f);
    p.set(S::FilterCutoff, 20000.0f);
    p.set(S::FilterRes, 0.0f);
    p.set(S::FilterKey, 0.0f);
    p.set(S::FilterEnv, 0.0f);
    p.set(S::VoiceDrift, 0.0f);
    p.set(S::AmpA, 5.0f);
    p.set(S::AmpS, 100.0f);
}

AudioBuffer play(S& synth, std::vector<TimedEvent> events, int samples, std::vector<int> pattern = {512})
{
    RenderOptions o{.sampleRate = sr};
    o.events = std::move(events);
    o.blockPattern = std::move(pattern);
    return render(synth, silence(samples, 0), o);
}

/// Frequency by linearly interpolated rising zero crossings.
double estimateFrequency(const AudioBuffer& b, int start, int length)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = start + 1; i < start + length; ++i)
    {
        const float a = b.at(0, i - 1), c = b.at(0, i);
        if (a <= 0.0f && c > 0.0f)
        {
            const double t = (i - 1) + a / (a - c);
            if (first < 0.0)
                first = t;
            else
                ++count;
            last = t;
        }
    }
    return count > 0 ? count * sr / (last - first) : 0.0;
}
} // namespace

TEST_CASE("synth is in tune", "[fast]")
{
    S synth;
    sinePatch(synth);
    const auto out = play(synth, {noteOn(0, 69)}, 96000);
    const double f = estimateFrequency(out, 9600, 86400);
    CHECK(1200.0 * std::log2(f / 440.0) == Approx(0.0).margin(0.1));
}

TEST_CASE("synth sawtooth meets the aliasing target at C7", "[fast]")
{
    // Pitch bend puts C7 (MIDI 96) exactly on an FFT bin, so the exact metric applies.
    constexpr int size = 1 << 15;
    constexpr int cycles = 1429;
    const double target = binCentredFrequency(cycles, size, sr);
    const double semis = 69.0 + 12.0 * std::log2(target / 440.0);
    S synth;
    sinePatch(synth);
    synth.params().set(S::Osc1Shape, 0.0f); // saw
    const float bend = static_cast<float>((semis - 96.0) / 2.0); // bend range 2 st
    const auto out = play(synth, {{0, {.type = EventType::PitchBend, .value = bend}}, noteOn(0, 96)}, 3 * size);
    CHECK(aliasingRatioDb(out, 0, 2 * size, size, cycles, sr) < -70.0);
}

TEST_CASE("ladder self-oscillation follows the keyboard", "[fast]")
{
    S synth;
    auto& p = synth.params();
    p.set(S::MixOsc1, -70.0f);
    p.set(S::MixOsc2, -70.0f);
    p.set(S::MixNoise, -40.0f); // a little noise excites the resonance
    p.set(S::FilterType, 1.0f); // LP 24 ladder
    p.set(S::FilterRes, 100.0f);
    p.set(S::FilterKey, 100.0f);
    p.set(S::FilterEnv, 0.0f);
    p.set(S::FilterCutoff, 261.63f); // C4 at key 60 -> tracks the note
    p.set(S::VoiceDrift, 0.0f);
    p.set(S::AmpS, 100.0f);
    for (int key : {60, 72})
    {
        const auto out = play(synth, {noteOn(0, key)}, 96000);
        const double f = estimateFrequency(out, 48000, 48000);
        const double expected = 440.0 * std::exp2((key - 69) / 12.0);
        INFO("key " << key << " oscillates at " << f << " Hz");
        CHECK(std::abs(1200.0 * std::log2(f / expected)) < 25.0);
    }
}

TEST_CASE("synth voice stealing does not click", "[fast]")
{
    S synth;
    sinePatch(synth);
    synth.params().set(S::VoiceCount, 3.0f);
    synth.params().set(S::AmpR, 400.0f);
    std::vector<TimedEvent> ev;
    const int keys[] = {48, 52, 55, 59, 62, 64, 67, 71, 72, 76};
    for (int i = 0; i < 10; ++i)
    {
        ev.push_back(noteOn(4800 + i * 3001, keys[i]));
        ev.push_back(noteOff(4800 + i * 3001 + 4000, keys[i]));
    }
    const auto out = play(synth, ev, 48000);
    CHECK(allFinite(out));
    CHECK(clickScore(out.slice(9600, 28800), 0) < 8.0);
}

TEST_CASE("synth notes always release to silence", "[fast]")
{
    S synth;
    const auto out = play(synth, {noteOn(0, 60), noteOn(100, 64), noteOff(9000, 60), noteOff(9100, 64)}, 48000);
    CHECK(peakDb(out.slice(0, 9000)) > -30.0);
    CHECK(peakDb(out.slice(9100 + static_cast<int>(synth.tailSeconds() * sr), 4800)) < -100.0);
}

TEST_CASE("synth glide reaches the target pitch in the glide time", "[fast]")
{
    S synth;
    sinePatch(synth);
    synth.params().set(S::VoiceMode, 2.0f); // legato
    synth.params().set(S::VoiceGlide, 100.0f);
    const auto out = play(synth, {noteOn(0, 57), noteOn(24000, 69)}, 48000);
    // Halfway through the glide the pitch is between the notes; after it, at A4.
    CHECK(estimateFrequency(out, 24000 + 2000, 800) < 430.0);
    CHECK(1200.0 * std::log2(estimateFrequency(out, 24000 + 7200, 9600) / 440.0) == Approx(0.0).margin(1.0));
}

TEST_CASE("Subtractive Synth patches match golden metrics", "[fast]")
{
    // A held A2 per patch: level through attack/sustain/release and the sustain's spectral
    // balance in octave bands. Octave bands are wide, so the rectangular window's leakage barely
    // moves them, and it is the same on every platform.
    struct Patch
    {
        const char* name;
        std::vector<std::pair<S::Param, float>> settings;
    };
    const std::vector<Patch> patches{
        {"default", {}},
        {"lp12-resonant", {{S::FilterType, 0.0f}, {S::FilterRes, 85.0f}, {S::FilterCutoff, 800.0f}}},
        {"hp12", {{S::FilterType, 2.0f}, {S::FilterCutoff, 600.0f}, {S::FilterEnv, 0.0f}}},
        {"bp12-square", {{S::FilterType, 3.0f}, {S::Osc1Shape, 33.3f}, {S::Osc1Pw, 25.0f}, {S::FilterCutoff, 1500.0f}}},
        {"noise-sine", {{S::Osc1Shape, 100.0f}, {S::MixOsc2, -70.0f}, {S::MixNoise, -12.0f}}},
    };
    constexpr int noteLength = 48000;
    nlohmann::json metrics;
    for (const auto& patch : patches)
    {
        S synth;
        for (auto [p, v] : patch.settings)
            synth.params().set(p, v);
        const auto out = play(synth, {noteOn(0, 45), noteOff(noteLength, 45)}, noteLength + 24000);
        const std::string k = patch.name;
        metrics[k + ".attack-rms-db"] = rmsDb(out.slice(0, 2400));
        metrics[k + ".sustain-rms-db"] = rmsDb(out.slice(24000, 16384));
        metrics[k + ".release-rms-db"] = rmsDb(out.slice(noteLength + 4800, 4800));

        // Octave bands 125 Hz .. 8 kHz of the sustain, relative to the total (dB).
        const auto power = powerSpectrum(out, 0, 24000, 16384);
        const double binHz = sr / 16384.0;
        double total = 0.0;
        for (double p : power)
            total += p;
        for (double centre = 125.0; centre <= 8000.0; centre *= 2.0)
        {
            double band = 0.0;
            for (size_t i = 0; i < power.size(); ++i)
            {
                const double f = static_cast<double>(i) * binHz;
                if (f >= centre / std::sqrt(2.0) && f < centre * std::sqrt(2.0))
                    band += power[i];
            }
            metrics[k + ".band-" + std::to_string(static_cast<int>(centre)) + "-db"] =
                10.0 * std::log10(std::max(band, 1e-30) / std::max(total, 1e-30));
        }
    }
    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/subtractive-synth/patches.json", metrics, 0.5);
    INFO(result.report);
    CHECK(result.passed);
}

