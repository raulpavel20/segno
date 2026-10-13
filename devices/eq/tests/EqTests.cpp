#include "Eq.h"
#include "fd/dsp/Loudness.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using E = fd::devices::Eq;

namespace
{
constexpr double sr = 48000.0;

void setBand(Device& d, int band, E::BandType type, float freq, float gain, float q = 0.71f,
             E::Placement place = E::Placement::Stereo, int slope = 1)
{
    auto& p = d.params();
    p.set(E::paramIndex(band, E::On), 1.0f);
    p.set(E::paramIndex(band, E::Type), static_cast<float>(type));
    p.set(E::paramIndex(band, E::Freq), freq);
    p.set(E::paramIndex(band, E::Gain), gain);
    p.set(E::paramIndex(band, E::Q), q);
    p.set(E::paramIndex(band, E::Place), static_cast<float>(place));
    p.set(E::paramIndex(band, E::Slope), static_cast<float>(slope));
}

/// Measured magnitude in dB at f from an impulse through the device (L channel).
double measuredDb(Device& d, double f, int channel = 0)
{
    const auto ir = render(d, impulse(16384, 2), {.sampleRate = sr});
    return 20.0 * std::log10(std::abs(responseAt(ir.channel(channel), ir.numSamples(), f, sr)));
}
} // namespace

TEST_CASE("EQ has 24 bands and permanent IDs", "[fast]")
{
    E eq;
    CHECK(eq.params().size() == static_cast<size_t>(E::NumParams));
    CHECK(eq.params().spec(E::paramIndex(2, E::Freq)).id == "band3.freq");
    CHECK(eq.params().spec(E::Output).id == "output");
}

TEST_CASE("EQ defaults are bit-transparent", "[fast]")
{
    E eq;
    const auto in = whiteNoise(48000, 2, 0.5f);
    CHECK(nullResidualDb(render(eq, in, {.sampleRate = sr}), in) == silenceDb);
}

TEST_CASE("EQ with all bands off nulls against the input", "[fast]")
{
    E eq;
    for (int b = 0; b < E::numBands; ++b)
        eq.params().set(E::paramIndex(b, E::On), 0.0f);
    const auto in = whiteNoise(48000, 3, 0.5f);
    CHECK(nullResidualDb(render(eq, in, {.sampleRate = sr}), in) == silenceDb);
}

TEST_CASE("EQ bell, shelf and cut responses match the design", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 6.0f, 1.0f);
    CHECK(measuredDb(eq, 1000.0) == Approx(6.0).margin(0.05));

    E shelf;
    setBand(shelf, 0, E::BandType::LowShelf, 200.0f, -9.0f);
    CHECK(measuredDb(shelf, 20.0) == Approx(-9.0).margin(0.1));

    for (int slope = 0; slope < 8; ++slope)
    {
        // Butterworth attenuation one octave below the cutoff: 10 log10(1 + 2^(2N)).
        E cut;
        setBand(cut, 0, E::BandType::LowCut, 1000.0f, 0.0f, 0.71f, E::Placement::Stereo, slope);
        const int order = E::slopeDbPerOct(slope) / 6;
        const double expected = -10.0 * std::log10(1.0 + std::pow(2.0, 2.0 * order));
        INFO("slope index " << slope);
        CHECK(measuredDb(cut, 500.0) == Approx(expected).margin(0.5));
        CHECK(measuredDb(cut, 1000.0) == Approx(-3.0103).margin(0.1));
    }
}

TEST_CASE("EQ Mid/Side placement leaves the other component untouched", "[fast]")
{
    // L == R input has no side: a side band must change nothing.
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 12.0f, 1.0f, E::Placement::Side);
    const auto in = sine(1000.0, sr, 24000, 0.25f, 2);
    const auto out = render(eq, in, {.sampleRate = sr});
    CHECK(nullResidualDb(out, in) < -100.0);

    // A left band leaves the right channel bit-identical.
    E left;
    setBand(left, 1, E::BandType::Bell, 1000.0f, 12.0f, 1.0f, E::Placement::Left);
    const auto noise = whiteNoise(24000, 5, 0.25f);
    const auto o2 = render(left, noise, {.sampleRate = sr});
    bool rightUntouched = true;
    for (int i = 0; i < noise.numSamples(); ++i)
        rightUntouched &= o2.at(1, i) == noise.at(1, i);
    CHECK(rightUntouched);
}

TEST_CASE("EQ fast frequency sweep does not click", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 20.0f, 12.0f, 2.0f);
    const auto in = sine(1000.0, sr, 96000, 0.25f, 2);
    RenderOptions opts{.sampleRate = sr};
    opts.blockPattern = {64};
    opts.beforeBlock = [](Device& d, std::int64_t pos) {
        const double t = static_cast<double>(pos) / 48000.0; // 20 Hz -> 20 kHz over 1 s (log)
        d.params().set(E::paramIndex(1, E::Freq), static_cast<float>(20.0 * std::pow(1000.0, std::min(1.0, t))));
    };
    const auto out = render(eq, in, opts);
    CHECK(allFinite(out));
    CHECK(clickScore(out.slice(4800, 86400), 0) < 8.0);
}

TEST_CASE("EQ analyzer taps carry the pre and post signals", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 12.0f, 1.0f);
    render(eq, sine(1000.0, sr, 4096, 0.25f, 2), {.sampleRate = sr});
    std::vector<float> pre(4096), post(4096);
    CHECK(eq.analysis().fifo(E::PreFifo).pop(pre.data(), pre.size()) == 4096);
    CHECK(eq.analysis().fifo(E::PostFifo).pop(post.data(), post.size()) == 4096);
    float peakPre = 0.0f, peakPost = 0.0f;
    for (int i = 2048; i < 4096; ++i)
    {
        peakPre = std::max(peakPre, std::abs(pre[static_cast<size_t>(i)]));
        peakPost = std::max(peakPost, std::abs(post[static_cast<size_t>(i)]));
    }
    CHECK(20.0 * std::log10(peakPost / peakPre) == Approx(12.0).margin(0.2));
}

// ---- stage 2 (devices/eq/SPEC.md, "Stage 2") --------------------------------------------------

namespace
{
/// RMS in dB of one channel between two times (seconds).
double rmsBetween(const AudioBuffer& b, double from, double to, int channel = 0)
{
    const auto a = static_cast<int>(from * sr), z = std::min(b.numSamples(), static_cast<int>(to * sr));
    double sum = 0.0;
    for (int i = a; i < z; ++i)
        sum += static_cast<double>(b.at(channel, i)) * b.at(channel, i);
    return z > a && sum > 0.0 ? 10.0 * std::log10(sum / (z - a)) : -300.0;
}

/// A sine at `hz` whose amplitude is `levels[k]` for `seconds` each, in turn.
AudioBuffer steppedSine(double hz, std::vector<float> levels, double seconds)
{
    const int each = static_cast<int>(seconds * sr);
    AudioBuffer out(2, each * static_cast<int>(levels.size()));
    for (int i = 0; i < out.numSamples(); ++i)
    {
        const float v =
            levels[static_cast<size_t>(i / each)] * static_cast<float>(std::sin(2.0 * M_PI * hz * i / sr));
        out.at(0, i) = out.at(1, i) = v;
    }
    return out;
}

void makeDynamic(Device& d, int band, float range, std::optional<float> threshold, float attack = 5.0f,
                 float release = 120.0f, E::TriggerSource trigger = E::TriggerSource::Input)
{
    auto& p = d.params();
    p.set(E::dynIndex(band, E::Dyn), 1.0f);
    p.set(E::dynIndex(band, E::Range), range);
    p.set(E::dynIndex(band, E::AutoThreshold), threshold ? 0.0f : 1.0f);
    if (threshold)
        p.set(E::dynIndex(band, E::Threshold), *threshold);
    p.set(E::dynIndex(band, E::Attack), attack);
    p.set(E::dynIndex(band, E::Release), release);
    p.set(E::dynIndex(band, E::Trigger), static_cast<float>(trigger));
}
} // namespace

TEST_CASE("EQ stage 2: the new parameters keep their IDs, and their defaults change nothing", "[fast]")
{
    E eq;
    CHECK(eq.params().spec(E::dynIndex(0, E::Dyn)).id == "band1.dyn");
    CHECK(eq.params().spec(E::dynIndex(23, E::Trigger)).id == "band24.trigger");
    CHECK(eq.params().spec(E::AutoGain).id == "autogain");
    CHECK(eq.params().spec(E::Listen).id == "listen");
    CHECK(eq.params().spec(E::Listen).hasFlag(ParamFlags::Transient));
    CHECK(eq.params().spec(E::Delta).hasFlag(ParamFlags::Transient));
    CHECK(eq.info().stateVersion == 2);
    CHECK(eq.info().buses.sidechainChannels == 2);

    // A core curve: the same with stage 2 at its defaults as without it.
    E a, b;
    for (auto* d : {&a, &b})
    {
        setBand(*d, 1, E::BandType::Bell, 1000.0f, 6.0f, 1.0f);
        setBand(*d, 3, E::BandType::HighShelf, 6000.0f, -4.0f);
    }
    for (int k = 0; k < E::numBands; ++k)
        b.params().set(E::dynIndex(k, E::Range), 12.0f); // Range alone, with Dynamics off: nothing
    const auto in = whiteNoise(48000, 3, 0.3f);
    CHECK(nullResidualDb(render(a, in, {.sampleRate = sr}), render(b, in, {.sampleRate = sr})) == silenceDb);
}

TEST_CASE("EQ stage 2: how far a dynamic band moves, by how far its trigger is over the threshold", "[fast]")
{
    CHECK(E::dynamicGain(-10.0f, -6.0f) == 0.0f);
    CHECK(E::dynamicGain(-3.0f, -6.0f) == 0.0f);
    CHECK(E::dynamicGain(0.0f, -6.0f) < 0.0f); // the knee starts before the threshold
    CHECK(E::dynamicGain(6.0f, -6.0f) == Approx(-3.0f));
    CHECK(E::dynamicGain(12.0f, -6.0f) == Approx(-6.0f));
    CHECK(E::dynamicGain(30.0f, -6.0f) == Approx(-6.0f));
    CHECK(E::dynamicGain(30.0f, 4.0f) == Approx(4.0f));
    float last = 0.0f;
    for (float over = -6.0f; over <= 14.0f; over += 0.25f) // smooth and steady
    {
        const float g = E::dynamicGain(over, -6.0f);
        CHECK(g <= last + 1e-6f);
        last = g;
    }
}

TEST_CASE("EQ stage 2: a dynamic bell cuts only while its range is loud", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 0.0f, 1.0f);
    makeDynamic(eq, 1, -6.0f, -40.0f);
    // Quiet (-60 dBFS: under the threshold), loud (-20: 17 dB over), quiet again.
    const auto in = steppedSine(1000.0, {0.001f, 0.1f, 0.001f}, 1.0);
    const auto out = render(eq, in, {.sampleRate = sr});
    CHECK(rmsBetween(out, 0.5, 1.0) - rmsBetween(in, 0.5, 1.0) == Approx(0.0).margin(0.1));
    CHECK(rmsBetween(out, 1.5, 2.0) - rmsBetween(in, 1.5, 2.0) == Approx(-6.0).margin(0.2));
    // The attack: well on its way within 15 ms, little of it in the first millisecond.
    CHECK(rmsBetween(out, 1.0, 1.001) - rmsBetween(in, 1.0, 1.001) > -2.0);
    CHECK(rmsBetween(out, 1.015, 1.025) - rmsBetween(in, 1.015, 1.025) < -5.0);
    // Back within the release.
    CHECK(rmsBetween(out, 2.6, 3.0) - rmsBetween(in, 2.6, 3.0) == Approx(0.0).margin(0.2));
}

TEST_CASE("EQ stage 2: triggered by the sidechain, a bass dips under the kick only while it plays", "[fast]")
{
    E eq;
    setBand(eq, 0, E::BandType::Bell, 60.0f, 0.0f, 1.0f);
    makeDynamic(eq, 0, -6.0f, -30.0f, 5.0f, 60.0f, E::TriggerSource::Sidechain);
    const auto bass = steppedSine(60.0, {0.2f, 0.2f}, 1.0);
    const auto kick = steppedSine(60.0, {0.0f, 0.5f}, 1.0); // silent, then loud
    const auto out = render(eq, bass, {.sampleRate = sr, .sidechain = &kick});
    CHECK(rmsBetween(out, 0.5, 1.0) - rmsBetween(bass, 0.5, 1.0) == Approx(0.0).margin(0.1));
    CHECK(rmsBetween(out, 1.5, 2.0) - rmsBetween(bass, 1.5, 2.0) == Approx(-6.0).margin(0.3));
    // With no sidechain connected, it listens to the input (the bass, over the threshold).
    E alone;
    setBand(alone, 0, E::BandType::Bell, 60.0f, 0.0f, 1.0f);
    makeDynamic(alone, 0, -6.0f, -30.0f, 5.0f, 60.0f, E::TriggerSource::Sidechain);
    const auto self = render(alone, bass, {.sampleRate = sr});
    CHECK(rmsBetween(self, 1.5, 2.0) - rmsBetween(bass, 1.5, 2.0) == Approx(-6.0).margin(0.3));
}

TEST_CASE("EQ stage 2: Auto Threshold moves on what stands out from the usual level", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 2000.0f, 0.0f, 1.0f);
    makeDynamic(eq, 1, -6.0f, std::nullopt);
    // Steady for 4 s (the usual level), then 12 dB louder.
    const auto in = steppedSine(2000.0, {0.05f, 0.05f, 0.05f, 0.05f, 0.2f}, 1.0);
    const auto out = render(eq, in, {.sampleRate = sr});
    CHECK(rmsBetween(out, 3.5, 4.0) - rmsBetween(in, 3.5, 4.0) == Approx(0.0).margin(0.5));
    CHECK(rmsBetween(out, 4.05, 4.25) - rmsBetween(in, 4.05, 4.25) < -2.0);
}

TEST_CASE("EQ stage 2: Auto Gain keeps the loudness of pink noise", "[fast]")
{
    E eq;
    setBand(eq, 3, E::BandType::HighShelf, 4000.0f, 6.0f);
    const auto in = pinkNoise(static_cast<int>(sr * 4), 7, 0.25f);
    const auto loudness = [](const AudioBuffer& b)
    {
        dsp::LoudnessMeter m;
        m.prepare(sr, 2);
        const float* ch[2] = {b.channel(0), b.channel(1)};
        m.process(ch, b.numSamples());
        return m.integrated();
    };
    const float dry = loudness(in), boosted = loudness(render(eq, in, {.sampleRate = sr}));
    CHECK(boosted - dry > 1.0f); // the shelf makes it louder...
    eq.params().set(E::AutoGain, 1.0f);
    const auto out = render(eq, in, {.sampleRate = sr});
    CHECK(loudness(out) == Approx(dry).margin(0.5)); // ...and Auto Gain takes that back
    CHECK(eq.analysis().read(E::AutoGainDb) < -1.0f);
}

TEST_CASE("EQ stage 2: Delta plays only what the dynamics change", "[fast]")
{
    // Every band static: nothing.
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 6.0f, 1.0f);
    eq.params().set(E::Delta, 1.0f);
    const auto noise = whiteNoise(48000, 4, 0.3f);
    CHECK(peakDb(render(eq, noise, {.sampleRate = sr})) < -120.0);
    // A band cutting 6 dB: the cut, so the delta is half the band's signal, inverted.
    E dyn;
    setBand(dyn, 1, E::BandType::Bell, 1000.0f, 0.0f, 1.0f);
    makeDynamic(dyn, 1, -6.0f, -40.0f);
    dyn.params().set(E::Delta, 1.0f);
    const auto tone = steppedSine(1000.0, {0.1f}, 1.0);
    const auto out = render(dyn, tone, {.sampleRate = sr});
    CHECK(rmsBetween(out, 0.5, 1.0) - rmsBetween(tone, 0.5, 1.0) ==
          Approx(20.0 * std::log10(1.0 - 0.501)).margin(0.3));
}

TEST_CASE("EQ stage 2: Listen plays only the band's range", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 0.0f, 2.0f); // flat: Listen still plays its range
    eq.params().set(E::Listen, 2.0f);                       // band 2
    const auto at = [&](double hz)
    {
        const auto in = steppedSine(hz, {0.1f}, 0.5);
        return rmsBetween(render(eq, in, {.sampleRate = sr}), 0.25, 0.5) - rmsBetween(in, 0.25, 0.5);
    };
    CHECK(at(1000.0) == Approx(0.0).margin(0.5));
    CHECK(at(100.0) < -20.0);
    CHECK(at(10000.0) < -20.0);
}

TEST_CASE("EQ stage 2: the dynamics and the sidechain reach the display", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 0.0f, 1.0f);
    makeDynamic(eq, 1, -6.0f, -40.0f);
    const auto in = steppedSine(1000.0, {0.1f}, 0.5);
    const auto key = steppedSine(200.0, {0.3f}, 0.5);
    render(eq, in, {.sampleRate = sr, .sidechain = &key});
    auto& frames = eq.analysis().fifo(E::DynamicsFifo);
    REQUIRE(frames.available() >= static_cast<size_t>(E::dynamicsFrame));
    std::vector<float> all(frames.available());
    frames.pop(all.data(), all.size());
    REQUIRE(all.size() % E::dynamicsFrame == 0);
    const auto* last = all.data() + all.size() - E::dynamicsFrame;
    CHECK(last[1] == Approx(-6.0f).margin(0.2)); // band 2's gain now
    CHECK(last[E::numBands + 1] > -30.0f);       // its trigger's level
    CHECK(last[0] == 0.0f);                      // band 1: not dynamic
    CHECK(eq.analysis().fifo(E::SidechainFifo).available() > 0);
}
