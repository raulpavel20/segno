#include "Eq.h"
#include "fd/dsp/Fft.h"
#include "fd/dsp/Loudness.h"
#include "fd/dsp/MatchedFilters.h"
#include "fd/testkit/Allocations.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <functional>
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
    CHECK(eq.info().stateVersion >= 2);
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

// ---- stage 2, step 2b: the spectral engine (devices/eq/SPEC.md, "Stage 2") ---------------------

namespace
{
/// The input, `latency` samples later (what a delay-only device puts out).
AudioBuffer delayed(const AudioBuffer& in, int latency)
{
    AudioBuffer out(in.numChannels(), in.numSamples());
    for (int c = 0; c < in.numChannels(); ++c)
        for (int i = latency; i < in.numSamples(); ++i)
            out.at(c, i) = in.at(c, i - latency);
    return out;
}

/// A long-term power spectrum (Welch: 8192-point Hann frames, half overlapping) of one channel,
/// from `from` samples on.
std::vector<double> powerSpectrum(const AudioBuffer& b, int from, int channel = 0)
{
    constexpr int n = 8192;
    dsp::Fft fft(13);
    std::vector<double> sum(n / 2 + 1, 0.0);
    std::vector<std::complex<float>> frame(n);
    for (int at = from; at + n <= b.numSamples(); at += n / 2)
    {
        for (int i = 0; i < n; ++i)
            frame[static_cast<size_t>(i)] = {
                b.at(channel, at + i) * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * i / n)), 0.0f};
        fft.forward(frame.data());
        for (int k = 0; k <= n / 2; ++k)
            sum[static_cast<size_t>(k)] += std::norm(frame[static_cast<size_t>(k)]);
    }
    return sum;
}

/// How much louder `out` is than `in` (dB) in the band `octaves` either side of `hz`.
double changeDb(const std::vector<double>& out, const std::vector<double>& in, double hz, double octaves)
{
    const auto lo = static_cast<size_t>(std::floor(hz * std::exp2(-octaves) * 8192 / sr));
    const auto hi = static_cast<size_t>(std::ceil(hz * std::exp2(octaves) * 8192 / sr));
    double a = 0.0, b = 0.0;
    for (size_t k = lo; k <= hi; ++k)
        a += out[k], b += in[k];
    return 10.0 * std::log10(a / b);
}

void makeSpectral(Device& d, int band, float range, float detail = 50.0f)
{
    makeDynamic(d, band, range, std::nullopt);
    d.params().set(E::spectralIndex(band, E::Spectral), 1.0f);
    d.params().set(E::spectralIndex(band, E::Detail), detail);
}

/// Pink noise with a resonance (a +12 dB bell at 3 kHz, Q 8), or without.
AudioBuffer pinkWith(double resonanceDb, int seconds = 6)
{
    auto in = pinkNoise(static_cast<int>(sr) * seconds, 11, 0.25f);
    if (resonanceDb != 0.0)
    {
        const auto c = dsp::designMatched(dsp::MatchedType::Bell, 3000.0, resonanceDb, 8.0, sr);
        for (int ch = 0; ch < 2; ++ch)
        {
            double s1 = 0.0, s2 = 0.0;
            for (int i = 0; i < in.numSamples(); ++i)
            {
                const double x = in.at(ch, i), y = c.b0 * x + s1;
                s1 = c.b1 * x - c.a1 * y + s2;
                s2 = c.b2 * x - c.a2 * y;
                in.at(ch, i) = static_cast<float>(y);
            }
        }
    }
    return in;
}

int peakAt(const AudioBuffer& b, int channel = 0)
{
    int at = 0;
    for (int i = 1; i < b.numSamples(); ++i)
        if (std::abs(b.at(channel, i)) > std::abs(b.at(channel, at)))
            at = i;
    return at;
}
} // namespace

TEST_CASE("EQ step 2b: the spectral settings keep their IDs and change the delay only", "[fast]")
{
    E eq;
    CHECK(eq.params().spec(E::spectralIndex(0, E::Spectral)).id == "band1.spectral");
    CHECK(eq.params().spec(E::spectralIndex(23, E::Detail)).id == "band24.detail");
    CHECK(eq.params().spec(E::Phase).id == "phase");
    CHECK(eq.params().spec(E::Resolution).id == "resolution");
    CHECK(E::NumParams == 392);
    CHECK(eq.info().stateVersion == 3);
    for (const auto i : {E::spectralIndex(4, E::Spectral), static_cast<size_t>(E::Phase),
                         static_cast<size_t>(E::Resolution)})
    {
        CHECK(eq.params().spec(i).hasFlag(ParamFlags::ChangesLatency));
        CHECK_FALSE(eq.params().spec(i).hasFlag(ParamFlags::Automatable));
    }
    CHECK(eq.params().spec(E::spectralIndex(4, E::Detail)).hasFlag(ParamFlags::Automatable));
    eq.prepare({sr, 512});
    CHECK(eq.latencySamples() == 0); // zero latency, no spectral band: none, as before

    // Spectral on a band that's off still sets the delay: it depends only on these settings.
    eq.params().set(E::spectralIndex(20, E::Spectral), 1.0f);
    CHECK(eq.latencySamples() == E::latencyFor(true, 1, sr));
    CHECK(E::latencyFor(true, 0, 48000.0) == 2560); // about 53 ms
    CHECK(E::latencyFor(true, 1, 48000.0) == 5120); // about 107 ms
    CHECK(E::latencyFor(true, 2, 48000.0) == 10240);
    CHECK(E::latencyFor(true, 1, 96000.0) == 10240); // the same time at 96 kHz
}

TEST_CASE("EQ step 2b: the reported delay is the delay, for every setting and rate", "[fast]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (int setting = 0; setting < 7; ++setting) // zero latency; spectral at 3 resolutions; linear at 3
        {
            E eq;
            if (setting > 0)
            {
                eq.params().set(E::Resolution, static_cast<float>((setting - 1) % 3));
                if (setting <= 3)
                    eq.params().set(E::spectralIndex(5, E::Spectral), 1.0f);
                else
                    eq.params().set(E::Phase, 1.0f);
            }
            const int length = static_cast<int>(rate * 0.25);
            const auto out = render(eq, impulse(length, 2, 1.0f, 100), {.sampleRate = rate});
            INFO(rate << " Hz, setting " << setting);
            CHECK(peakAt(out) == 100 + eq.latencySamples());
            CHECK(peakAt(out, 1) == 100 + eq.latencySamples());
        }
}

TEST_CASE("EQ step 2b: linear phase, flat, is only the delay", "[fast]")
{
    E eq;
    eq.params().set(E::Phase, 1.0f);
    const auto in = whiteNoise(96000, 5, 0.5f);
    const auto out = render(eq, in, {.sampleRate = sr});
    CHECK(nullResidualDb(out, delayed(in, eq.latencySamples())) < -120.0);
}

TEST_CASE("EQ step 2b: linear phase has the zero-latency curve's magnitude, and no phase change", "[fast]")
{
    const auto curve = [](E& eq)
    {
        setBand(eq, 0, E::BandType::LowCut, 30.0f, 0.0f, 0.71f, E::Placement::Stereo, 3); // 24 dB/oct
        setBand(eq, 1, E::BandType::Bell, 250.0f, -3.0f, 1.2f);
        setBand(eq, 2, E::BandType::Bell, 3000.0f, 4.0f, 2.0f);
        setBand(eq, 3, E::BandType::HighShelf, 10000.0f, 3.0f);
    };
    E zero, linear;
    curve(zero);
    curve(linear);
    linear.params().set(E::Phase, 1.0f);
    const auto ir = render(linear, impulse(16384, 2), {.sampleRate = sr});
    const auto reference = render(zero, impulse(16384, 2), {.sampleRate = sr});
    const auto db = [](const AudioBuffer& b, double hz)
    { return 20.0 * std::log10(std::abs(responseAt(b.channel(0), b.numSamples(), hz, sr))); };
    double worst = 0.0;
    for (double hz = 40.0; hz < 20000.0; hz *= std::exp2(1.0 / 6.0))
        worst = std::max(worst, std::abs(db(ir, hz) - db(reference, hz)));
    CHECK(worst < 0.1);

    // The impulse response is symmetric about the delay.
    const int centre = linear.latencySamples();
    float peak = 0.0f, asymmetry = 0.0f;
    for (int k = 0; k < centre; ++k)
    {
        peak = std::max(peak, std::abs(ir.at(0, centre + k)));
        asymmetry = std::max(asymmetry, std::abs(ir.at(0, centre + k) - ir.at(0, centre - k)));
    }
    CHECK(asymmetry < 1e-4f * peak);
}

TEST_CASE("EQ step 2b: mid and side bands in linear phase act on mid or side only", "[fast]")
{
    E eq;
    eq.params().set(E::Phase, 1.0f);
    setBand(eq, 1, E::BandType::Bell, 1000.0f, -12.0f, 1.0f, E::Placement::Side);
    // Mid only (the same in both channels): untouched, only delayed.
    const auto mid = impulse(16384, 2, 0.5f, 0);
    CHECK(nullResidualDb(render(eq, mid, {.sampleRate = sr}), delayed(mid, eq.latencySamples())) < -110.0);
    // Side only: the bell's cut at 1 kHz, opposite in the two channels.
    auto sideOnly = impulse(16384, 2, 0.5f, 0);
    sideOnly.at(1, 0) = -0.5f;
    const auto out = render(eq, sideOnly, {.sampleRate = sr});
    const double at1k =
        20.0 * std::log10(std::abs(responseAt(out.channel(0), out.numSamples(), 1000.0, sr)) / 0.5);
    CHECK(at1k == Approx(-12.0).margin(0.1));
    float mirrored = 0.0f;
    for (int i = 0; i < out.numSamples(); ++i)
        mirrored = std::max(mirrored, std::abs(out.at(0, i) + out.at(1, i)));
    CHECK(mirrored < 1e-6f);
}

TEST_CASE("EQ step 2b: a spectral band brings a resonance down to its surroundings", "[fast]")
{
    // A wide spectral bell around it; the resonance 12 dB over pink noise.
    E eq;
    setBand(eq, 1, E::BandType::Bell, 3000.0f, 0.0f, 0.5f);
    makeSpectral(eq, 1, -18.0f);
    const auto in = pinkWith(12.0);
    const auto out = render(eq, in, {.sampleRate = sr});
    const auto latency = eq.latencySamples();
    const auto before = powerSpectrum(delayed(in, latency), static_cast<int>(sr));
    const auto after = powerSpectrum(out, static_cast<int>(sr));
    CHECK(changeDb(after, before, 3000.0, 1.0 / 48) < -9.0);
    CHECK(std::abs(changeDb(after, before, 1500.0, 1.0 / 12)) < 1.0);
    CHECK(std::abs(changeDb(after, before, 6000.0, 1.0 / 12)) < 1.0);
    // The display shows the cut there.
    auto& frames = eq.analysis().fifo(E::DynamicsFifo);
    std::vector<float> all(frames.available());
    frames.pop(all.data(), all.size());
    REQUIRE(all.size() >= static_cast<size_t>(E::dynamicsFrame));
    const auto* last = all.data() + all.size() - E::dynamicsFrame + 2 * E::numBands;
    const int point = static_cast<int>(std::lround(12.0 * std::log2(3000.0 / 20.0)));
    CHECK(last[point] < -6.0f);
    CHECK(last[point - 12] > -1.0f); // an octave down
}

TEST_CASE("EQ step 2b: a spectral band leaves pink noise alone", "[fast]")
{
    // Nothing sticks out: under 1 dB of change anywhere (in twelfth-octave bands).
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 0.0f, 0.2f); // as wide as it gets
    makeSpectral(eq, 1, -12.0f);
    const auto in = pinkWith(0.0);
    const auto out = render(eq, in, {.sampleRate = sr});
    const auto before = powerSpectrum(delayed(in, eq.latencySamples()), static_cast<int>(sr));
    const auto after = powerSpectrum(out, static_cast<int>(sr));
    double worst = 0.0;
    for (double hz = 40.0; hz < 18000.0; hz *= std::exp2(1.0 / 12.0))
        worst = std::max(worst, std::abs(changeDb(after, before, hz, 1.0 / 24)));
    CHECK(worst < 1.0);
}

TEST_CASE("EQ step 2b: a spectral band triggered by the sidechain cuts where the sidechain sticks out",
          "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 2000.0f, 0.0f, 0.5f);
    makeSpectral(eq, 1, -12.0f);
    eq.params().set(E::dynIndex(1, E::Trigger), static_cast<float>(E::TriggerSource::Sidechain));
    const auto in = pinkWith(0.0, 4);
    const auto key = steppedSine(2000.0, {0.3f}, 4.0);
    const auto out = render(eq, in, {.sampleRate = sr, .sidechain = &key});
    const auto before = powerSpectrum(delayed(in, eq.latencySamples()), static_cast<int>(sr));
    const auto after = powerSpectrum(out, static_cast<int>(sr));
    CHECK(changeDb(after, before, 2000.0, 1.0 / 48) < -6.0);
    CHECK(std::abs(changeDb(after, before, 1000.0, 1.0 / 12)) < 1.0);
    CHECK(std::abs(changeDb(after, before, 4000.0, 1.0 / 12)) < 1.0);
}

TEST_CASE("EQ step 2b: at zero latency with a spectral band, the other bands stay minimum phase, in time",
          "[fast]")
{
    // The spectral band never moves (Manual, nothing over 0 dB): the rest is the zero-latency EQ,
    // delayed.
    const auto curve = [](E& eq)
    {
        setBand(eq, 1, E::BandType::Bell, 500.0f, 6.0f, 1.0f);
        setBand(eq, 2, E::BandType::HighShelf, 6000.0f, -4.0f, 0.71f, E::Placement::Mid);
    };
    E zero, spectral;
    curve(zero);
    curve(spectral);
    setBand(spectral, 5, E::BandType::Bell, 4000.0f, 0.0f, 1.0f);
    makeDynamic(spectral, 5, -12.0f, 0.0f);
    spectral.params().set(E::spectralIndex(5, E::Spectral), 1.0f);
    const auto in = whiteNoise(48000, 6, 0.25f);
    const auto expected = delayed(render(zero, in, {.sampleRate = sr}), spectral.latencySamples());
    CHECK(nullResidualDb(render(spectral, in, {.sampleRate = sr}), expected) < -120.0);
}

TEST_CASE("EQ step 2b: Delta with every band static is silent, in either processing", "[fast]")
{
    for (const bool linear : {false, true})
    {
        E eq;
        setBand(eq, 1, E::BandType::Bell, 1000.0f, 6.0f, 1.0f);
        setBand(eq, 2, E::BandType::LowShelf, 200.0f, -3.0f);
        makeDynamic(eq, 2, -6.0f, 0.0f); // dynamic, never over its threshold
        eq.params().set(E::spectralIndex(4, E::Spectral), 1.0f);
        eq.params().set(E::Phase, linear ? 1.0f : 0.0f);
        eq.params().set(E::Delta, 1.0f);
        INFO((linear ? "linear phase" : "zero latency"));
        CHECK(peakDb(render(eq, whiteNoise(48000, 4, 0.3f), {.sampleRate = sr})) < -120.0);
    }
}

TEST_CASE("EQ step 2b: Delta in linear phase plays what a dynamic band takes away", "[fast]")
{
    E eq;
    eq.params().set(E::Phase, 1.0f);
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 0.0f, 1.0f);
    makeDynamic(eq, 1, -6.0f, -40.0f);
    eq.params().set(E::Delta, 1.0f);
    const auto tone = steppedSine(1000.0, {0.1f}, 1.5);
    const auto out = render(eq, tone, {.sampleRate = sr});
    CHECK(rmsBetween(out, 1.0, 1.5) - rmsBetween(tone, 1.0, 1.5) ==
          Approx(20.0 * std::log10(1.0 - 0.501)).margin(0.3));
}

TEST_CASE("EQ step 2b: Listen lines up with the delay", "[fast]")
{
    E eq;
    eq.params().set(E::Phase, 1.0f);
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 0.0f, 2.0f);
    eq.params().set(E::Listen, 2.0f);
    const auto in = impulse(24000, 2, 1.0f, 100);
    const auto out = render(eq, in, {.sampleRate = sr});
    // The band pass's response starts where the delay ends, not before.
    double early = 0.0;
    for (int i = 0; i < 100 + eq.latencySamples(); ++i)
        early = std::max(early, static_cast<double>(std::abs(out.at(0, i))));
    CHECK(early < 1e-6);
    CHECK(peakDb(out) > -40.0);
}

TEST_CASE("EQ step 2b: the engine sounds the same for any block size, and never allocates", "[fast]")
{
    for (const bool linear : {false, true})
    {
        const auto make = [linear]
        {
            auto eq = std::make_unique<E>();
            setBand(*eq, 1, E::BandType::Bell, 3000.0f, 0.0f, 0.5f);
            makeSpectral(*eq, 1, -12.0f);
            setBand(*eq, 2, E::BandType::LowShelf, 150.0f, 4.0f, 0.71f, E::Placement::Side);
            makeDynamic(*eq, 2, -6.0f, -30.0f);
            eq->params().set(E::Phase, linear ? 1.0f : 0.0f);
            eq->params().set(E::Resolution, 0.0f);
            return eq;
        };
        const auto in = pinkWith(12.0, 1);
        auto reference = make();
        const auto expected = render(*reference, in, {.sampleRate = sr});
        for (const auto& pattern : std::vector<std::vector<int>>{{1}, {4096}, {3, 100, 31, 33, 1000}})
        {
            auto eq = make();
            INFO((linear ? "linear phase" : "zero latency") << ", first block " << pattern.front());
            CHECK(bitIdentical(render(*eq, in, {.sampleRate = sr, .blockPattern = pattern}), expected));
        }
        if (allocationTrackingEnabled())
        {
            const auto before = allocationsDuringProcess();
            auto eq = make();
            render(*eq, in, {.sampleRate = sr});
            CHECK(allocationsDuringProcess() == before);
        }
    }
}

TEST_CASE("EQ step 2b: what the engine costs", "[.benchmark]")
{
    // Release only: seconds of processing per second of audio at 48 kHz, for the SPEC's targets.
    const auto measure = [](const char* what, const std::function<void(E&)>& setUp)
    {
        E eq;
        setUp(eq);
        const auto in = pinkWith(6.0, 10);
        const auto start = std::chrono::steady_clock::now();
        render(eq, in, {.sampleRate = sr});
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
        WARN(what << ": " << 100.0 * took.count() / 10.0 << " % of a core");
    };
    measure("4 bands, zero latency",
            [](E& eq)
            {
                for (int b = 0; b < 4; ++b)
                    setBand(eq, b, E::BandType::Bell, 200.0f * static_cast<float>(b + 1), 3.0f, 1.0f);
            });
    measure("24 dynamic bands",
            [](E& eq)
            {
                for (int b = 0; b < E::numBands; ++b)
                {
                    setBand(eq, b, E::BandType::Bell, 40.0f * std::pow(1.3f, static_cast<float>(b)), 0.0f,
                            1.0f);
                    makeDynamic(eq, b, -6.0f, -40.0f);
                }
            });
    for (int resolution = 0; resolution < 3; ++resolution)
    {
        measure(resolution == 0   ? "linear phase, 8 bands, Low"
                : resolution == 1 ? "linear phase, 8 bands, Medium"
                                  : "linear phase, 8 bands, High",
                [resolution](E& eq)
                {
                    eq.params().set(E::Phase, 1.0f);
                    eq.params().set(E::Resolution, static_cast<float>(resolution));
                    for (int b = 0; b < 8; ++b)
                        setBand(eq, b, E::BandType::Bell, 100.0f * static_cast<float>(b + 1), 2.0f, 1.0f);
                });
        measure(resolution == 0   ? "4 spectral bands, Low"
                : resolution == 1 ? "4 spectral bands, Medium"
                                  : "4 spectral bands, High",
                [resolution](E& eq)
                {
                    eq.params().set(E::Resolution, static_cast<float>(resolution));
                    for (int b = 0; b < 4; ++b)
                    {
                        setBand(eq, b, E::BandType::Bell, 400.0f * static_cast<float>(b + 1), 0.0f, 1.0f,
                                b % 2 == 0 ? E::Placement::Stereo : E::Placement::Mid);
                        makeSpectral(eq, b, -12.0f);
                    }
                });
    }
    measure("24 spectral bands, High, linear phase",
            [](E& eq)
            {
                eq.params().set(E::Phase, 1.0f);
                eq.params().set(E::Resolution, 2.0f);
                for (int b = 0; b < E::numBands; ++b)
                {
                    setBand(eq, b, E::BandType::Bell, 40.0f * std::pow(1.3f, static_cast<float>(b)), 0.0f,
                            1.0f);
                    makeSpectral(eq, b, -12.0f);
                }
            });
}
