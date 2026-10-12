// Phaser & Flanger (M3 step B3): the automated test targets of devices/phaser-flanger/SPEC.md.

#include "PhaserFlanger.h"
#include "fd/device/Realtime.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using P = fd::devices::PhaserFlanger;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<P::Param, float>>;

/// The effect alone, still: no Low Cut, no sweep, no feedback.
constexpr Settings kStill{{P::LowCut, 0.0f}, {P::Amount, 0.0f}, {P::Feedback, 0.0f}};

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings base, Settings settings, RenderOptions o = {})
{
    P d;
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
    return 20.0 * std::log10(std::max(ratio, 1e-30));
}

double rmsOver(const AudioBuffer& b, int c, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double>(b.at(c, i)) * b.at(c, i);
    return std::sqrt(sum / (to - from));
}

AudioBuffer mono(const AudioBuffer& b)
{
    AudioBuffer out(2, b.numSamples());
    for (int i = 0; i < b.numSamples(); ++i)
        out.at(0, i) = out.at(1, i) = b.at(0, i);
    return out;
}

/// What's left of a tone at `hz` once the best-fitting sinusoid is taken away, against it (dB).
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

/// Runs `blocks` blocks of `size` samples of `in` (looped), the song at `ppq(block)`, and gives
/// each block's analysis value in `slot` after it.
std::vector<float> play(Device& d, int blocks, int size, const std::function<double(int)>& ppq, int slot)
{
    AudioBuffer buffer(2, size);
    float* channels[] = {buffer.channel(0), buffer.channel(1)};
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        ProcessContext ctx;
        ctx.input = {channels, 2};
        ctx.output = {channels, 2};
        ctx.numSamples = size;
        ctx.transport.ppqPosition = ppq(b);
        ctx.transport.playing = true;
        processRealtime(d, ctx);
        out.push_back(d.analysis().read(slot));
    }
    return out;
}

double wrapped(double cycles)
{
    return std::abs(cycles - std::round(cycles));
}

P::Response responseOf(P::ModeKind mode, int notches, double spread)
{
    return {.mode = mode, .notches = notches, .spread = spread, .feedback = 0.0, .mix = 0.5};
}
} // namespace

TEST_CASE("Phaser & Flanger: where the sweep goes", "[phaser][fast]")
{
    const P::Sweep s{.centerHz = 800.0, .flangerMs = 2.0, .doublerMs = 20.0, .amount = 1.0, .envAmount = 0.5};
    auto phaser = s, flanger = s, doubler = s;
    flanger.mode = P::ModeKind::Flanger;
    doubler.mode = P::ModeKind::Doubler;
    CHECK(P::sweep(phaser, 0.0, 0.0) == 800.0);
    CHECK(std::abs(P::sweep(phaser, 1.0, 0.0) - 6400.0) < 1e-9); // three octaves up
    CHECK(std::abs(P::sweep(phaser, -1.0, 0.0) - 100.0) < 1e-9);
    CHECK(std::abs(P::sweep(phaser, 0.0, 1.0) - 800.0 * std::exp2(1.5)) < 1e-9); // the envelope at half
    CHECK(P::sweep(flanger, 1.0, 0.0) == 2.0);                                   // Time at the LFO's top
    CHECK(P::sweep(flanger, -1.0, 0.0) == 0.0); // down to zero at full Amount
    CHECK(P::sweep(doubler, 1.0, 0.0) == 25.0);
    CHECK(P::sweep(doubler, -1.0, 0.0) == 15.0);
    // Spread: bunched (high Q) to wide (low Q).
    CHECK(P::allpassQ(0.0) == 4.0);
    CHECK(std::abs(P::allpassQ(1.0) - 0.3) < 1e-12);
    // The note values are the Chorus's.
    CHECK(P::numDivisions == 17);
}

TEST_CASE("Phaser & Flanger: the phaser's response is response(), with its notches", "[phaser][fast]")
{
    const int length = 1 << 15;
    for (const int notches : {1, 2, 6, 12})
        for (const float spread : {0.0f, 50.0f, 100.0f})
        {
            INFO(notches << " notches, spread " << spread);
            const auto ir =
                renderWith(impulse(length), kStill,
                           {{P::PhaserNotches, static_cast<float>(notches)}, {P::PhaserSpread, spread}});
            const auto r = responseOf(P::ModeKind::Phaser, notches, spread * 0.01);
            for (double hz = 100.0; hz <= 10000.0; hz *= 1.12)
            {
                const double measured = std::abs(responseAt(ir.channel(0), length, hz, kRate));
                const double expected = P::response(r, 800.0, hz, kRate);
                // Near a notch the level is tiny: compare to within 0.2 dB, or both deep down.
                if (expected > 0.01)
                    CHECK(std::abs(db(measured / expected)) < 0.2);
                else
                    CHECK(measured < 0.02);
            }

            // Notches notches, each at least 40 dB deep (found on a fine grid, then refined).
            int found = 0;
            const auto level = [&](double hz) { return P::response(r, 800.0, hz, kRate); };
            double prev2 = level(20.0), prev1 = level(20.0 * 1.002);
            for (double hz = 20.0 * 1.004; hz < 20000.0; hz *= 1.002)
            {
                const double now = level(hz);
                if (prev1 < prev2 && prev1 <= now)
                {
                    double lo = hz / 1.004, hi = hz;
                    for (int k = 0; k < 60; ++k)
                    {
                        const double a = lo + (hi - lo) / 3.0, b = hi - (hi - lo) / 3.0;
                        (level(a) < level(b) ? hi : lo) = (level(a) < level(b) ? b : a);
                    }
                    if (db(level(0.5 * (lo + hi))) < -40.0)
                        ++found;
                }
                prev2 = prev1;
                prev1 = now;
            }
            CHECK(found == notches);
        }
}

TEST_CASE("Phaser & Flanger: the flanger's comb and the doubler's take come Time later", "[phaser][fast]")
{
    const auto g = P::gains(0.5f);
    SECTION("Flanger: Time, and each pass the feedback times the last")
    {
        const auto ir = renderWith(impulse(samples(0.05)), kStill,
                                   {{P::Mode, 1.0f}, {P::FlangerTime, 2.0f}, {P::Feedback, 50.0f}});
        CHECK(std::abs(ir.at(0, 0) - g.dry) < 1e-6f);
        CHECK(std::abs(ir.at(0, 96) - g.wet) < 1e-6f);
        CHECK(std::abs(ir.at(0, 192) - 0.5f * g.wet) < 1e-6f);
        CHECK(std::abs(ir.at(0, 95)) < 1e-6f);
    }
    SECTION("Doubler: Time")
    {
        const auto ir =
            renderWith(impulse(samples(0.05)), kStill, {{P::Mode, 2.0f}, {P::DoublerTime, 20.0f}});
        CHECK(std::abs(ir.at(0, 960) - g.wet) < 1e-6f);
        CHECK(std::abs(ir.at(0, 959)) < 1e-6f);
    }
}

TEST_CASE("Phaser & Flanger: the flanger's echoes arrive where sweep() says", "[phaser][fast]")
{
    constexpr double rate = 0.25, time = 6.0;
    const int spacing = samples(0.05), total = samples(4.0);
    AudioBuffer train(2, total);
    for (int i = 0; i + spacing < total; i += spacing)
        train.at(0, i) = train.at(1, i) = 1.0f;
    const auto out = renderWith(train, {{P::LowCut, 0.0f}, {P::Feedback, 0.0f}, {P::Mix, 100.0f}},
                                {{P::Mode, 1.0f},
                                 {P::Shape, 0.0f},
                                 {P::Rate, static_cast<float>(rate)},
                                 {P::Amount, 100.0f},
                                 {P::FlangerTime, static_cast<float>(time)},
                                 {P::Offset, 0.0f}});
    // The LFO's value, smoothed as the device smooths it (3 ms), sample by sample.
    const double k = 1.0 - std::exp(-1.0 / (0.003 * kRate));
    std::vector<double> lfo(static_cast<size_t>(total));
    double s = 0.0;
    for (int i = 0; i < total; ++i)
    {
        s += k * (std::sin(2.0 * std::numbers::pi * rate * i / kRate) - s);
        lfo[static_cast<size_t>(i)] = s;
    }
    const P::Sweep sw{.mode = P::ModeKind::Flanger, .flangerMs = time, .amount = 1.0, .envAmount = 0.0};
    int checked = 0;
    for (int t0 = 0; t0 + 2 * spacing < total; t0 += spacing)
    {
        // Where t - delay(t) = t0; echoes shorter than the line's shortest read are skipped.
        double t = t0;
        for (int it = 0; it < 50; ++it)
            t = t0 + P::sweep(sw, lfo[static_cast<size_t>(t)], 0.0) * 0.001 * kRate;
        if (t - t0 < 24.0)
            continue;
        const int from = static_cast<int>(t) - 8;
        int loudest = from;
        for (int i = from; i <= from + 16; ++i)
            if (std::abs(out.at(0, i)) > std::abs(out.at(0, loudest)))
                loudest = i;
        INFO("impulse at " << t0);
        CHECK(std::abs(loudest - t) <= 1.0);
        ++checked;
    }
    CHECK(checked > 40);
}

TEST_CASE("Phaser & Flanger: Through Zero holds the dry sound back 1 ms, reported as latency",
          "[phaser][fast]")
{
    P d;
    d.params().set(P::FlangerZero, 1.0f);
    d.prepare({.sampleRate = kRate, .maxBlockSize = 512});
    CHECK(d.latencySamples() == 48);
    d.params().set(P::FlangerZero, 0.0f);
    CHECK(d.latencySamples() == 0);

    SECTION("at 0 % Dry/Wet, the input 1 ms later, in every mode")
    {
        const auto in = pinkNoise(samples(1.0), 4, 0.5f);
        for (const float mode : {0.0f, 1.0f, 2.0f})
        {
            const auto out = renderWith(in, {}, {{P::Mode, mode}, {P::FlangerZero, 1.0f}, {P::Mix, 0.0f}});
            double worst = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 48; i < in.numSamples(); ++i)
                    worst = std::max(worst, static_cast<double>(std::abs(out.at(c, i) - in.at(c, i - 48))));
            CHECK(db(worst) < -120.0);
        }
    }

    SECTION("where the sweep crosses the dry sound, the two add up (+3 dB at 50 %)")
    {
        const auto in = sine(1000.0, kRate, samples(1.0), 0.25f);
        const auto out =
            renderWith(in, kStill, {{P::Mode, 1.0f}, {P::FlangerZero, 1.0f}, {P::FlangerTime, 1.0f}});
        CHECK(std::abs(db(rmsOver(out, 0, samples(0.2), samples(1.0)) /
                          rmsOver(in, 0, samples(0.2), samples(1.0))) -
                       db(std::sqrt(2.0))) < 0.1);
    }
}

TEST_CASE("Phaser & Flanger: the envelope follows the input's level, within Attack and Release",
          "[phaser][fast]")
{
    // A 0 dBFS burst of 100 ms, then silence.
    auto in = sine(1000.0, kRate, samples(0.5), 1.0f);
    for (int c = 0; c < 2; ++c)
        for (int i = samples(0.1); i < in.numSamples(); ++i)
            in.at(c, i) = 0.0f;
    const auto after = [&](double seconds)
    {
        P d;
        setAll(d, {{P::Env, 1.0f}, {P::EnvAttack, 10.0f}, {P::EnvRelease, 250.0f}});
        RenderOptions o;
        o.sampleRate = kRate;
        (void)render(d, in.slice(0, samples(seconds)), o);
        return d.analysis().read(P::Envelope);
    };
    CHECK(after(0.005) < 0.6f);  // still rising, half way in
    CHECK(after(0.012) > 0.98f); // there within Env Attack
    CHECK(after(0.1) > 0.98f);
    CHECK(after(0.2) > 0.4f);   // falling, back within Env Release of the burst ending
    CHECK(after(0.36) == 0.0f); // 100 ms + 250 ms

    // Off, it's nothing.
    P d;
    RenderOptions o;
    o.sampleRate = kRate;
    (void)render(d, in.slice(0, samples(0.1)), o);
    CHECK(d.analysis().read(P::Envelope) == 0.0f);
}

TEST_CASE("Phaser & Flanger: synced, locked to the song; Spin sweeps the right side faster", "[phaser][fast]")
{
    SECTION("after a jump, the song's place")
    {
        P d;
        setAll(d, {{P::Sync, 1.0f}, {P::Div, 8.0f}}); // 1/4: a cycle a beat
        d.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        const double perBlock = 512.0 / kRate * 2.0;
        const auto at = play(
            d, 80, 512, [&](int b) { return b < 40 ? b * perBlock : 7.3 + (b - 40) * perBlock; },
            P::LfoPosition);
        CHECK(wrapped(at.back() - (7.3 + 40 * perBlock)) < 1e-3);
    }

    SECTION("Spin 100 %: twice as many cycles on the right")
    {
        P d;
        setAll(d, {{P::Shape, 0.0f}, {P::Rate, 2.0f}, {P::Stereo, 1.0f}, {P::Spin, 100.0f}});
        d.prepare({.sampleRate = kRate, .maxBlockSize = 64});
        const auto count = [](const std::vector<float>& v)
        {
            int n = 0;
            for (size_t i = 1; i < v.size(); ++i)
                n += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
            return n;
        };
        const auto none = [](int) { return 0.0; };
        P e;
        setAll(e, {{P::Shape, 0.0f}, {P::Rate, 2.0f}, {P::Stereo, 1.0f}, {P::Spin, 100.0f}});
        e.prepare({.sampleRate = kRate, .maxBlockSize = 64});
        const auto left = play(d, 3000, 64, none, P::LfoLeft);   // 4 s: 8 cycles, 16 crossings
        const auto right = play(e, 3000, 64, none, P::LfoRight); // 16 cycles
        CHECK(std::abs(count(left) - 16) <= 1);
        CHECK(std::abs(count(right) - 32) <= 1);
    }
}

TEST_CASE("Phaser & Flanger: at Offset 0, a mono input stays mono", "[phaser][fast]")
{
    const auto in = mono(pinkNoise(samples(1.0), 5, 0.4f));
    for (const float mode : {0.0f, 1.0f, 2.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(
            in, {}, {{P::Mode, mode}, {P::Offset, 0.0f}, {P::Feedback, 60.0f}, {P::Warmth, 40.0f}});
        bool same = true;
        for (int i = 0; i < out.numSamples(); ++i)
            same = same && out.at(0, i) == out.at(1, i);
        CHECK(same);
    }
}

TEST_CASE("Phaser & Flanger: Low Cut leaves the lows untouched", "[phaser][fast]")
{
    const int length = samples(3.0), from = samples(1.0), to = samples(3.0);
    const auto low = sine(40.0, kRate, length, 0.25f);
    for (const float mode : {0.0f, 1.0f, 2.0f})
        for (const float mix : {0.0f, 50.0f, 100.0f})
        {
            INFO("mode " << mode << ", mix " << mix);
            const auto out = renderWith(low, {},
                                        {{P::Mode, mode},
                                         {P::Mix, mix},
                                         {P::LowCut, 1.0f},
                                         {P::LowCutFreq, 200.0f},
                                         {P::PhaserCenter, 300.0f}});
            CHECK(std::abs(db(rmsOver(out, 0, from, to) / rmsOver(low, 0, from, to))) < 0.5);
            CHECK(strayDb(out, 0, 40.0, from, to) < -50.0);
        }
}

TEST_CASE("Phaser & Flanger: at 0 % Dry/Wet the output is the input", "[phaser][fast]")
{
    const auto in = pinkNoise(samples(2.0), 3, 0.5f);
    for (const float mode : {0.0f, 1.0f, 2.0f})
        for (const float lowCut : {0.0f, 1.0f})
        {
            INFO("mode " << mode << ", low cut " << lowCut);
            const auto out = renderWith(in, {},
                                        {{P::Mode, mode},
                                         {P::LowCut, lowCut},
                                         {P::Mix, 0.0f},
                                         {P::Feedback, 80.0f},
                                         {P::Warmth, 50.0f},
                                         {P::Env, 1.0f}});
            CHECK(nullResidualDb(in, out) < -120.0);
        }
}

TEST_CASE("Phaser & Flanger: at 50 % Dry/Wet it's as loud as the dry sound", "[phaser][fast]")
{
    const auto in = pinkNoise(samples(4.0), 9, 0.3f);
    for (const float mode : {0.0f, 1.0f, 2.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(in, {}, {{P::Mode, mode}, {P::Feedback, 0.0f}});
        for (int c = 0; c < 2; ++c)
            CHECK(std::abs(db(rmsOver(out, c, samples(0.5), samples(4.0)) /
                              rmsOver(in, c, samples(0.5), samples(4.0)))) < 1.0);
    }
}

TEST_CASE("Phaser & Flanger: full feedback with full Warmth stays bounded", "[phaser][fast]")
{
    const auto in = sine(200.0, kRate, samples(10.0), 1.0f);
    for (const float mode : {0.0f, 1.0f, 2.0f})
        for (const float invert : {0.0f, 1.0f})
        {
            INFO("mode " << mode << ", invert " << invert);
            const auto out = renderWith(in, {},
                                        {{P::Mode, mode},
                                         {P::Feedback, 95.0f},
                                         {P::Warmth, 100.0f},
                                         {P::Invert, invert},
                                         {P::Rate, 1.0f}, // a second's windows hold whole cycles
                                         {P::Mix, 100.0f}});
            REQUIRE(allFinite(out));
            CHECK(peakDb(out) <= 6.0);
            CHECK(rmsOver(out, 0, samples(9.0), samples(10.0)) <=
                  rmsOver(out, 0, samples(1.0), samples(2.0)) * dsp::dbToGain(0.5f));
        }
}

TEST_CASE("Phaser & Flanger: changing the mode, notches, shape, Sync or Stereo doesn't click",
          "[phaser][fast]")
{
    const auto in = sine(1000.0, kRate, samples(2.0), 0.5f);
    struct Change
    {
        const char* name;
        P::Param param;
        float from, to;
    };
    for (const auto& change :
         {Change{"Phaser to Flanger", P::Mode, 0.0f, 1.0f}, Change{"Flanger to Doubler", P::Mode, 1.0f, 2.0f},
          Change{"Doubler to Phaser", P::Mode, 2.0f, 0.0f}, Change{"notches", P::PhaserNotches, 2.0f, 8.0f},
          Change{"shape", P::Shape, 0.0f, 4.0f}, Change{"Sync on", P::Sync, 0.0f, 1.0f},
          Change{"Spin to Offset", P::Stereo, 1.0f, 0.0f}})
    {
        INFO(change.name);
        RenderOptions o;
        o.beforeBlock = [&change](Device& d, std::int64_t start)
        {
            if (start >= samples(1.0))
                d.params().set(change.param, change.to);
        };
        const auto out =
            renderWith(in, {}, {{change.param, change.from}, {P::Amount, 80.0f}, {P::Feedback, 40.0f}}, o);
        for (int c = 0; c < 2; ++c)
            CHECK(clickScore(out.slice(samples(0.5), samples(1.0)), c) < 8.0);
    }
}

TEST_CASE("Phaser & Flanger: the tail is the feedback's fall over the loop's longest pass", "[phaser][fast]")
{
    P d;
    d.params().set(P::Mode, 1.0f); // Flanger, 2 ms: the longest pass is Time, plus Through Zero's 1 ms
    d.params().set(P::Feedback, 50.0f);
    CHECK(std::abs(d.tailSeconds() - ((3.0 / std::log10(2.0) + 1.0) * 0.003 + 0.01)) < 1e-9);
    d.params().set(P::Feedback, 0.0f);
    CHECK(std::abs(d.tailSeconds() - 0.013) < 1e-9);
    d.params().set(P::Mode, 0.0f); // Phaser: the chain's delay at its lowest notch
    CHECK(d.tailSeconds() > 0.01);
    CHECK(d.tailSeconds() < 1.0);
}

TEST_CASE("Phaser & Flanger: its character matches golden metrics", "[phaser][fast]")
{
    nlohmann::json metrics;
    // Warmth at 100 %: the effect's tone, on the doubler's take (8 kHz against 1 kHz).
    const auto ir =
        renderWith(impulse(samples(0.1)), kStill, {{P::Mode, 2.0f}, {P::Warmth, 100.0f}, {P::Mix, 100.0f}});
    const int at = samples(0.02) - 64;
    metrics["warmth100.tone_8k_db"] = db(std::abs(responseAt(ir.channel(0) + at, 256, 8000.0, kRate)) /
                                         std::abs(responseAt(ir.channel(0) + at, 256, 1000.0, kRate)));
    // Each mode's defaults on pink noise: the level, and how far it moves (the spread of short-term
    // levels at 2 kHz, as the sweep passes).
    const auto pink = pinkNoise(samples(6.0), 11, 0.3f);
    constexpr const char* kNames[] = {"phaser", "flanger", "doubler"};
    for (int mode = 0; mode < 3; ++mode)
    {
        const auto out = renderWith(pink, {}, {{P::Mode, static_cast<float>(mode)}});
        metrics[std::string(kNames[mode]) + ".pink_level_db"] =
            db(rmsOver(out, 0, samples(1.0), samples(6.0)) / rmsOver(pink, 0, samples(1.0), samples(6.0)));
    }
    // The default phaser's response at its centre: its notches' depth and the peak between them.
    const auto r = responseOf(P::ModeKind::Phaser, 2, 0.5);
    double lowest = 1e9, highest = 0.0;
    for (double hz = 100.0; hz < 8000.0; hz *= 1.001)
    {
        lowest = std::min(lowest, P::response(r, 800.0, hz, kRate));
        highest = std::max(highest, P::response(r, 800.0, hz, kRate));
    }
    metrics["phaser.default_notch_db"] = std::max(-60.0, db(lowest)); // deeper is "a notch"
    metrics["phaser.default_peak_db"] = db(highest);

    const auto result =
        checkGolden(std::string(FD_GOLDEN_DIR) + "/phaser-flanger/character.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
