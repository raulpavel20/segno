// Auto Pan & Tremolo (M3 step B4): the automated test targets of devices/auto-pan-tremolo/SPEC.md.

#include "AutoPanTremolo.h"
#include "fd/device/Realtime.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using A = fd::devices::AutoPanTremolo;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<A::Param, float>>;

/// Free at a slow sine, so a level or a place can be read sample by sample.
constexpr Settings kSlow{{A::Sync, 0.0f}, {A::Rate, 0.5f}, {A::Shape, 0.0f}, {A::Amount, 100.0f}};

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings base, Settings settings, RenderOptions o = {})
{
    A d;
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

/// The LFO's position at sample `n`, free from 0 at `rate`.
double positionAt(int n, double rate)
{
    return n * rate / kRate;
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

/// The level of channel `c` in windows of `window` samples (RMS).
std::vector<double> envelope(const AudioBuffer& b, int c, int window)
{
    std::vector<double> out;
    for (int i = 0; i + window <= b.numSamples(); i += window)
        out.push_back(rmsOver(b, c, i, i + window));
    return out;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
    const auto n = static_cast<double>(std::min(a.size(), b.size()));
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i)
    {
        ma += a[i] / n;
        mb += b[i] / n;
    }
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(saa * sbb);
}

int crossings(const std::vector<double>& v, double around)
{
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i)
        n += (v[i - 1] < around) != (v[i] < around);
    return n;
}
} // namespace

TEST_CASE("Auto Pan & Tremolo: the wave, Edge, the levels and the balance", "[autopan][fast]")
{
    // Edge: harder at +100 %, softer at -100 %, nothing at 0.
    CHECK(A::edged(0.5, 0.0) == 0.5);
    CHECK(A::edged(-0.5, 1.0) == -std::pow(0.5, 0.125));
    int beyond = 0, within = 0;
    constexpr int steps = 10000;
    for (int i = 0; i < steps; ++i)
    {
        const double v = std::sin(2.0 * std::numbers::pi * i / steps);
        beyond += std::abs(A::edged(v, 1.0)) > 0.9;
        within += std::abs(A::edged(v, -1.0)) < 0.5;
    }
    CHECK(beyond > 0.7 * steps); // pushed to the extremes
    CHECK(within > 0.7 * steps); // eased towards the middle

    // The wave: the LFO's value at the position plus Phase, flipped by Invert.
    const A::Wave w{.shape = 0, .phase = 0.25};
    CHECK(std::abs(A::wave(w, 0.0) - 1.0) < 1e-6);
    CHECK(std::abs(A::wave({.shape = 0, .phase = 0.25, .invert = true}, 0.0) + 1.0) < 1e-6);

    // The levels: full at the top, 1 - Amount at the bottom; the mirror the other way round.
    CHECK(A::level(0.5, 1.0, false) == 1.0);
    CHECK(A::level(0.5, -1.0, false) == 0.5);
    CHECK(A::level(0.5, -1.0, true) == 0.25);
    CHECK(A::mirrorLevel(0.5, 1.0, false) == 0.5);

    // The balance: as it was in the middle, equal power everywhere.
    const auto middle = A::panGains(0.0);
    CHECK(std::abs(middle.first - 1.0) < 1e-12);
    CHECK(std::abs(middle.second - 1.0) < 1e-12);
    for (const double place : {-1.0, -0.5, 0.3, 1.0})
    {
        const auto g = A::panGains(place);
        CHECK(std::abs(g.first * g.first + g.second * g.second - 2.0) < 1e-12);
    }
    CHECK(std::abs(A::panGains(-1.0).second) < 1e-12);
}

TEST_CASE("Auto Pan & Tremolo: Panning moves the sound at equal power, as wave() says", "[autopan][fast]")
{
    // A steady input: the output is each side's gain.
    const auto out = renderWith(dc(samples(4.0), 0.5f), kSlow, {{A::Mode, 0.0f}});
    const A::Wave w{};
    for (int i = samples(0.05); i < out.numSamples(); i += 97)
    {
        const double l = out.at(0, i) / 0.5, r = out.at(1, i) / 0.5;
        INFO("sample " << i);
        CHECK(std::abs(db((l * l + r * r) / 2.0)) < 0.05);
        const double place = std::atan2(r, l) * 4.0 / std::numbers::pi - 1.0;
        CHECK(std::abs(place - A::wave(w, positionAt(i, 0.5))) < 0.01);
    }
}

TEST_CASE("Auto Pan & Tremolo: Tremolo's level follows level(), down to silence", "[autopan][fast]")
{
    const A::Wave w{};
    for (const float amount : {50.0f, 100.0f})
    {
        INFO("amount " << amount);
        const auto out = renderWith(dc(samples(4.0), 0.5f), kSlow, {{A::Mode, 1.0f}, {A::Amount, amount}});
        // The level, through the 1 ms one-pole the device moves it with, from where it starts (full).
        const double k = 1.0 - std::exp(-1.0 / (0.001 * kRate));
        double smoothed = 1.0, lowest = 1.0;
        for (int i = 0; i < out.numSamples(); ++i)
        {
            smoothed += k * (A::level(amount * 0.01, A::wave(w, positionAt(i, 0.5)), false) - smoothed);
            const double measured = out.at(0, i) / 0.5;
            lowest = std::min(lowest, measured);
            if (smoothed > 0.05 && i % 97 == 0)
                CHECK(std::abs(db(measured / smoothed)) < 0.1);
        }
        if (amount == 100.0f)
            CHECK(db(lowest) < -100.0);
        else
            CHECK(std::abs(db(lowest) - db(0.5)) < 0.05);
    }
}

TEST_CASE("Auto Pan & Tremolo: Offset and Spin set the sides apart", "[autopan][fast]")
{
    SECTION("Offset 0: a mono input stays mono")
    {
        const auto in = pinkNoise(samples(1.0), 5, 0.4f);
        AudioBuffer mono(2, in.numSamples());
        for (int i = 0; i < in.numSamples(); ++i)
            mono.at(0, i) = mono.at(1, i) = in.at(0, i);
        const auto out = renderWith(mono, {}, {{A::Mode, 1.0f}, {A::Amount, 80.0f}, {A::Vintage, 1.0f}});
        bool same = true;
        for (int i = 0; i < out.numSamples(); ++i)
            same = same && out.at(0, i) == out.at(1, i);
        CHECK(same);
    }
    SECTION("Offset 180: one side's level is the other's mirror")
    {
        const auto out = renderWith(dc(samples(2.0), 0.5f), kSlow,
                                    {{A::Mode, 1.0f}, {A::Amount, 60.0f}, {A::Offset, 180.0f}});
        for (int i = samples(0.1); i < out.numSamples(); i += 101)
            CHECK(std::abs((out.at(0, i) + out.at(1, i)) / 0.5 - (2.0 - 0.6)) < 0.01);
    }
    SECTION("Spin 100 %: twice as many cycles on the right")
    {
        const auto out = renderWith(dc(samples(4.0), 0.5f), {},
                                    {{A::Mode, 1.0f},
                                     {A::Sync, 0.0f},
                                     {A::Rate, 2.0f},
                                     {A::Amount, 100.0f},
                                     {A::Stereo, 1.0f},
                                     {A::Spin, 100.0f}});
        std::vector<double> left, right;
        for (int i = 0; i < out.numSamples(); i += 48)
        {
            left.push_back(out.at(0, i));
            right.push_back(out.at(1, i));
        }
        CHECK(std::abs(crossings(left, 0.25) - 16) <= 1); // 2 Hz for 4 s
        CHECK(std::abs(crossings(right, 0.25) - 32) <= 1);
    }
}

TEST_CASE("Auto Pan & Tremolo: Harmonic pulses the lows and the highs in turn", "[autopan][fast]")
{
    const Settings harmonic{
        {A::Mode, 1.0f}, {A::Sync, 0.0f}, {A::Rate, 2.0f}, {A::Amount, 100.0f}, {A::Harmonic, 1.0f}};
    const auto low = renderWith(sine(200.0, kRate, samples(3.0), 0.3f), {}, harmonic);
    const auto high = renderWith(sine(3000.0, kRate, samples(3.0), 0.3f), {}, harmonic);
    CHECK(correlation(envelope(low, 0, 480), envelope(high, 0, 480)) < -0.9);
}

TEST_CASE("Auto Pan & Tremolo: a square wave chops cleanly", "[autopan][fast]")
{
    const auto out = renderWith(
        sine(1000.0, kRate, samples(2.0), 0.5f), {},
        {{A::Mode, 1.0f}, {A::Sync, 0.0f}, {A::Rate, 4.0f}, {A::Shape, 4.0f}, {A::Amount, 100.0f}});
    for (int c = 0; c < 2; ++c)
        CHECK(clickScore(out.slice(samples(0.2), samples(1.5)), c) < 8.0);
}

TEST_CASE("Auto Pan & Tremolo: synced, Phase moves the cycle against the beat", "[autopan][fast]")
{
    // A cycle a beat (1/4) at 120 BPM: a beat in, the sine is back where it started.
    const auto at = [&](float phase)
    {
        const auto out =
            renderWith(dc(samples(0.5), 0.5f), {},
                       {{A::Mode, 1.0f}, {A::Div, 8.0f}, {A::Amount, 100.0f}, {A::Phase, phase}});
        return out.at(0, samples(0.5) - 1) / 0.5;
    };
    CHECK(std::abs(at(0.0f) - 0.5) < 0.01);  // sin(0): half way
    CHECK(std::abs(at(90.0f) - 1.0) < 0.01); // a quarter on: the top
    CHECK(at(270.0f) < 0.01);                // three quarters: the bottom

    // Played through, or started later: the same place.
    RenderOptions through;
    through.sampleRate = kRate;
    A a;
    setAll(a, {{A::Div, 8.0f}});
    (void)render(a, silence(samples(2.0)), through);
    RenderOptions later = through;
    later.transport.ppqPosition = 2.0;
    A b;
    setAll(b, {{A::Div, 8.0f}});
    (void)render(b, silence(samples(1.0)), later);
    const double gap = a.analysis().read(A::LfoPosition) - b.analysis().read(A::LfoPosition);
    CHECK(std::abs(gap - std::round(gap)) < 1e-4);
    CHECK(a.analysis().read(A::RateHz) == 2.0f);
}

TEST_CASE("Auto Pan & Tremolo: Attack lets a note through, then the movement fades in", "[autopan][fast]")
{
    // Silence, then a burst at 100 ms.
    auto in = sine(1000.0, kRate, samples(0.6), 0.5f);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < samples(0.1); ++i)
            in.at(c, i) = 0.0f;
    const Settings settings{
        {A::Mode, 1.0f}, {A::Sync, 0.0f}, {A::Rate, 4.0f}, {A::Amount, 100.0f}, {A::Attack, 200.0f}};
    const auto out = renderWith(in, {}, settings);
    // The first 5 ms: at the input's level, within 0.3 dB.
    CHECK(std::abs(db(rmsOver(out, 0, samples(0.1), samples(0.105)) /
                      rmsOver(in, 0, samples(0.1), samples(0.105)))) < 0.3);
    const auto rampAfter = [&](double seconds)
    {
        A d;
        setAll(d, settings);
        RenderOptions o;
        o.sampleRate = kRate;
        (void)render(d, in.slice(0, samples(seconds)), o);
        return d.analysis().read(A::AttackRamp);
    };
    CHECK(rampAfter(0.29) < 1.0f);
    CHECK(rampAfter(0.305) == 1.0f); // full depth Attack later, within 5 ms
}

TEST_CASE("Auto Pan & Tremolo: Low Cut keeps the lows unmoved", "[autopan][fast]")
{
    const int length = samples(3.0), from = samples(1.0), to = samples(3.0);
    const auto low = sine(40.0, kRate, length, 0.25f);
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(low, {},
                                    {{A::Mode, mode},
                                     {A::Sync, 0.0f},
                                     {A::Rate, 3.0f},
                                     {A::Amount, 100.0f},
                                     {A::LowCut, 1.0f},
                                     {A::LowCutFreq, 200.0f}});
        for (int c = 0; c < 2; ++c)
        {
            CHECK(std::abs(db(rmsOver(out, c, from, to) / rmsOver(low, c, from, to))) < 0.1);
            CHECK(strayDb(out, c, 40.0, from, to) < -50.0);
        }
    }
}

TEST_CASE("Auto Pan & Tremolo: at Amount 0 the output is the input", "[autopan][fast]")
{
    const auto in = pinkNoise(samples(2.0), 3, 0.5f);
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("mode " << mode);
        const auto out = renderWith(in, {}, {{A::Mode, mode}, {A::Amount, 0.0f}, {A::Harmonic, 1.0f}});
        if (mode == 0.0f)
            CHECK(nullResidualDb(in, out) < -120.0);
        else // Harmonic splits the sound; without it, the input exactly
            CHECK(nullResidualDb(in, renderWith(in, {}, {{A::Mode, mode}, {A::Amount, 0.0f}})) < -120.0);
    }
}

TEST_CASE("Auto Pan & Tremolo: its character matches golden metrics", "[autopan][fast]")
{
    nlohmann::json metrics;
    // Vintage: the optical curve on a steady input (its mean and its lowest level), and the
    // saturation on a loud tone.
    const auto vintage = renderWith(
        dc(samples(4.0), 0.5f), {},
        {{A::Mode, 1.0f}, {A::Sync, 0.0f}, {A::Rate, 2.0f}, {A::Amount, 80.0f}, {A::Vintage, 1.0f}});
    double lowest = 1.0, sum = 0.0;
    for (int i = samples(1.0); i < vintage.numSamples(); ++i)
    {
        lowest = std::min(lowest, static_cast<double>(vintage.at(0, i)));
        sum += vintage.at(0, i);
    }
    metrics["vintage.mean_level_db"] = db(sum / (vintage.numSamples() - samples(1.0)) / 0.5);
    metrics["vintage.lowest_level_db"] = db(lowest / 0.5);
    constexpr int size = 4096, cycles = 85;
    const auto loud = renderWith(sine(binCentredFrequency(cycles, size, kRate), kRate, samples(0.5), 0.7f),
                                 {}, {{A::Mode, 1.0f}, {A::Amount, 0.0f}, {A::Vintage, 1.0f}});
    metrics["vintage.h3_minus3dbfs_db"] = harmonicLevelsDb(loud, 0, samples(0.2), size, cycles, 4)[2];
    // Each mode's defaults on pink noise: the level.
    const auto pink = pinkNoise(samples(4.0), 11, 0.3f);
    for (const float mode : {0.0f, 1.0f})
    {
        const auto out = renderWith(pink, {}, {{A::Mode, mode}});
        metrics[std::string(mode == 0.0f ? "panning" : "tremolo") + ".pink_level_db"] =
            db(rmsOver(out, 0, samples(0.5), samples(4.0)) / rmsOver(pink, 0, samples(0.5), samples(4.0)));
    }

    const auto result =
        checkGolden(std::string(FD_GOLDEN_DIR) + "/auto-pan-tremolo/character.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
