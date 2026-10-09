// Compressor (M2 step B1): the automated test targets of devices/compressor/SPEC.md.

#include "Compressor.h"

#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <initializer_list>
#include <utility>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using C = fd::devices::Compressor;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<C::Param, float>>;

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings settings, const AudioBuffer* key = nullptr)
{
    C c;
    setAll(c, settings);
    RenderOptions o;
    o.sampleRate = kRate;
    o.sidechain = key;
    return render(c, in, o);
}

/// A constant (DC) level in both channels: the cleanest way to see the gain.
AudioBuffer level(std::initializer_list<std::pair<double, int>> segments)
{
    int total = 0;
    for (const auto& s : segments)
        total += s.second;
    AudioBuffer b(2, total);
    int at = 0;
    for (const auto& [db, n] : segments)
    {
        for (int i = 0; i < n; ++i)
            b.at(0, at + i) = b.at(1, at + i) = static_cast<float>(std::pow(10.0, db / 20.0));
        at += n;
    }
    return b;
}

double gainDb(const AudioBuffer& in, const AudioBuffer& out, int i)
{
    return 20.0 * std::log10(std::abs(out.at(0, i)) / std::abs(in.at(0, i)));
}

/// Seconds for the gain to go from `from` to `to` dB, starting at sample `start`.
double timeBetween(const AudioBuffer& in, const AudioBuffer& out, int start, double from, double to)
{
    const bool falling = to < from;
    int first = -1;
    for (int i = start; i < out.numSamples(); ++i)
    {
        const double g = gainDb(in, out, i);
        if (first < 0 && (falling ? g <= from : g >= from))
            first = i;
        if (falling ? g <= to : g >= to)
            return (i - std::max(first, start)) / kRate;
    }
    return -1.0;
}
} // namespace

TEST_CASE("Compressor: steady levels land on the static curve within 0.05 dB", "[fast]")
{
    for (float ratio : {1.5f, 2.0f, 4.0f, 10.0f, C::infiniteRatio})
        for (float knee : {0.0f, 6.0f, 18.0f})
            for (double in : {-60.0, -40.0, -24.0, -18.0, -12.0, -6.0, 0.0})
            {
                INFO("ratio " << ratio << " knee " << knee << " in " << in);
                const auto x = level({{in, 24000}});
                const auto y = renderWith(x, {{C::Threshold, -24.0f}, {C::Ratio, ratio}, {C::Knee, knee},
                                              {C::Attack, 0.01f}, {C::Release, 3000.0f}});
                const double expected = C::curve(static_cast<float>(in), -24.0f, ratio, knee) - in;
                CHECK(gainDb(x, y, 23999) == Approx(expected).margin(0.05));
            }
}

TEST_CASE("Compressor: attack and release take their set times, within 5 %", "[fast]")
{
    // +20 dB steps into a limiter curve: 20 dB of reduction comes and goes.
    for (float attack : {1.0f, 10.0f, 100.0f})
        for (float release : {50.0f, 150.0f, 1000.0f})
        {
            INFO("attack " << attack << " release " << release);
            const int step = static_cast<int>(kRate * 3);
            const auto x = level({{-40.0, step}, {-20.0, step}, {-40.0, step}});
            const auto y = renderWith(x, {{C::Threshold, -40.0f}, {C::Ratio, C::infiniteRatio}, {C::Knee, 0.0f},
                                          {C::Attack, attack}, {C::Release, release}});
            CHECK(timeBetween(x, y, step, -2.0, -18.0) == Approx(attack * 0.001).epsilon(0.05));
            CHECK(timeBetween(x, y, 2 * step, -20.0, -0.02) == Approx(release * 0.001).epsilon(0.05));
        }
}

TEST_CASE("Compressor: an infinite ratio holds the threshold after its attack", "[fast]")
{
    const int step = static_cast<int>(kRate * 2);
    const auto x = level({{-40.0, step}, {-10.0, step}});
    const auto y = renderWith(x, {{C::Threshold, -30.0f}, {C::Ratio, C::infiniteRatio}, {C::Knee, 0.0f},
                                  {C::Attack, 300.0f}, {C::Release, 150.0f}});
    double overshoot = -200.0;
    for (int i = step; i < 2 * step; ++i)
        overshoot = std::max(overshoot, 20.0 * std::log10(std::abs(y.at(0, i))) + 30.0);
    UNSCOPED_INFO("overshoot above the threshold with the slowest attack: " << overshoot << " dB");
    CHECK(overshoot <= 20.0 + 1e-3); // bounded by the step itself: never a boost
    CHECK(20.0 * std::log10(std::abs(y.at(0, 2 * step - 1))) == Approx(-30.0).margin(0.05));
}

TEST_CASE("Compressor: Mix at 0 % and levels below the threshold null against the input", "[fast]")
{
    const auto noise = whiteNoise(48000, 9, 0.25f, 2);
    for (float lookahead : {0.0f, 1.0f, 2.0f})
    {
        INFO("lookahead " << lookahead);
        C c;
        setAll(c, {{C::Threshold, -40.0f}, {C::Ratio, 8.0f}, {C::Mix, 0.0f}, {C::Lookahead, lookahead}});
        const auto out = render(c, noise, {.sampleRate = kRate});
        const int latency = c.latencySamples();
        double worst = 0.0;
        for (int i = latency; i < noise.numSamples(); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(out.at(0, i) - noise.at(0, i - latency))));
        CHECK(20.0 * std::log10(worst + 1e-30) < -120.0);
    }
    // Below the threshold (and its knee) with no makeup: untouched.
    const auto quiet = whiteNoise(48000, 3, 0.05f, 2); // peaks under -20 dBFS
    const auto out = renderWith(quiet, {{C::Threshold, 0.0f}, {C::Knee, 6.0f}});
    double worst = 0.0;
    for (int i = 0; i < quiet.numSamples(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(out.at(1, i) - quiet.at(1, i))));
    CHECK(20.0 * std::log10(worst + 1e-30) < -120.0);
}

TEST_CASE("Compressor: latency follows Lookahead (ADR 0020)", "[fast]")
{
    C c;
    c.prepare({kRate, 512});
    CHECK(c.latencySamples() == 0);
    c.params().set(C::Lookahead, 1.0f);
    CHECK(c.latencySamples() == 48);
    c.params().set(C::Lookahead, 2.0f);
    CHECK(c.latencySamples() == 480);
    CHECK(c.info().params[C::Lookahead].hasFlag(ParamFlags::ChangesLatency));
    CHECK_FALSE(c.info().params[C::Lookahead].hasFlag(ParamFlags::Automatable));

    // With lookahead the reduction is in place when a step arrives: no overshoot.
    const int step = static_cast<int>(kRate);
    const auto x = level({{-40.0, step}, {-10.0, step}});
    const auto y = renderWith(x, {{C::Threshold, -30.0f}, {C::Ratio, C::infiniteRatio}, {C::Knee, 0.0f},
                                  {C::Attack, 0.1f}, {C::Lookahead, 2.0f}});
    double peak = -200.0;
    for (int i = step + 480; i < 2 * step; ++i)
        peak = std::max(peak, 20.0 * std::log10(std::abs(y.at(0, i))));
    CHECK(peak < -29.5);
}

TEST_CASE("Compressor: a sidechain key ducks the input within the attack time", "[fast]")
{
    const int half = static_cast<int>(kRate);
    const auto input = level({{-20.0, 2 * half}});
    auto key = level({{-80.0, half}, {0.0, half}});
    const auto y = renderWith(input, {{C::Threshold, -30.0f}, {C::Ratio, 4.0f}, {C::Knee, 0.0f}, {C::Attack, 5.0f}},
                              &key);
    CHECK(gainDb(input, y, half - 1) == Approx(0.0).margin(0.01)); // key quiet: untouched
    const double settled = C::curve(0.0f, -30.0f, 4.0f, 0.0f) - 0.0;
    CHECK(gainDb(input, y, 2 * half - 1) == Approx(settled).margin(0.05));
    CHECK(timeBetween(input, y, half, -0.1 * -settled, -0.9 * -settled) == Approx(0.005).epsilon(0.05));
}

TEST_CASE("Compressor: auto makeup, Listen, Glue and the infinity ratio text", "[fast]")
{
    // Auto makeup brings a steady -18 dBFS level back to -18 dBFS.
    const auto x = level({{-18.0, 24000}});
    const auto y = renderWith(x, {{C::Threshold, -30.0f}, {C::Ratio, 3.0f}, {C::Knee, 0.0f}, {C::Attack, 0.01f},
                                  {C::Release, 3000.0f}, {C::MakeupAuto, 1.0f}});
    CHECK(gainDb(x, y, 23999) == Approx(0.0).margin(0.05));
    CHECK(C::autoMakeup(-30.0f, 3.0f, 0.0f) == Approx(8.0f));

    // Listen plays the filtered detector signal: a 2 kHz low-pass keeps about a quarter of white noise.
    const auto noise = whiteNoise(24000, 4, 0.25f, 2);
    const auto heard = renderWith(noise, {{C::ScListen, 1.0f}, {C::ScFilter, 2.0f}, {C::ScFreq, 2000.0f}});
    CHECK(rms(heard) < rms(noise) * 0.4);

    // Glue: feedback detection stays stable, and its soft clip keeps peaks under full scale.
    const auto loud = whiteNoise(48000, 5, 0.9f, 2);
    const auto glued = renderWith(loud, {{C::Character, 1.0f}, {C::Makeup, 24.0f}, {C::Threshold, -40.0f}});
    CHECK(allFinite(glued));
    CHECK(peak(glued) <= 1.0f);

    const auto& ratio = C().info().params[C::Ratio];
    CHECK(ratio.toText(C::infiniteRatio) == "\xE2\x88\x9E:1");
    CHECK(ratio.toText(4.0f) == "4.0:1");
    CHECK(ratio.fromText("inf") == C::infiniteRatio);
}

TEST_CASE("Compressor: Character, Detector, sidechain filter and Auto Makeup changes crossfade", "[fast]")
{
    // Driven hard enough that Glue's clip is working and with Listen on, where a switch would
    // be heard at once (the conformance stimulus stays in the clean region).
    const auto input = stereoSines(1000.0, 1500.0, kRate, 48000, 0.9f);
    struct Case
    {
        C::Param param;
        float from, to;
        Settings settings;
    };
    const Case cases[] = {
        {C::Character, 0.0f, 1.0f, {{C::Threshold, -12.0f}, {C::Ratio, 2.0f}, {C::Makeup, 12.0f}}},
        {C::Character, 1.0f, 0.0f, {{C::Threshold, -12.0f}, {C::Ratio, 2.0f}, {C::Makeup, 12.0f}}},
        {C::Detector, 0.0f, 1.0f,
         {{C::Threshold, -30.0f}, {C::Ratio, C::infiniteRatio}, {C::Attack, 0.01f}, {C::Release, 3000.0f}}},
        {C::Detector, 1.0f, 0.0f,
         {{C::Threshold, -30.0f}, {C::Ratio, C::infiniteRatio}, {C::Attack, 0.01f}, {C::Release, 3000.0f}}},
        {C::ScFilter, 0.0f, 2.0f, {{C::ScListen, 1.0f}, {C::ScFreq, 1200.0f}}},
        {C::ScFilter, 2.0f, 1.0f, {{C::ScListen, 1.0f}, {C::ScFreq, 1200.0f}}},
        {C::MakeupAuto, 0.0f, 1.0f, {{C::Threshold, -40.0f}, {C::Ratio, 8.0f}}},
        {C::MakeupAuto, 1.0f, 0.0f, {{C::Threshold, -40.0f}, {C::Ratio, 8.0f}}},
    };
    for (const auto& k : cases)
    {
        C c;
        setAll(c, k.settings);
        c.params().set(k.param, k.from);
        RenderOptions o{.sampleRate = kRate};
        o.beforeBlock = [&](Device& d, std::int64_t start) {
            if (start >= 24000)
                d.params().set(k.param, k.to);
        };
        const auto out = render(c, input, o).slice(12000, 36000);
        for (int ch = 0; ch < 2; ++ch)
        {
            INFO("param " << c.info().params[k.param].id << " " << k.from << " -> " << k.to << ", channel " << ch);
            CHECK(clickScore(out, ch) < 3.0); // an instant switch scores 4 to 55 here
        }
    }
}

TEST_CASE("Compressor: meters and the reduction history", "[fast]")
{
    C c;
    const auto x = level({{-10.0, 48000}});
    render(c, x, {.sampleRate = kRate});
    CHECK(c.analysis().takePeak(C::GainReduction) > 1.0f);
    CHECK(c.analysis().takePeak(C::PeakInL) == Approx(std::pow(10.0, -0.5)).margin(1e-3));
    CHECK(c.analysis().takePeak(C::PeakInR) == Approx(std::pow(10.0, -0.5)).margin(1e-3));
    CHECK(c.analysis().takePeak(C::DetectorLevel) == Approx(std::pow(10.0, -0.5)).margin(1e-3));
    auto& fifo = c.analysis().fifo(C::historyFifo);
    CHECK(fifo.available() >= 3 * 90); // a frame every 10 ms
    float frame[3] = {};
    fifo.pop(frame, 3);
    CHECK(frame[0] == Approx(-10.0f).margin(0.01));
}

TEST_CASE("Compressor static curve and timing match golden metrics", "[fast]")
{
    nlohmann::json metrics;
    for (float ratio : {2.0f, 4.0f, C::infiniteRatio})
        for (double in : {-40.0, -24.0, -12.0, 0.0})
        {
            const auto x = level({{in, 24000}});
            const auto y = renderWith(x, {{C::Threshold, -24.0f}, {C::Ratio, ratio}, {C::Attack, 0.01f},
                                          {C::Release, 3000.0f}});
            metrics["gain.r" + std::to_string(static_cast<int>(ratio)) + ".in" + std::to_string(static_cast<int>(in))] =
                gainDb(x, y, 23999);
        }
    // Glue at default settings on a loud drum-like burst: its overall reduction.
    const auto burst = level({{-6.0, 4800}, {-30.0, 19200}, {-6.0, 4800}, {-30.0, 19200}});
    const auto glued = renderWith(burst, {{C::Character, 1.0f}});
    metrics["glue.burst_rms_db"] = 20.0 * std::log10(rms(glued));
    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/compressor/curve.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
