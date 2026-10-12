// Chorus (M3 step B2): the automated test targets of devices/chorus/SPEC.md.

#include "Chorus.h"
#include "fd/device/Realtime.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using C = fd::devices::Chorus;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<C::Param, float>>;

/// Only the voices: all wet, no Low Cut.
constexpr Settings kWet{{C::Mix, 100.0f}, {C::LowCut, 0.0f}};

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings base, Settings settings, RenderOptions o = {})
{
    C d;
    setAll(d, base);
    setAll(d, settings);
    o.sampleRate = kRate;
    return render(d, in, o);
}

int samples(double seconds)
{
    return static_cast<int>(std::lround(seconds * kRate));
}

double db(double ratio)
{
    return 20.0 * std::log10(ratio);
}

double rmsOver(const AudioBuffer& b, int c, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double>(b.at(c, i)) * b.at(c, i);
    return std::sqrt(sum / (to - from));
}

/// A buffer with the same signal on both sides.
AudioBuffer mono(const AudioBuffer& b)
{
    AudioBuffer out(2, b.numSamples());
    for (int i = 0; i < b.numSamples(); ++i)
        out.at(0, i) = out.at(1, i) = b.at(0, i);
    return out;
}

/// The intervals between a channel's rising zero crossings over [from, to) samples.
std::vector<double> periods(const AudioBuffer& b, int c, int from, int to)
{
    std::vector<double> crossings, out;
    for (int i = from + 1; i < to; ++i)
    {
        const float a = b.at(c, i - 1), z = b.at(c, i);
        if (a < 0.0f && z >= 0.0f)
            crossings.push_back(i - 1 + a / (a - z));
    }
    for (size_t i = 1; i < crossings.size(); ++i)
        out.push_back(crossings[i] - crossings[i - 1]);
    return out;
}

/// How far a tone at `hz` strays from itself over [from, to): what's left after the best-fitting
/// sinusoid at `hz` is taken away, against that sinusoid (dB). Sidebands, mostly.
double strayDb(const AudioBuffer& b, int c, double hz, int from, int to)
{
    double cs = 0.0, sn = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double w = 2.0 * std::numbers::pi * hz * i / kRate;
        cs += b.at(c, i) * std::cos(w);
        sn += b.at(c, i) * std::sin(w);
    }
    const double n = to - from;
    cs *= 2.0 / n;
    sn *= 2.0 / n;
    double residual = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double w = 2.0 * std::numbers::pi * hz * i / kRate;
        const double r = b.at(c, i) - cs * std::cos(w) - sn * std::sin(w);
        residual += r * r;
    }
    return db(std::sqrt(residual / n) / (std::hypot(cs, sn) / std::sqrt(2.0)));
}

/// Where the echo of an impulse at `t0` (samples) arrives through a voice whose delay at sample t
/// is `delay(t)` (samples): the t at which t - delay(t) = t0.
double arrival(double t0, const std::function<double(double)>& delay)
{
    double t = t0 + delay(t0);
    for (int i = 0; i < 50; ++i)
        t = t0 + delay(t);
    return t;
}

/// Runs `blocks` blocks of `size` samples of silence, the song at `ppq(block)` (beats).
void play(Device& d, int blocks, int size, const std::function<double(int)>& ppq, double tempo = 120.0)
{
    AudioBuffer buffer(2, size);
    float* channels[] = {buffer.channel(0), buffer.channel(1)};
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        ProcessContext ctx;
        ctx.input = {channels, 2};
        ctx.output = {channels, 2};
        ctx.numSamples = size;
        ctx.transport.tempoBpm = tempo;
        ctx.transport.ppqPosition = ppq(b);
        ctx.transport.playing = true;
        processRealtime(d, ctx);
    }
}

double wrapped(double cycles)
{
    return std::abs(cycles - std::round(cycles));
}
} // namespace

TEST_CASE("Chorus: the note values run from the slowest to the fastest", "[chorus][fast]")
{
    for (int i = 1; i < C::numDivisions; ++i)
        CHECK(C::divisionBeats(i, 4.0) < C::divisionBeats(i - 1, 4.0));
    CHECK(C::divisionBeats(0, 4.0) == 32.0); // 8 bars
    CHECK(C::divisionBeats(3, 3.0) == 3.0);  // a bar follows the meter
    CHECK(C::divisionBeats(5, 4.0) == 2.0);  // 1/2, the default
}

TEST_CASE("Chorus: with no depth, the voices trail by Delay, to the sample", "[chorus][fast]")
{
    for (const auto mode : {C::ModeKind::Chorus, C::ModeKind::Ensemble})
    {
        INFO("mode " << static_cast<int>(mode));
        const auto out =
            renderWith(impulse(samples(0.1)), kWet,
                       {{C::Mode, static_cast<float>(mode)}, {C::Depth, 0.0f}, {C::DelayTime, 6.0f}});
        // Ensemble's three voices, all at the same delay, sum to sqrt(3) of one.
        const float level = mode == C::ModeKind::Ensemble ? std::sqrt(3.0f) : 1.0f;
        const int at = samples(0.006);
        for (int c = 0; c < 2; ++c)
        {
            CHECK(std::abs(out.at(c, at) - level) < 1e-5f);
            double elsewhere = 0.0;
            for (int i = 0; i < out.numSamples(); ++i)
                if (i != at)
                    elsewhere += std::abs(out.at(c, i));
            CHECK(elsewhere < 1e-5);
        }
    }
}

TEST_CASE("Chorus: each voice's echo arrives where voiceDelayMs says", "[chorus][fast]")
{
    constexpr double rate = 0.5;
    const int spacing = samples(0.05);
    const int total = samples(4.0);
    AudioBuffer train(2, total);
    for (int i = 0; i + spacing < total; i += spacing)
        train.at(0, i) = train.at(1, i) = 1.0f;

    for (const auto mode : {C::ModeKind::Chorus, C::ModeKind::Ensemble, C::ModeKind::Vibrato})
        for (const float shape : {0.0f, 50.0f, 100.0f})
        {
            INFO("mode " << static_cast<int>(mode) << ", shape " << shape);
            const auto out = renderWith(train, kWet,
                                        {{C::Mode, static_cast<float>(mode)},
                                         {C::Depth, 100.0f},
                                         {C::Rate, static_cast<float>(rate)},
                                         {C::Shape, shape},
                                         {C::DelayTime, 6.0f}});
            const C::Voicing v{
                .mode = mode, .depth = 1.0, .delayMs = 6.0, .shape = shape * 0.01, .offset = 0.5};
            const int voices = C::voiceCount(mode);
            int checked = 0;
            for (int t0 = 0; t0 + 2 * spacing < total; t0 += spacing)
                for (int side = 0; side < 2; ++side)
                {
                    std::array<double, C::maxVoices> at{};
                    for (int k = 0; k < voices; ++k)
                        at[static_cast<size_t>(k)] = arrival(
                            t0,
                            [&](double t)
                            {
                                return C::voiceDelayMs(v, kRate, side, k, rate * t / kRate) * kRate / 1000.0;
                            });
                    for (int k = 0; k < voices; ++k)
                    {
                        // Where two voices' echoes meet, neither can be told apart: skip it.
                        bool alone = true;
                        for (int j = 0; j < voices; ++j)
                            alone = alone && (j == k || std::abs(at[static_cast<size_t>(j)] -
                                                                 at[static_cast<size_t>(k)]) > 24.0);
                        if (!alone)
                            continue;
                        const double expected = at[static_cast<size_t>(k)];
                        const int from = static_cast<int>(expected) - 8;
                        int loudest = from;
                        for (int i = from; i <= from + 16; ++i)
                            if (std::abs(out.at(side, i)) > std::abs(out.at(side, loudest)))
                                loudest = i;
                        INFO("impulse at " << t0 << ", side " << side << ", voice " << k);
                        CHECK(std::abs(loudest - expected) <= 1.0);
                        ++checked;
                    }
                }
            CHECK(checked > 100);
        }
}

TEST_CASE("Chorus: Vibrato bends the pitch as far as pitchSwing says", "[chorus][fast]")
{
    for (const float depth : {25.0f, 50.0f, 100.0f})
        for (const float shape : {0.0f, 100.0f})
        {
            INFO("depth " << depth << ", shape " << shape);
            const auto out = renderWith(
                sine(1000.0, kRate, samples(2.0), 0.5f), kWet,
                {{C::Mode, 2.0f}, {C::Rate, 5.0f}, {C::Depth, depth}, {C::Shape, shape}, {C::Offset, 0.0f}});
            double worst = 0.0;
            for (const double p : periods(out, 0, samples(0.2), samples(2.0)))
                worst = std::max(worst, std::abs(1200.0 * std::log2(48.0 / p)));
            const C::Voicing v{
                .mode = C::ModeKind::Vibrato, .depth = depth * 0.01, .shape = shape * 0.01, .offset = 0.0};
            CHECK(std::abs(worst - C::pitchSwing(v, 5.0, kRate)) < 1.0);
        }
}

TEST_CASE("Chorus: at Offset 0, a mono input stays mono", "[chorus][fast]")
{
    const auto in = mono(pinkNoise(samples(1.0), 5, 0.4f));
    for (const float mode : {0.0f, 1.0f, 2.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(
            in, {}, {{C::Mode, mode}, {C::Offset, 0.0f}, {C::Feedback, 60.0f}, {C::Warmth, 40.0f}});
        bool same = true;
        for (int i = 0; i < out.numSamples(); ++i)
            same = same && out.at(0, i) == out.at(1, i);
        CHECK(same);
    }
}

TEST_CASE("Chorus: at Width 0 % the voices are mono", "[chorus][fast]")
{
    const auto in = pinkNoise(samples(1.0), 6, 0.4f);
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(in, kWet, {{C::Mode, mode}, {C::Width, 0.0f}});
        double side = 0.0;
        for (int i = 0; i < out.numSamples(); ++i)
            side = std::max(side, static_cast<double>(std::abs(out.at(0, i) - out.at(1, i))));
        CHECK(db(std::max(side, 1e-12)) < -120.0);
    }
}

TEST_CASE("Chorus: Low Cut leaves the lows untouched and chorus what's above", "[chorus][fast]")
{
    const int length = samples(3.0), from = samples(1.0), to = samples(3.0);
    const auto low = sine(40.0, kRate, length, 0.25f);
    const auto high = sine(2000.0, kRate, length, 0.25f);
    for (const float mode : {0.0f, 1.0f, 2.0f})
        for (const float mix : {0.0f, 50.0f, 100.0f})
        {
            INFO("mode " << mode << ", mix " << mix);
            const Settings cut{{C::Mode, mode}, {C::Mix, mix}, {C::LowCut, 1.0f}, {C::LowCutFreq, 200.0f}};
            const auto lows = renderWith(low, {}, cut);
            CHECK(std::abs(db(rmsOver(lows, 0, from, to) / rmsOver(low, 0, from, to))) < 0.5);
            CHECK(strayDb(lows, 0, 40.0, from, to) < -50.0); // -62 dBFS
            if (mix > 0.0f || mode == 2.0f)
                CHECK(strayDb(renderWith(high, {}, cut), 0, 2000.0, from, to) > -30.0);
        }
}

TEST_CASE("Chorus: at 0 % Dry/Wet the output is the input", "[chorus][fast]")
{
    const auto in = pinkNoise(samples(2.0), 3, 0.5f);
    for (const float mode : {0.0f, 1.0f})
        for (const float lowCut : {0.0f, 1.0f})
        {
            INFO("mode " << mode << ", low cut " << lowCut);
            const auto out = renderWith(in, {},
                                        {{C::Mode, mode},
                                         {C::LowCut, lowCut},
                                         {C::Mix, 0.0f},
                                         {C::Feedback, 80.0f},
                                         {C::Warmth, 50.0f},
                                         {C::Depth, 80.0f}});
            CHECK(nullResidualDb(in, out) < -120.0);
        }
}

TEST_CASE("Chorus: at 50 % Dry/Wet it's as loud as the dry sound", "[chorus][fast]")
{
    const auto in = pinkNoise(samples(4.0), 9, 0.3f);
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(in, {}, {{C::Mode, mode}});
        for (int c = 0; c < 2; ++c)
            CHECK(std::abs(db(rmsOver(out, c, samples(0.5), samples(4.0)) /
                              rmsOver(in, c, samples(0.5), samples(4.0)))) < 1.0);
    }
}

TEST_CASE("Chorus: full feedback with full Warmth stays bounded", "[chorus][fast]")
{
    const auto in = sine(200.0, kRate, samples(10.0), 1.0f);
    for (const float mode : {0.0f, 1.0f})
        for (const float invert : {0.0f, 1.0f})
        {
            INFO("mode " << mode << ", invert " << invert);
            const auto out = renderWith(in, {},
                                        {{C::Mode, mode},
                                         {C::Feedback, 95.0f},
                                         {C::Warmth, 100.0f},
                                         {C::Invert, invert},
                                         {C::Rate, 1.0f}, // a second's windows hold whole cycles
                                         {C::Mix, 100.0f}});
            REQUIRE(allFinite(out));
            CHECK(peakDb(out) <= 6.0);
            CHECK(rmsOver(out, 0, samples(9.0), samples(10.0)) <=
                  rmsOver(out, 0, samples(1.0), samples(2.0)) * dsp::dbToGain(0.5f));
        }
}

TEST_CASE("Chorus: Invert flips each pass through the feedback", "[chorus][fast]")
{
    for (const float invert : {0.0f, 1.0f})
    {
        INFO("invert " << invert);
        const auto out =
            renderWith(impulse(samples(0.1)), kWet,
                       {{C::Depth, 0.0f}, {C::DelayTime, 5.0f}, {C::Feedback, 50.0f}, {C::Invert, invert}});
        const float sign = invert > 0.0f ? -1.0f : 1.0f;
        CHECK(std::abs(out.at(0, samples(0.005)) - 1.0f) < 1e-5f);
        CHECK(std::abs(out.at(0, samples(0.010)) - sign * 0.5f) < 1e-5f);
        CHECK(std::abs(out.at(0, samples(0.015)) - 0.25f) < 1e-5f);
    }
}

TEST_CASE("Chorus: synced, the movement is locked to the song", "[chorus][fast]")
{
    // One cycle a beat (1/4) at 120 BPM: two cycles a second.
    const Settings synced{{C::Sync, 1.0f}, {C::Div, 8.0f}};

    SECTION("played through or started later, it's at the same place")
    {
        RenderOptions through;
        through.sampleRate = kRate;
        C a;
        setAll(a, synced);
        (void)render(a, silence(samples(2.0)), through);
        RenderOptions later = through;
        later.transport.ppqPosition = 2.0; // a second in
        C b;
        setAll(b, synced);
        (void)render(b, silence(samples(1.0)), later);
        CHECK(wrapped(a.analysis().read(C::LfoPosition) - b.analysis().read(C::LfoPosition)) < 1e-4);
        CHECK(a.analysis().read(C::RateHz) == 2.0f);
    }

    SECTION("after a jump, it glides to the song's place")
    {
        C d;
        setAll(d, synced);
        d.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        const double perBlock = 512.0 / kRate * 2.0; // beats
        // Forty blocks from the start, then a jump to beat 10.3 and on from there.
        play(d, 80, 512, [&](int b) { return b < 40 ? b * perBlock : 10.3 + (b - 40) * perBlock; });
        CHECK(wrapped(d.analysis().read(C::LfoPosition) - (10.3 + 40 * perBlock)) < 1e-3);
    }

    SECTION("whatever the blocks, the same")
    {
        RenderOptions one, other;
        one.sampleRate = other.sampleRate = kRate;
        other.blockPattern = {7, 300, 1, 64};
        const auto in = sine(1000.0, kRate, samples(1.0), 0.5f);
        C a, b;
        setAll(a, synced);
        setAll(b, synced);
        CHECK(bitIdentical(render(a, in, one), render(b, in, other)));
    }
}

TEST_CASE("Chorus: changing the mode, Sync or Offset doesn't click", "[chorus][fast]")
{
    const auto in = sine(1000.0, kRate, samples(2.0), 0.5f);
    struct Change
    {
        const char* name;
        C::Param param;
        float from, to;
    };
    for (const auto& change :
         {Change{"Chorus to Ensemble", C::Mode, 0.0f, 1.0f},
          Change{"Ensemble to Vibrato", C::Mode, 1.0f, 2.0f},
          Change{"Vibrato to Chorus", C::Mode, 2.0f, 0.0f}, Change{"Sync on", C::Sync, 0.0f, 1.0f},
          Change{"Offset", C::Offset, 0.0f, 180.0f}, Change{"Delay", C::DelayTime, 1.0f, 30.0f}})
    {
        INFO(change.name);
        RenderOptions o;
        o.beforeBlock = [&change](Device& d, std::int64_t start)
        {
            if (start >= samples(1.0))
                d.params().set(change.param, change.to);
        };
        const auto out =
            renderWith(in, {}, {{change.param, change.from}, {C::Depth, 80.0f}, {C::Feedback, 40.0f}}, o);
        for (int c = 0; c < 2; ++c)
            CHECK(clickScore(out.slice(samples(0.5), samples(1.0)), c) < 8.0);
    }
}

TEST_CASE("Chorus: the tail is the longest delay, and the feedback's fall", "[chorus][fast]")
{
    C d;
    // The defaults: 6 ms and a quarter of the 8 ms sweep, no feedback.
    CHECK(std::abs(d.tailSeconds() - (0.008 + 0.01)) < 1e-9);
    d.params().set(C::Feedback, 50.0f);
    CHECK(std::abs(d.tailSeconds() - ((3.0 / std::log10(2.0) + 1.0) * 0.008 + 0.01)) < 1e-9);
    d.params().set(C::Mode, 2.0f); // Vibrato: no feedback
    CHECK(d.tailSeconds() < 0.02);
}

TEST_CASE("Chorus: its character matches golden metrics", "[chorus][fast]")
{
    nlohmann::json metrics;
    // Warmth at 100 %: the voices' tone (an impulse's echo, 8 kHz against 1 kHz)...
    const auto ir = renderWith(impulse(samples(0.1)), kWet, {{C::Depth, 0.0f}, {C::Warmth, 100.0f}});
    const int at = samples(0.006) - 64;
    metrics["warmth100.tone_8k_db"] = db(std::abs(responseAt(ir.channel(0) + at, 256, 8000.0, kRate)) /
                                         std::abs(responseAt(ir.channel(0) + at, 256, 1000.0, kRate)));
    // ...and its saturation: the third harmonic of a loud tone.
    constexpr int size = 4096, cycles = 85; // about 1 kHz, on an FFT bin
    const auto loud = renderWith(sine(binCentredFrequency(cycles, size, kRate), kRate, samples(0.5), 0.7f),
                                 kWet, {{C::Depth, 0.0f}, {C::Warmth, 100.0f}});
    metrics["warmth100.h3_minus3dbfs_db"] = harmonicLevelsDb(loud, 0, samples(0.2), size, cycles, 4)[2];
    // The default chorus and ensemble on pink noise: their level, and how far they bend.
    const auto pink = pinkNoise(samples(4.0), 11, 0.3f);
    for (const auto mode : {C::ModeKind::Chorus, C::ModeKind::Ensemble})
    {
        const std::string name = mode == C::ModeKind::Chorus ? "chorus" : "ensemble";
        const auto out = renderWith(pink, {}, {{C::Mode, static_cast<float>(mode)}});
        metrics[name + ".pink_level_db"] =
            db(rmsOver(out, 0, samples(0.5), samples(4.0)) / rmsOver(pink, 0, samples(0.5), samples(4.0)));
        metrics[name + ".swing_cents"] = C::pitchSwing({.mode = mode}, 0.8, kRate);
    }

    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/chorus/character.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
