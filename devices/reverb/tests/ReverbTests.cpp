// Reverb (M3 step B5): the automated test targets of devices/reverb/SPEC.md.

#include "Reverb.h"
#include "fd/dsp/Filters.h"
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
using R = fd::devices::Reverb;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<R::Param, float>>;

/// The reverb alone: all wet, no predelay, no filter going in, still (no Motion).
constexpr Settings kWet{{R::Mix, 100.0f}, {R::Predelay, 0.0f}, {R::Filter, 0.0f}, {R::Motion, 0.0f}};

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings base, Settings settings, RenderOptions o = {})
{
    R d;
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
    return std::sqrt(sum / std::max(1, to - from));
}

/// How long channel `c` of an impulse response takes to fall 60 dB in the octave around `hz`:
/// the band's Schroeder decay curve, its slope from -5 to -25 dB (T20) times three.
double decayOf(const AudioBuffer& ir, int c, double hz)
{
    dsp::Biquad a, b;
    const auto band = dsp::designBiquad(dsp::BiquadType::Bandpass, kRate, hz, std::numbers::sqrt2);
    a.setCoeffs(band);
    b.setCoeffs(band);
    std::vector<double> energy(static_cast<size_t>(ir.numSamples()));
    for (int i = 0; i < ir.numSamples(); ++i)
    {
        const double y = b.process(a.process(ir.at(c, i)));
        energy[static_cast<size_t>(i)] = y * y;
    }
    // Backward integration.
    for (int i = ir.numSamples() - 2; i >= 0; --i)
        energy[static_cast<size_t>(i)] += energy[static_cast<size_t>(i) + 1];
    const double total = energy[0];
    double t5 = -1.0, t25 = -1.0;
    for (int i = 0; i < ir.numSamples(); ++i)
    {
        const double level = 10.0 * std::log10(energy[static_cast<size_t>(i)] / total);
        if (t5 < 0.0 && level <= -5.0)
            t5 = i / kRate;
        if (t25 < 0.0 && level <= -25.0)
        {
            t25 = i / kRate;
            break;
        }
    }
    return t5 < 0.0 || t25 < 0.0 ? 0.0 : 3.0 * (t25 - t5);
}

/// The normalised echo density of channel `c` over [from, from + window): the share of samples
/// beyond one standard deviation, against a Gaussian's (1 for a dense, noise-like tail).
double echoDensity(const AudioBuffer& b, int c, int from, int window)
{
    double sum = 0.0;
    for (int i = from; i < from + window; ++i)
        sum += static_cast<double>(b.at(c, i)) * b.at(c, i);
    const double sd = std::sqrt(sum / window);
    int beyond = 0;
    for (int i = from; i < from + window; ++i)
        beyond += std::abs(b.at(c, i)) > sd;
    return beyond / static_cast<double>(window) / std::erfc(1.0 / std::numbers::sqrt2);
}

/// The RMS level of channel `c` over [from, to) in the octave around `hz`.
double bandLevel(const AudioBuffer& b, int c, double hz, int from, int to)
{
    dsp::Biquad x, y;
    const auto band = dsp::designBiquad(dsp::BiquadType::Bandpass, kRate, hz, std::numbers::sqrt2);
    x.setCoeffs(band);
    y.setCoeffs(band);
    double sum = 0.0;
    for (int i = 0; i < to; ++i)
    {
        const double v = y.process(x.process(b.at(c, i)));
        if (i >= from)
            sum += v * v;
    }
    return std::sqrt(sum / (to - from));
}

R::Bands bandsOf(float decay, float lowMult, float highMult)
{
    return {.decay = decay,
            .lowMult = lowMult * 0.01,
            .lowHz = 250.0,
            .highMult = highMult * 0.01,
            .highHz = 4000.0};
}
} // namespace

TEST_CASE("Reverb: the decay by band, as decayAt says", "[reverb][fast]")
{
    struct Case
    {
        R::ModeKind mode;
        float decay, low, high;
    };
    const Case cases[] = {
        {R::ModeKind::Room, 1.0f, 120.0f, 60.0f},  {R::ModeKind::Hall, 2.5f, 120.0f, 60.0f},
        {R::ModeKind::Plate, 2.5f, 120.0f, 60.0f}, {R::ModeKind::Space, 6.0f, 120.0f, 60.0f},
        {R::ModeKind::Hall, 2.5f, 50.0f, 150.0f},  {R::ModeKind::Hall, 2.5f, 200.0f, 40.0f},
    };
    for (const auto& k : cases)
    {
        INFO("mode " << static_cast<int>(k.mode) << ", decay " << k.decay << ", low " << k.low << ", high "
                     << k.high);
        const double longest = k.decay * std::max({100.0f, k.low, k.high}) * 0.01;
        const auto ir = renderWith(impulse(samples(std::min(60.0, longest * 0.6 + 0.3))), kWet,
                                   {{R::Mode, static_cast<float>(k.mode)},
                                    {R::Decay, k.decay},
                                    {R::LowMult, k.low},
                                    {R::HighMult, k.high},
                                    {R::Early, 0.0f}});
        const auto bands = bandsOf(k.decay, k.low, k.high);
        const double loop = R::meanLine(k.mode, 0.5) + 0.0007;
        for (const double hz : {125.0, 1000.0, 8000.0})
        {
            const double expected = R::decayAt(bands, hz, loop, kRate);
            const double measured = decayOf(ir, 0, hz);
            INFO(hz << " Hz: measured " << measured << " s, expected " << expected << " s");
            CHECK(std::abs(measured / expected - 1.0) < 0.1);
        }
    }
}

TEST_CASE("Reverb: Decay is the setting at any Size", "[reverb][fast]")
{
    for (const float size : {0.0f, 50.0f, 100.0f})
    {
        INFO("size " << size);
        const auto ir =
            renderWith(impulse(samples(2.5)), kWet, {{R::Size, size}, {R::Decay, 2.0f}, {R::Early, 0.0f}});
        const double measured = decayOf(ir, 0, 1000.0);
        INFO("measured " << measured);
        CHECK(std::abs(measured / R::decayAt(bandsOf(2.0f, 120.0f, 60.0f), 1000.0,
                                             R::meanLine(R::ModeKind::Hall, size * 0.01), kRate) -
                       1.0) < 0.1);
    }
}

TEST_CASE("Reverb: nothing before Predelay; the first reflection at it", "[reverb][fast]")
{
    for (const float mode : {0.0f, 1.0f, 3.0f})
    {
        INFO("mode " << mode);
        const auto ir = renderWith(impulse(samples(0.3)), {{R::Mix, 100.0f}, {R::Filter, 0.0f}},
                                   {{R::Mode, mode}, {R::Predelay, 50.0f}, {R::Early, 100.0f}});
        const int at = samples(0.05);
        double before = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < at; ++i)
                before = std::max(before, static_cast<double>(std::abs(ir.at(c, i))));
        CHECK(db(std::max(before, 1e-12)) < -120.0);
        CHECK(std::abs(ir.at(0, at)) > 0.05f);
    }
}

TEST_CASE("Reverb: the tail grows dense", "[reverb][fast]")
{
    struct Case
    {
        R::ModeKind mode;
        double build; // seconds
    };
    for (const auto& k : {Case{R::ModeKind::Room, 0.03}, Case{R::ModeKind::Plate, 0.01},
                          Case{R::ModeKind::Hall, 0.08}, Case{R::ModeKind::Space, 0.2}})
    {
        INFO("mode " << static_cast<int>(k.mode));
        const auto ir = renderWith(impulse(samples(0.6)), kWet, {{R::Mode, static_cast<float>(k.mode)}});
        for (double t = k.build; t < 0.4; t += 0.04)
        {
            INFO("at " << t << " s: " << echoDensity(ir, 0, samples(t), samples(0.02)));
            CHECK(echoDensity(ir, 0, samples(t), samples(0.02)) >= 0.9);
        }
    }
}

TEST_CASE("Reverb: Freeze holds the tail and lets nothing new in", "[reverb][fast]")
{
    // A burst, frozen 300 ms later, then a louder one that must not get in.
    const int total = samples(31.0);
    AudioBuffer in(2, total);
    const auto first = whiteNoise(samples(0.1), 7, 0.25f);
    const auto second = whiteNoise(samples(0.1), 8, 0.5f);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < first.numSamples(); ++i)
        {
            in.at(c, i) = first.at(c, i);
            in.at(c, samples(5.0) + i) = second.at(c, i);
        }
    RenderOptions o;
    o.beforeBlock = [](Device& d, std::int64_t start)
    {
        if (start >= samples(0.3))
            d.params().set(R::Freeze, 1.0f);
    };
    const auto out = renderWith(in, {{R::Mix, 100.0f}}, {{R::Decay, 3.0f}}, o);
    const double held = rmsOver(out, 0, samples(1.0), samples(2.0));
    REQUIRE(held > 0.005);
    for (double t = 2.0; t + 1.0 <= 31.0; t += 1.0)
    {
        INFO("at " << t << " s");
        CHECK(std::abs(db(rmsOver(out, 0, samples(t), samples(t + 1.0)) / held)) < 0.5);
    }
}

TEST_CASE("Reverb: wide, or mono at Width 0", "[reverb][fast]")
{
    const auto in = pinkNoise(samples(0.2), 3, 0.5f);
    AudioBuffer mono(2, samples(2.0));
    for (int i = 0; i < in.numSamples(); ++i)
        mono.at(0, i) = mono.at(1, i) = in.at(0, i);
    const auto wide = renderWith(mono, kWet, {});
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = samples(0.3); i < wide.numSamples(); ++i)
    {
        lr += static_cast<double>(wide.at(0, i)) * wide.at(1, i);
        ll += static_cast<double>(wide.at(0, i)) * wide.at(0, i);
        rr += static_cast<double>(wide.at(1, i)) * wide.at(1, i);
    }
    CHECK(std::abs(lr / std::sqrt(ll * rr)) < 0.3);

    const auto narrow = renderWith(mono, kWet, {{R::Width, 0.0f}});
    double side = 0.0;
    for (int i = 0; i < narrow.numSamples(); ++i)
        side = std::max(side, static_cast<double>(std::abs(narrow.at(0, i) - narrow.at(1, i))));
    CHECK(db(std::max(side, 1e-12)) < -120.0);
}

TEST_CASE("Reverb: Ducking dips the reverb while the input plays", "[reverb][fast]")
{
    auto in = sine(1000.0, kRate, samples(4.0), 0.31623f);
    for (int c = 0; c < 2; ++c)
        for (int i = samples(2.0); i < in.numSamples(); ++i)
            in.at(c, i) = 0.0f;
    const Settings settings{{R::Mix, 100.0f}, {R::Decay, 3.0f}, {R::DuckRelease, 300.0f}};
    const auto ducked = renderWith(in, settings, {{R::Duck, 100.0f}});
    const auto open = renderWith(in, settings, {});
    CHECK(db(rmsOver(open, 0, samples(1.0), samples(1.5)) / rmsOver(ducked, 0, samples(1.0), samples(1.5))) >=
          25.0);
    CHECK(std::abs(db(rmsOver(open, 0, samples(2.31), samples(2.36)) /
                      rmsOver(ducked, 0, samples(2.31), samples(2.36)))) < 0.5);
}

TEST_CASE("Reverb: a loud burst stays bounded", "[reverb][fast]")
{
    const auto burst = whiteNoise(samples(0.1), 9, 1.0f);
    AudioBuffer in(2, samples(3.0));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < burst.numSamples(); ++i)
            in.at(c, i) = burst.at(c, i);
    for (const float mode : {0.0f, 1.0f, 2.0f, 3.0f})
        for (const float size : {0.0f, 100.0f})
            for (const float decay : {0.1f, 60.0f})
            {
                INFO("mode " << mode << ", size " << size << ", decay " << decay);
                const auto out =
                    renderWith(in, {{R::Mix, 100.0f}}, {{R::Mode, mode}, {R::Size, size}, {R::Decay, decay}});
                REQUIRE(allFinite(out));
                CHECK(peakDb(out) <= 6.0);
            }
}

TEST_CASE("Reverb: at 0 % Dry/Wet the output is the input", "[reverb][fast]")
{
    const auto in = pinkNoise(samples(2.0), 3, 0.5f);
    const auto out = renderWith(in, {}, {{R::Mix, 0.0f}, {R::Decay, 10.0f}, {R::Duck, 50.0f}});
    CHECK(nullResidualDb(in, out) < -120.0);
}

TEST_CASE("Reverb: changing Size, Predelay or Mode doesn't click", "[reverb][fast]")
{
    const auto in = sine(1000.0, kRate, samples(2.0), 0.5f);
    struct Change
    {
        const char* name;
        R::Param param;
        float from, to;
    };
    for (const auto& change :
         {Change{"Size", R::Size, 0.0f, 100.0f}, Change{"Predelay", R::Predelay, 0.0f, 300.0f},
          Change{"Mode", R::Mode, 1.0f, 2.0f}})
    {
        INFO(change.name);
        RenderOptions o;
        o.beforeBlock = [&change](Device& d, std::int64_t start)
        {
            if (start >= samples(1.0))
                d.params().set(change.param, change.to);
        };
        const auto out = renderWith(in, {{R::Mix, 50.0f}}, {{change.param, change.from}}, o);
        for (int c = 0; c < 2; ++c)
            CHECK(clickScore(out.slice(samples(0.5), samples(1.0)), c) < 8.0);
    }
}

TEST_CASE("Reverb: the tail is the longest band's decay and the predelay", "[reverb][fast]")
{
    R d;
    d.params().set(R::Decay, 2.0f);
    d.params().set(R::LowMult, 200.0f);
    d.params().set(R::Predelay, 100.0f);
    CHECK(d.tailSeconds() > 4.1);
    CHECK(d.tailSeconds() < 4.4);
    d.params().set(R::Freeze, 1.0f);
    CHECK(d.tailSeconds() == 60.0);
}

TEST_CASE("Reverb: its character matches golden metrics", "[reverb][fast]")
{
    nlohmann::json metrics;
    constexpr const char* kNames[] = {"room", "hall", "plate", "space"};
    const auto pink = pinkNoise(samples(0.5), 11, 0.3f);
    AudioBuffer burst(2, samples(4.0));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < pink.numSamples(); ++i)
            burst.at(c, i) = pink.at(c, i);
    for (int mode = 0; mode < 4; ++mode)
    {
        const std::string name = kNames[mode];
        // Each mode's defaults: how loud its reverb is against the input, the early part's
        // share, and how bright the tail is a second in.
        const auto out = renderWith(burst, {{R::Mix, 100.0f}}, {{R::Mode, static_cast<float>(mode)}});
        metrics[name + ".level_db"] =
            db(rmsOver(out, 0, 0, samples(1.0)) / rmsOver(burst, 0, 0, samples(0.5)));
        const auto ir =
            renderWith(impulse(samples(1.5)), kWet, {{R::Mode, static_cast<float>(mode)}, {R::Early, 50.0f}});
        metrics[name + ".early_share_db"] =
            db(rmsOver(ir, 0, 0, samples(0.05)) / rmsOver(ir, 0, samples(0.05), samples(1.0)));
        metrics[name + ".tail_8k_vs_1k_db"] = db(bandLevel(ir, 0, 8000.0, samples(0.5), samples(1.0)) /
                                                 bandLevel(ir, 0, 1000.0, samples(0.5), samples(1.0)));
    }

    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/reverb/character.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
