// Gate (M2 step B2): the automated test targets of devices/gate/SPEC.md.

#include "Gate.h"

#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using G = fd::devices::Gate;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<G::Param, float>>;

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings settings, const AudioBuffer* key = nullptr)
{
    G g;
    setAll(g, settings);
    RenderOptions o;
    o.sampleRate = kRate;
    o.sidechain = key;
    return render(g, in, o);
}

/// A constant (DC) level in both channels, segment by segment.
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

/// A DC level that follows `db(i)`.
AudioBuffer levelCurve(int n, const std::function<double(int)>& db)
{
    AudioBuffer b(2, n);
    for (int i = 0; i < n; ++i)
        b.at(0, i) = b.at(1, i) = static_cast<float>(std::pow(10.0, db(i) / 20.0));
    return b;
}

double inputDb(const AudioBuffer& in, int i)
{
    return 20.0 * std::log10(std::abs(in.at(0, i)));
}

double gainDb(const AudioBuffer& in, const AudioBuffer& out, int i)
{
    return 20.0 * std::log10(std::max(1e-12, static_cast<double>(std::abs(out.at(0, i)))) / std::abs(in.at(0, i)));
}

/// The first sample from `start` where the gain crosses `db` (upwards or downwards).
int firstCrossing(const AudioBuffer& in, const AudioBuffer& out, int start, double db, bool upwards)
{
    for (int i = start; i < out.numSamples(); ++i)
        if (upwards ? gainDb(in, out, i) >= db : gainDb(in, out, i) <= db)
            return i;
    return -1;
}

double seconds(int samples)
{
    return samples / kRate;
}
} // namespace

TEST_CASE("Gate: opens at the threshold and closes at threshold minus return", "[fast]")
{
    // Slow ramps: up from -60 to -20 dB and back over 8 s each (5 dB/s).
    constexpr int ramp = static_cast<int>(8 * kRate);
    const auto x = levelCurve(2 * ramp, [](int i) {
        const double t = i < ramp ? double(i) / ramp : 2.0 - double(i) / ramp;
        return -60.0 + 40.0 * t;
    });
    const auto y = renderWith(x, {{G::Threshold, -40.0f}, {G::Return, 3.0f}, {G::Range, -40.0f}, {G::Attack, 0.02f},
                                  {G::Hold, 0.0f}, {G::Release, 1.0f}});
    const int opened = firstCrossing(x, y, 0, -20.0, true);
    const int closed = firstCrossing(x, y, ramp, -20.0, false);
    REQUIRE(opened > 0);
    REQUIRE(closed > ramp);
    CHECK(inputDb(x, opened) == Approx(-40.0).margin(0.1));
    CHECK(inputDb(x, closed) == Approx(-43.0).margin(0.1));
}

TEST_CASE("Gate: attack, hold and release times", "[fast]")
{
    const int half = static_cast<int>(kRate / 2), second = static_cast<int>(kRate);
    const auto x = level({{-60.0, half}, {-20.0, second}, {-60.0, second}});
    const auto y = renderWith(x, {{G::Threshold, -40.0f}, {G::Range, -40.0f}, {G::Attack, 10.0f}, {G::Hold, 50.0f},
                                  {G::Release, 100.0f}});
    // Attack: 10 to 90 % of the way from the range to 0 dB.
    const int a0 = firstCrossing(x, y, half, -36.0, true), a1 = firstCrossing(x, y, half, -4.0, true);
    CHECK(seconds(a1 - a0) == Approx(0.010).epsilon(0.05));
    // Hold: from the level dropping to the release starting.
    const int drop = half + second;
    const int released = firstCrossing(x, y, drop, -0.01, false);
    CHECK(seconds(released - drop) == Approx(0.050).epsilon(0.05));
    // Release: to within -60 dB (a thousandth) of the way to the range.
    const int done = firstCrossing(x, y, released, -40.0 + 0.04, false);
    CHECK(seconds(done - released) == Approx(0.100).epsilon(0.05));
}

TEST_CASE("Gate: Range -inf closes to silence, -20 dB turns down by 20 dB", "[fast]")
{
    const int half = static_cast<int>(kRate / 2);
    const auto quiet = level({{-60.0, half}});
    CHECK(peak(renderWith(quiet, {})) == 0.0f); // closed from the start
    const auto ranged = renderWith(quiet, {{G::Range, -20.0f}});
    CHECK(gainDb(quiet, ranged, half - 1) == Approx(-20.0).margin(0.05));

    // Open, then closed: silence once the release is over.
    const auto x = level({{-20.0, half}, {-60.0, 2 * half}});
    const auto y = renderWith(x, {});
    CHECK(peak(y.slice(2 * half, half)) < 1e-6f); // < -120 dBFS
    CHECK(G().info().params[G::Range].toText(-80.0f) == "-inf dB");
}

TEST_CASE("Gate: Flip passes exactly what the gate takes away", "[fast]")
{
    // Noise bursts above and below the threshold.
    auto x = whiteNoise(48000, 7, 0.5f, 2);
    for (int i = 0; i < x.numSamples(); ++i)
    {
        const float env = (i / 6000) % 2 == 0 ? 1.0f : 0.003f;
        x.at(0, i) *= env;
        x.at(1, i) *= env;
    }
    for (float lookahead : {0.0f, 2.0f})
    {
        INFO("lookahead " << lookahead);
        const auto gated = renderWith(x, {{G::Lookahead, lookahead}, {G::Range, -30.0f}});
        const auto flipped = renderWith(x, {{G::Lookahead, lookahead}, {G::Range, -30.0f}, {G::Flip, 1.0f}});
        const int latency = lookahead > 0.0f ? 480 : 0;
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = latency; i < x.numSamples(); ++i)
                worst = std::max(worst, std::abs(double(gated.at(c, i)) + flipped.at(c, i) - x.at(c, i - latency)));
        CHECK(worst < 1e-6); // < -120 dBFS
        CHECK(rms(flipped) > 0.0003); // and Flip isn't silent: it passes the quiet parts
    }
}

TEST_CASE("Gate: no chatter around the threshold", "[fast]")
{
    // After a quiet start, a level wandering +-1 dB around the threshold, crossing it many times.
    const int n = static_cast<int>(2 * kRate);
    const auto x = levelCurve(n, [](int i) {
        if (i < 4800)
            return -60.0;
        const double t = i / kRate;
        return -40.0 + 0.6 * std::sin(2.0 * 3.14159265 * 3.0 * t) + 0.4 * std::sin(2.0 * 3.14159265 * 7.3 * t);
    });
    auto transitions = [&](float ret) {
        const auto y = renderWith(x, {{G::Return, ret}, {G::Range, -40.0f}, {G::Attack, 0.02f}, {G::Hold, 0.0f},
                                      {G::Release, 1.0f}});
        int count = 0;
        bool open = gainDb(x, y, 0) > -20.0;
        for (int i = 1; i < n; ++i)
            if (const bool now = gainDb(x, y, i) > -20.0; now != open)
            {
                open = now;
                ++count;
            }
        return count;
    };
    CHECK(transitions(3.0f) == 1); // opens once and stays open
    CHECK(transitions(0.0f) > 4);  // without hysteresis it would chatter
}

TEST_CASE("Gate: latency follows Lookahead, and lookahead keeps the attack", "[fast]")
{
    for (const auto& [setting, samples] : {std::pair{0.0f, 0}, std::pair{1.0f, 48}, std::pair{2.0f, 480}})
    {
        G g;
        g.params().set(G::Lookahead, setting);
        g.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        CHECK(g.latencySamples() == samples);
    }
    // A hit after silence: with 10 ms lookahead the gate is already open when it arrives (it
    // comes out 480 samples late); without, its first sample is still cut.
    const auto x = level({{-80.0, 4800}, {-10.0, 4800}});
    const auto ahead = renderWith(x, {{G::Lookahead, 2.0f}, {G::Attack, 1.0f}});
    CHECK(20.0 * std::log10(std::abs(ahead.at(0, 4800 + 480)) / x.at(0, 4800)) == Approx(0.0).margin(0.1));
    const auto late = renderWith(x, {{G::Attack, 1.0f}});
    CHECK(std::abs(late.at(0, 4800)) < 0.01f * x.at(0, 4800));
}

TEST_CASE("Gate: the expander follows its curve and stops at the range", "[fast]")
{
    for (float ratio : {2.0f, 4.0f})
        for (double in : {-70.0, -50.0, -40.0, -30.0})
        {
            const auto x = level({{in, 24000}});
            const auto y = renderWith(x, {{G::Mode, 1.0f}, {G::Ratio, ratio}, {G::Range, -20.0f}});
            INFO("ratio " << ratio << ", " << in << " dB");
            CHECK(gainDb(x, y, 23999) == Approx(G::expanderGain(float(in), -40.0f, ratio, -20.0f)).margin(0.05));
        }
    CHECK(G::expanderGain(-50.0f, -40.0f, 2.0f, -80.0f) == Approx(-10.0f));
    CHECK(G::expanderGain(-70.0f, -40.0f, 4.0f, -20.0f) == Approx(-20.0f));
}

TEST_CASE("Gate: a sidechain key opens and closes the gate", "[fast]")
{
    const int half = static_cast<int>(kRate / 2);
    const auto input = level({{-20.0, 3 * half}});
    const auto key = level({{-80.0, half}, {-10.0, half}, {-80.0, half}});
    const auto y = renderWith(input, {{G::Range, -40.0f}}, &key);
    CHECK(gainDb(input, y, half - 1) == Approx(-40.0).margin(0.05)); // the key is quiet: closed
    CHECK(gainDb(input, y, 2 * half - 1) == Approx(0.0).margin(0.05)); // the key plays: open
    CHECK(gainDb(input, y, 3 * half - 1) == Approx(-40.0).margin(0.05));
}

TEST_CASE("Gate: Mode, Flip, Listen and sidechain filter changes crossfade", "[fast]")
{
    const auto input = stereoSines(1000.0, 1500.0, kRate, 48000, 0.9f);
    struct Case
    {
        G::Param param;
        float from, to;
        Settings settings;
    };
    const Case cases[] = {
        {G::Mode, 0.0f, 1.0f, {{G::Threshold, 0.0f}}}, // silent as a gate, nearly full as an expander
        {G::Mode, 1.0f, 0.0f, {{G::Threshold, 0.0f}, {G::Release, 1.0f}}},
        {G::Flip, 0.0f, 1.0f, {}},
        {G::Flip, 1.0f, 0.0f, {}},
        {G::ScListen, 0.0f, 1.0f, {{G::ScFilter, 2.0f}, {G::ScFreq, 300.0f}}},
        {G::ScFilter, 0.0f, 2.0f, {{G::ScListen, 1.0f}, {G::ScFreq, 1200.0f}}},
    };
    for (const auto& k : cases)
    {
        G g;
        setAll(g, k.settings);
        g.params().set(k.param, k.from);
        RenderOptions o{.sampleRate = kRate};
        o.beforeBlock = [&](Device& d, std::int64_t start) {
            if (start >= 24000)
                d.params().set(k.param, k.to);
        };
        const auto out = render(g, input, o).slice(12000, 36000);
        for (int ch = 0; ch < 2; ++ch)
        {
            INFO("param " << g.info().params[k.param].id << " " << k.from << " -> " << k.to << ", channel " << ch);
            CHECK(clickScore(out, ch) < 3.0);
        }
    }
}

TEST_CASE("Gate: meters and the level history", "[fast]")
{
    G g;
    const auto x = level({{-10.0, 48000}});
    render(g, x, {.sampleRate = kRate});
    CHECK(g.analysis().takePeak(G::Open) == 1.0f);
    CHECK(g.analysis().takePeak(G::PeakInL) == Approx(std::pow(10.0, -0.5)).margin(1e-3));
    CHECK(g.analysis().takePeak(G::DetectorLevel) == Approx(std::pow(10.0, -0.5)).margin(1e-3));
    CHECK(g.analysis().takePeak(G::GainReduction) > 50.0f); // it started closed
    auto& fifo = g.analysis().fifo(G::historyFifo);
    CHECK(fifo.available() >= 3 * 90); // a frame every 10 ms
    float frame[3] = {};
    fifo.pop(frame, 3);
    CHECK(frame[0] == Approx(-10.0f).margin(0.01));
    CHECK(frame[2] == 1.0f);
}

TEST_CASE("Gate curves and timing match golden metrics", "[fast]")
{
    nlohmann::json metrics;
    for (float ratio : {2.0f, 4.0f})
        for (double in : {-70.0, -50.0, -30.0})
        {
            const auto x = level({{in, 24000}});
            const auto y = renderWith(x, {{G::Mode, 1.0f}, {G::Ratio, ratio}, {G::Range, -30.0f}});
            metrics["expander.r" + std::to_string(int(ratio)) + ".in" + std::to_string(int(in))] = gainDb(x, y, 23999);
        }
    // A drum-like hit through the gate at its defaults: what gets through, and when.
    const auto hit = level({{-70.0, 4800}, {-6.0, 2400}, {-30.0, 2400}, {-70.0, 14400}});
    const auto gated = renderWith(hit, {});
    metrics["gate.hit_rms_db"] = 20.0 * std::log10(rms(gated));
    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/gate/curve.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
