// Limiter (M2 step B3): the automated test targets of devices/limiter/SPEC.md.

#include "Limiter.h"

#include "fd/dsp/Loudness.h"
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
using L = fd::devices::Limiter;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<L::Param, float>>;

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings settings)
{
    L l;
    setAll(l, settings);
    return render(l, in, {.sampleRate = kRate});
}

/// Hard material: loud noise, a low sine, and transients (single-sample spikes and bursts).
AudioBuffer torture(int n)
{
    auto b = whiteNoise(n, 11, 1.0f, 2);
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kRate);
        const float s = std::sin(2.0f * 3.14159265f * 55.0f * t) + 0.7f * std::sin(2.0f * 3.14159265f * 3137.0f * t);
        const float spike = i % 4801 == 0 ? 4.0f : 0.0f;
        b.at(0, i) = 0.5f * b.at(0, i) + s + spike;
        b.at(1, i) = 0.5f * b.at(1, i) - 0.8f * s + (i % 7919 == 0 ? -4.0f : 0.0f);
    }
    return b;
}

double maxTruePeak(const AudioBuffer& b)
{
    dsp::TruePeakDetector d;
    d.prepare(b.numChannels());
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            d.processSample(c, b.at(c, i));
    return std::max(static_cast<double>(d.peak()), static_cast<double>(peak(b)));
}
} // namespace

TEST_CASE("Limiter: the output never passes the ceiling", "[fast]")
{
    const auto x = torture(48000); // up to about +18 dB over full scale, and +24 dB of Gain on top
    for (float ceiling : {-0.3f, -6.0f})
        for (float gain : {0.0f, 24.0f})
            for (float mode : {0.0f, 1.0f})
                for (float link : {0.0f, 1.0f, 2.0f})
                    for (float lookahead : {0.0f, 2.0f})
                    {
                        INFO("ceiling " << ceiling << ", gain " << gain << ", mode " << mode << ", link " << link
                                        << ", lookahead " << lookahead);
                        const auto y = renderWith(x, {{L::Ceiling, ceiling}, {L::Gain, gain}, {L::Mode, mode},
                                                      {L::Link, link}, {L::Lookahead, lookahead},
                                                      {L::TruePeak, 0.0f}});
                        CHECK(peak(y) <= dsp::dbToGain(ceiling) * 1.000001f);
                    }
}

TEST_CASE("Limiter: with True Peak, the peaks between samples stay under the ceiling too", "[fast]")
{
    // A high sine whose peaks fall between samples, and the torture mix.
    const auto high = sine(11025.0 * 0.99, kRate, 48000, 1.0f, 2);
    for (const auto* x : {&high})
        for (float link : {0.0f, 2.0f})
        {
            const auto y = renderWith(*x, {{L::Ceiling, -1.0f}, {L::Gain, 12.0f}, {L::Link, link}});
            INFO("link " << link);
            CHECK(dsp::gainToDb(static_cast<float>(maxTruePeak(y))) <= -1.0f + 0.1f);
        }
    const auto loud = renderWith(torture(48000), {{L::Ceiling, -1.0f}, {L::Gain, 12.0f}});
    CHECK(dsp::gainToDb(static_cast<float>(maxTruePeak(loud))) <= -1.0f + 0.1f);
}

TEST_CASE("Limiter: below the ceiling it passes the input untouched, late by its latency", "[fast]")
{
    const auto x = whiteNoise(24000, 3, 0.25f, 2); // peaks at -12 dBFS
    for (const auto& [lookahead, truePeak] : {std::pair{0.0f, 0.0f}, std::pair{1.0f, 1.0f}, std::pair{2.0f, 1.0f}})
    {
        L l;
        setAll(l, {{L::Lookahead, lookahead}, {L::TruePeak, truePeak}});
        const auto y = render(l, x, {.sampleRate = kRate});
        const int latency = l.latencySamples();
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = latency; i < x.numSamples(); ++i)
                worst = std::max(worst, std::abs(double(y.at(c, i)) - x.at(c, i - latency)));
        INFO("lookahead " << lookahead << ", true peak " << truePeak);
        CHECK(worst < 1e-6); // < -120 dBFS
    }
}

TEST_CASE("Limiter: limiting a low sine by 6 dB distorts it little", "[fast]")
{
    constexpr int size = 32768, cycles = 69; // about 101 Hz
    const double f0 = binCentredFrequency(cycles, size, kRate);
    const auto x = sine(f0, kRate, 48000 + size, dsp::dbToGain(-0.3f + 6.0f), 2);
    for (float autoRelease : {0.0f, 1.0f})
    {
        const auto y = renderWith(x, {{L::Release, 80.0f}, {L::ReleaseAuto, autoRelease}});
        INFO("auto release " << autoRelease);
        CHECK(thdPercent(y, 0, 48000, size, cycles) < 1.0);
    }
}

TEST_CASE("Limiter: latency follows Lookahead and True Peak", "[fast]")
{
    for (int lookahead = 0; lookahead < 3; ++lookahead)
        for (bool truePeak : {false, true})
        {
            L l;
            setAll(l, {{L::Lookahead, static_cast<float>(lookahead)}, {L::TruePeak, truePeak ? 1.0f : 0.0f}});
            l.prepare({.sampleRate = kRate, .maxBlockSize = 512});
            const int expected = std::array{72, 144, 288}[static_cast<size_t>(lookahead)] +
                                 (truePeak ? dsp::TruePeakDetector::latencySamples() : 0);
            CHECK(l.latencySamples() == expected);
            const auto x = whiteNoise(8192, 5, 0.25f, 2);
            CHECK(estimateLatency(x, render(l, x, {.sampleRate = kRate, .skipPrepare = true}), 1024) == expected);
        }
}

TEST_CASE("Limiter: Standard limits harder as the ceiling drops; Maximise only moves the output", "[fast]")
{
    const auto x = sine(997.0, kRate, 48000, 1.0f, 2); // 0 dBFS peaks
    auto reduction = [&](float mode, float ceiling) {
        L l;
        setAll(l, {{L::Mode, mode}, {L::Ceiling, ceiling}, {L::Gain, 6.0f}, {L::TruePeak, 0.0f}});
        const auto y = render(l, x, {.sampleRate = kRate});
        return std::pair{l.analysis().takePeak(L::GainReduction), dsp::gainToDb(peak(y.slice(24000, 24000)))};
    };
    const auto [standardHigh, standardHighPeak] = reduction(0.0f, -0.3f);
    const auto [standardLow, standardLowPeak] = reduction(0.0f, -6.0f);
    CHECK(standardHigh == Approx(6.3f).margin(0.2f));
    CHECK(standardLow == Approx(12.0f).margin(0.2f));
    CHECK(standardLowPeak == Approx(-6.0f).margin(0.05f));
    const auto [maxHigh, maxHighPeak] = reduction(1.0f, -0.3f);
    const auto [maxLow, maxLowPeak] = reduction(1.0f, -6.0f);
    CHECK(maxHigh == Approx(6.0f).margin(0.2f));
    CHECK(maxLow == Approx(maxHigh).margin(0.01f)); // the same limiting
    CHECK(maxHighPeak == Approx(-0.3f).margin(0.05f));
    CHECK(maxLowPeak == Approx(-6.0f).margin(0.05f));
}

TEST_CASE("Limiter: M/S limits mid and side apart, keeping the centre", "[fast]")
{
    // A loud centre and a quieter wide part: only the side gives way when the mid fits.
    const int n = 48000;
    AudioBuffer x(2, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kRate);
        const float mid = 0.7f * std::sin(2.0f * 3.14159265f * 200.0f * t);
        const float side = 0.6f * std::sin(2.0f * 3.14159265f * 3000.0f * t);
        x.at(0, i) = mid + side;
        x.at(1, i) = mid - side;
    }
    const auto y = renderWith(x, {{L::Link, 2.0f}, {L::Ceiling, -1.0f}, {L::TruePeak, 0.0f}});
    CHECK(peak(y) <= dsp::dbToGain(-1.0f) * 1.000001f);
    // The mid comes through at full level; the side is turned down.
    double midIn = 0.0, midOut = 0.0, sideIn = 0.0, sideOut = 0.0;
    const int latency = 144;
    for (int i = 24000; i < n; ++i)
    {
        midIn += std::pow(0.5 * (x.at(0, i - latency) + x.at(1, i - latency)), 2.0);
        midOut += std::pow(0.5 * (y.at(0, i) + y.at(1, i)), 2.0);
        sideIn += std::pow(0.5 * (x.at(0, i - latency) - x.at(1, i - latency)), 2.0);
        sideOut += std::pow(0.5 * (y.at(0, i) - y.at(1, i)), 2.0);
    }
    CHECK(10.0 * std::log10(midOut / midIn) == Approx(0.0).margin(0.1));
    CHECK(10.0 * std::log10(sideOut / sideIn) < -1.0);
}

TEST_CASE("Limiter: loudness readout, its reset, and the meters", "[fast]")
{
    L l;
    // A 997 Hz sine at -23 dBFS in both channels reads -23 LUFS (EBU Tech 3341, case 1).
    const auto x = sine(997.0, kRate, static_cast<int>(10 * kRate), dsp::dbToGain(-23.0f), 2);
    render(l, x, {.sampleRate = kRate});
    CHECK(l.analysis().read(L::IntegratedLufs) == Approx(-23.0f).margin(0.1f));
    CHECK(l.analysis().read(L::ShortTermLufs) == Approx(-23.0f).margin(0.1f));
    CHECK(l.analysis().read(L::TruePeakMax) == Approx(-23.0f).margin(0.1f));
    CHECK(l.analysis().takePeak(L::PeakOutL) == Approx(dsp::dbToGain(-23.0f)).epsilon(0.02));
    CHECK(l.analysis().takePeak(L::GainReduction) == 0.0f);
    auto& fifo = l.analysis().fifo(L::historyFifo);
    CHECK(fifo.available() >= 3 * 90);

    // The readout's reset clears the integrated value; it builds up again.
    l.analysis().request(L::ResetLoudness);
    render(l, x.slice(0, 4800), {.sampleRate = kRate, .skipPrepare = true});
    CHECK(std::isinf(l.analysis().read(L::IntegratedLufs)));
}

TEST_CASE("Limiter: Mode and Channels changes crossfade", "[fast]")
{
    const auto input = stereoSines(1000.0, 1500.0, kRate, 48000, 0.9f);
    struct Case
    {
        L::Param param;
        float from, to;
    };
    for (const auto& k : {Case{L::Mode, 0.0f, 1.0f}, Case{L::Mode, 1.0f, 0.0f}, Case{L::Link, 0.0f, 2.0f},
                          Case{L::Link, 2.0f, 1.0f}})
    {
        L l;
        setAll(l, {{L::Gain, 12.0f}, {L::Ceiling, -6.0f}});
        l.params().set(k.param, k.from);
        RenderOptions o{.sampleRate = kRate};
        o.beforeBlock = [&](Device& d, std::int64_t start) {
            if (start >= 24000)
                d.params().set(k.param, k.to);
        };
        const auto out = render(l, input, o).slice(12000, 36000);
        for (int ch = 0; ch < 2; ++ch)
        {
            INFO("param " << l.info().params[k.param].id << " " << k.from << " -> " << k.to << ", channel " << ch);
            CHECK(clickScore(out, ch) < 3.0);
        }
    }
}

TEST_CASE("Limiter reduction and loudness match golden metrics", "[fast]")
{
    nlohmann::json metrics;
    const auto x = torture(96000);
    for (float gain : {0.0f, 12.0f})
        for (float link : {0.0f, 2.0f})
        {
            const auto y = renderWith(x, {{L::Gain, gain}, {L::Link, link}});
            metrics["rms_db.gain" + std::to_string(int(gain)) + ".link" + std::to_string(int(link))] =
                20.0 * std::log10(rms(y));
        }
    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/limiter/curve.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
