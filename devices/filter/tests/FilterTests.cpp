// Filter (M3 step B6): the automated test targets of devices/filter/SPEC.md.

#include "Filter.h"
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
using F = fd::devices::Filter;
using T = F::TypeKind;
using K = F::CircuitKind;

namespace
{
constexpr double kRate = 48000.0;

using Settings = std::initializer_list<std::pair<F::Param, float>>;

void setAll(Device& d, Settings settings)
{
    for (const auto& [param, value] : settings)
        d.params().set(param, value);
}

AudioBuffer renderWith(const AudioBuffer& in, Settings settings, RenderOptions o = {})
{
    F d;
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

int latency()
{
    F d;
    d.prepare({.sampleRate = kRate, .maxBlockSize = 512});
    return d.latencySamples();
}

/// The filter's impulse response at -40 dBFS (small-signal), whole: the oversampler's filters ring
/// either side of the latency.
struct Measured
{
    AudioBuffer ir;
    [[nodiscard]] double at(double hz) const
    {
        return std::abs(responseAt(ir.channel(0), ir.numSamples(), hz, kRate)) / 0.01;
    }
};
Measured measure(Settings settings, double seconds = 0.5)
{
    return {renderWith(impulse(samples(seconds), 2, 0.01f, samples(0.01)), settings)};
}

F::Settings settingsOf(T type, K circuit, bool steep, double res, double morph = 0.0)
{
    return {.type = type, .circuit = circuit, .steep = steep, .res = res, .morph = morph};
}

/// Plays block by block from a song position (ppq per block) and reads an analysis slot after each.
std::vector<float> play(Device& d, int blocks, int size, const std::function<double(int)>& ppq, int slot,
                        float level = 0.0f)
{
    AudioBuffer buffer(2, size);
    float* channels[] = {buffer.channel(0), buffer.channel(1)};
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        for (int i = 0; i < size; ++i)
            buffer.at(0, i) = buffer.at(1, i) = level;
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

/// The frequency of the strongest ring in `b` from `from` (zero crossings, over `count` samples).
double ringHz(const AudioBuffer& b, int from, int count)
{
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < from + count; ++i)
        if ((b.at(0, i - 1) < 0.0f) != (b.at(0, i) < 0.0f))
        {
            ++crossings;
            if (first < 0)
                first = i;
            last = i;
        }
    return crossings < 3 ? 0.0 : 0.5 * (crossings - 1) * kRate / (last - first);
}
} // namespace

TEST_CASE("Filter: the main control's place, and how far modulation moves it", "[filter][fast]")
{
    CHECK(F::placeOf(T::LowPass, 20.0) == 0.0);
    CHECK(std::abs(F::placeOf(T::LowPass, 20000.0) - 1.0) < 1e-12);
    CHECK(std::abs(F::valueAt(T::LowPass, F::placeOf(T::LowPass, 1234.0)) - 1234.0) < 1e-9);
    CHECK(F::placeOf(T::Dj, 0.0) == 0.5);
    CHECK(F::placeOf(T::Vowel, 25.0) == 0.25);
    // 100 %: half the scale each way, about five octaves of Freq.
    const double place = F::placeOf(T::LowPass, 500.0);
    const double up = F::valueAt(T::LowPass, F::modulated(place, 1.0, 1.0, 0.0, 0.0));
    CHECK(std::abs(std::log2(up / 500.0) - 5.0) < 0.1);
    CHECK(F::modulated(0.9, 1.0, 1.0, 1.0, 1.0) == 1.0);
    CHECK(std::abs(F::modulated(0.5, 0.0, 1.0, -1.0, 0.5) - 0.25) < 1e-12);
}

TEST_CASE("Filter: the response is what the display draws", "[filter][fast]")
{
    for (const auto type : {T::LowPass, T::HighPass, T::BandPass, T::Notch, T::Morph, T::NotchLowPass})
        for (const auto circuit : {K::Clean, K::Warm, K::Acid, K::Gritty})
            for (const bool steep : {false, true})
                for (const double hz : {100.0, 1000.0, 8000.0})
                    for (const double res : {0.0, 0.5, 0.9})
                    {
                        INFO("type " << static_cast<int>(type) << ", circuit " << static_cast<int>(circuit)
                                     << ", steep " << steep << ", " << hz << " Hz, res " << res);
                        const auto m = measure({{F::Type, static_cast<float>(type)},
                                                {F::Circuit, static_cast<float>(circuit)},
                                                {F::Slope, steep ? 1.0f : 0.0f},
                                                {F::Freq, static_cast<float>(hz)},
                                                {F::Res, static_cast<float>(res * 100.0)},
                                                {F::Morph, 30.0f}});
                        const auto s = settingsOf(type, circuit, steep, res, 0.3);
                        const double place = F::placeOf(T::LowPass, hz);
                        const double tolerance = circuit == K::Clean ? 0.2 : 1.0;
                        double worst = 0.0;
                        for (double f = 30.0; f <= 15000.0; f *= 1.35)
                        {
                            const double expected = F::magnitude(s, place, f, kRate);
                            if (db(expected) < -50.0)
                                continue; // far down the slope or in a notch: nothing to compare
                            worst = std::max(worst, std::abs(db(m.at(f) / expected)));
                        }
                        CHECK(worst < tolerance);
                    }
}

TEST_CASE("Filter: 12 and 24 dB per octave, every circuit", "[filter][fast]")
{
    for (const auto circuit : {K::Clean, K::Warm, K::Acid, K::Gritty})
        for (const bool steep : {false, true})
        {
            INFO("circuit " << static_cast<int>(circuit) << ", steep " << steep);
            const auto lp = measure({{F::Circuit, static_cast<float>(circuit)},
                                     {F::Slope, steep ? 1.0f : 0.0f},
                                     {F::Freq, 500.0f},
                                     {F::Res, 0.0f}});
            CHECK(std::abs(db(lp.at(2000.0) / lp.at(4000.0)) - (steep ? 24.0 : 12.0)) < 1.5);
            const auto hp = measure({{F::Type, 1.0f},
                                     {F::Circuit, static_cast<float>(circuit)},
                                     {F::Slope, steep ? 1.0f : 0.0f},
                                     {F::Freq, 4000.0f},
                                     {F::Res, 0.0f}});
            CHECK(std::abs(db(hp.at(1000.0) / hp.at(500.0)) - (steep ? 24.0 : 12.0)) < 1.5);
        }
}

TEST_CASE("Filter: the band pass's peak stays at 0 dB at any Resonance", "[filter][fast]")
{
    for (const bool steep : {false, true})
        for (const float res : {0.0f, 50.0f, 100.0f})
        {
            INFO("steep " << steep << ", res " << res);
            const auto m = measure(
                {{F::Type, 2.0f}, {F::Slope, steep ? 1.0f : 0.0f}, {F::Freq, 1000.0f}, {F::Res, res}}, 1.0);
            double peak = 0.0;
            for (double f = 700.0; f < 1400.0; f *= 1.002)
                peak = std::max(peak, m.at(f));
            CHECK(std::abs(db(peak)) < 0.5);
        }
}

TEST_CASE("Filter: Warm and Acid whistle at Freq at full Resonance, and stop", "[filter][fast]")
{
    for (const auto circuit : {K::Warm, K::Acid})
        for (const bool steep : {false, true})
        {
            INFO("circuit " << static_cast<int>(circuit) << ", steep " << steep);
            auto in = silence(samples(3.0));
            for (int i = 0; i < samples(0.01); ++i)
                in.at(0, i) = in.at(1, i) =
                    0.3f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * i / kRate));
            const auto out = renderWith(in, {{F::Circuit, static_cast<float>(circuit)},
                                             {F::Slope, steep ? 1.0f : 0.0f},
                                             {F::Freq, 1000.0f},
                                             {F::Res, 100.0f}});
            CHECK(std::abs(ringHz(out, samples(0.05), samples(0.1)) / 1000.0 - 1.0) < 0.03);
            const double ring = rmsOver(out, 0, samples(0.03), samples(0.05));
            CHECK(db(rmsOver(out, 0, samples(2.0), samples(2.1)) / ring) < -60.0);
        }
}

TEST_CASE("Filter: the DJ filter is nothing at the centre, and a low or high pass either side",
          "[filter][fast]")
{
    const auto in = pinkNoise(samples(1.0), 3, 0.4f);
    const auto out = renderWith(in, {{F::Type, 5.0f}, {F::Sweep, 0.0f}, {F::Res, 80.0f}});
    const int l = latency();
    AudioBuffer later(2, in.numSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = l; i < in.numSamples(); ++i)
            later.at(c, i) = in.at(c, i - l);
    CHECK(nullResidualDb(later, out) < -100.0);

    const auto lp = measure({{F::Type, 5.0f}, {F::Sweep, -100.0f}, {F::Res, 0.0f}});
    CHECK(lp.at(25.0) > 0.7);
    CHECK(lp.at(400.0) < 0.05);
    const auto hp = measure({{F::Type, 5.0f}, {F::Sweep, 100.0f}, {F::Res, 0.0f}});
    CHECK(hp.at(15000.0) > 0.7);
    CHECK(hp.at(2000.0) < 0.05);
}

TEST_CASE("Filter: the comb's peaks and notches sit at multiples of Freq", "[filter][fast]")
{
    for (const float pitch : {50.0f, 500.0f, 5000.0f})
        for (const float morph : {0.0f, 100.0f})
        {
            INFO(pitch << " Hz, morph " << morph);
            const auto m =
                measure({{F::Type, 6.0f}, {F::Freq, pitch}, {F::Res, 80.0f}, {F::Morph, morph}}, 1.0);
            for (int k = 1; k <= 3; ++k)
            {
                // The peak (or the notch) nearest each multiple.
                double best = morph < 50.0f ? 0.0 : 1e9, at = 0.0;
                for (double f = 0.9 * k * pitch; f < 1.1 * k * pitch; f *= 1.0005)
                {
                    const double v = m.at(f);
                    if (morph < 50.0f ? v > best : v < best)
                    {
                        best = v;
                        at = f;
                    }
                }
                CHECK(std::abs(at / (k * pitch) - 1.0) < 0.01);
            }
        }
}

TEST_CASE("Filter: the vowel's formants are where the table puts them", "[filter][fast]")
{
    for (const float vowel : {0.0f, 25.0f, 50.0f, 75.0f, 100.0f})
    {
        INFO("vowel " << vowel);
        const auto f = F::formants(vowel / 100.0, 0.0);
        const auto m = measure({{F::Type, 7.0f}, {F::Vowel, vowel}, {F::Res, 50.0f}}, 1.0);
        for (size_t n = 0; n < 3; ++n)
        {
            double best = 0.0, at = 0.0;
            for (double hz = 0.85 * f.hz[n]; hz < 1.15 * f.hz[n]; hz *= 1.002)
                if (const double v = m.at(hz); v > best)
                {
                    best = v;
                    at = hz;
                }
            CHECK(std::abs(at / f.hz[n] - 1.0) < 0.05);
        }
    }
}

TEST_CASE("Filter: resampling holds the sound at twice Freq", "[filter][fast]")
{
    const int size = 16384, start = samples(0.2);
    const double tone = binCentredFrequency(341, size, kRate); // about 1 kHz
    const auto out = renderWith(sine(tone, kRate, start + size, 0.5f), {{F::Type, 9.0f}, {F::Freq, 4000.0f}});
    const auto power = powerSpectrum(out, 0, start, size);
    // The strongest image besides the tone: 8 kHz less the tone.
    size_t best = 0;
    for (size_t b = 1; b < power.size(); ++b)
        if (std::abs(static_cast<double>(b) * kRate / size - tone) > 200.0 && power[b] > power[best])
            best = b;
    CHECK(std::abs(static_cast<double>(best) * kRate / size / (8000.0 - tone) - 1.0) < 0.01);
}

TEST_CASE("Filter: the LFO and the envelope move the cutoff up to five octaves", "[filter][fast]")
{
    SECTION("the LFO at 100 %")
    {
        F d;
        // At the scale's middle (632 Hz), so its whole reach fits between 20 Hz and 20 kHz.
        setAll(d, {{F::Freq, 632.5f},
                   {F::Lfo, 1.0f},
                   {F::LfoSync, 0.0f},
                   {F::LfoRate, 2.0f},
                   {F::LfoAmount, 100.0f}});
        d.prepare({.sampleRate = kRate, .maxBlockSize = 64});
        const auto at = play(d, 2000, 64, [](int) { return 0.0; }, F::MainLeft);
        const auto [lo, hi] = std::minmax_element(at.begin() + 200, at.end());
        CHECK(*hi > 0.997f);
        CHECK(*lo < 0.003f);
        // Five octaves either side, within 2 %.
        CHECK(std::abs(std::log2(F::valueAt(T::LowPass, *hi) / 632.5) / 5.0 - 1.0) < 0.02);
    }
    SECTION("the envelope at 100 %, at 0 dBFS")
    {
        F d;
        setAll(d, {{F::Freq, 100.0f}, {F::Env, 1.0f}, {F::EnvAmount, 100.0f}, {F::EnvAttack, 5.0f}});
        d.prepare({.sampleRate = kRate, .maxBlockSize = 64});
        const auto at = play(d, 200, 64, [](int) { return 0.0; }, F::MainLeft, 1.0f);
        CHECK(std::abs(at.back() - (F::placeOf(T::LowPass, 100.0) + 0.5)) < 0.003);
    }
}

TEST_CASE("Filter: the envelope follows Env Attack and Env Release, or the sidechain", "[filter][fast]")
{
    // A burst at 0 dBFS for 0.3 s, then silence: the cutoff rises within the attack, falls within the
    // release.
    F d;
    setAll(d, {{F::Freq, 200.0f},
               {F::Env, 1.0f},
               {F::EnvAmount, 100.0f},
               {F::EnvAttack, 20.0f},
               {F::EnvRelease, 200.0f}});
    d.prepare({.sampleRate = kRate, .maxBlockSize = 48});
    std::vector<float> at;
    AudioBuffer buffer(2, 48);
    float* channels[] = {buffer.channel(0), buffer.channel(1)};
    for (int b = 0; b < 1000; ++b) // 1 s in 1 ms blocks
    {
        for (int i = 0; i < 48; ++i)
            buffer.at(0, i) = buffer.at(1, i) = b < 300 ? (i % 2 == 0 ? 1.0f : -1.0f) : 0.0f;
        ProcessContext ctx;
        ctx.input = {channels, 2};
        ctx.output = {channels, 2};
        ctx.numSamples = 48;
        processRealtime(d, ctx);
        at.push_back(d.analysis().read(F::MainLeft));
    }
    const double base = F::placeOf(T::LowPass, 200.0);
    CHECK(at[25] > base + 0.49);        // risen by 25 ms
    CHECK(at[300 + 230] < base + 0.01); // fallen within 230 ms of the end

    SECTION("a sidechain moves it, not the input")
    {
        const auto quiet = sine(500.0, kRate, samples(1.0), 0.01f);
        auto key = silence(samples(1.0));
        for (int i = samples(0.5); i < samples(1.0); ++i)
            key.at(0, i) = key.at(1, i) = 1.0f;
        F e;
        setAll(e, {{F::Freq, 200.0f}, {F::Env, 1.0f}, {F::EnvAmount, 100.0f}});
        RenderOptions o;
        o.sampleRate = kRate;
        o.sidechain = &key;
        (void)render(e, quiet, o);
        CHECK(e.analysis().read(F::MainLeft) > base + 0.49);
        F f;
        setAll(f, {{F::Freq, 200.0f}, {F::Env, 1.0f}, {F::EnvAmount, 100.0f}});
        (void)render(f, quiet, RenderOptions{.sampleRate = kRate});
        CHECK(f.analysis().read(F::MainLeft) < base + 0.2); // the input alone is quiet
    }
}

TEST_CASE("Filter: synced, locked to the song; Phase and Spin", "[filter][fast]")
{
    const double perBlock = 512.0 / kRate * 2.0;
    const Settings synced{{F::Lfo, 1.0f}, {F::LfoSync, 1.0f}, {F::LfoDiv, 8.0f}, {F::LfoAmount, 100.0f}};
    SECTION("after a jump, the song's place")
    {
        F a, b;
        setAll(a, synced);
        setAll(b, synced);
        a.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        b.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        const auto through = play(a, 80, 512, [&](int k) { return k * perBlock; }, F::MainLeft);
        const auto jumped =
            play(b, 80, 512, [&](int k) { return k < 40 ? 3.7 + k * perBlock : k * perBlock; }, F::MainLeft);
        CHECK(std::abs(through.back() - jumped.back()) < 1e-3);
    }
    SECTION("Phase 180°: half a cycle behind")
    {
        F a, b;
        setAll(a, synced);
        setAll(b, synced);
        b.params().set(F::LfoPhase, 180.0f);
        a.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        b.prepare({.sampleRate = kRate, .maxBlockSize = 512});
        const auto x = play(a, 40, 512, [&](int k) { return k * perBlock + 0.1; }, F::MainLeft);
        const auto y = play(b, 40, 512, [&](int k) { return k * perBlock + 0.1; }, F::MainLeft);
        const double place = F::placeOf(T::LowPass, 2000.0);
        CHECK(std::abs((x.back() - place) + (y.back() - place)) < 2e-3); // a sine, mirrored
    }
    SECTION("Spin 100 %: twice as many cycles on the right")
    {
        const auto count = [](const std::vector<float>& v, double centre)
        {
            int n = 0;
            for (size_t i = 1; i < v.size(); ++i)
                n += (v[i - 1] < centre) != (v[i] < centre);
            return n;
        };
        const Settings spin{{F::Lfo, 1.0f},       {F::LfoSync, 0.0f},   {F::LfoRate, 2.0f},
                            {F::LfoStereo, 1.0f}, {F::LfoSpin, 100.0f}, {F::LfoAmount, 50.0f}};
        F a, b;
        setAll(a, spin);
        setAll(b, spin);
        a.prepare({.sampleRate = kRate, .maxBlockSize = 64});
        b.prepare({.sampleRate = kRate, .maxBlockSize = 64});
        const auto none = [](int) { return 0.0; };
        const double place = F::placeOf(T::LowPass, 2000.0);
        CHECK(std::abs(count(play(a, 3000, 64, none, F::MainLeft), place) - 16) <= 1); // 4 s: 8 cycles
        CHECK(std::abs(count(play(b, 3000, 64, none, F::MainRight), place) - 32) <= 1);
    }
}

TEST_CASE("Filter: at Offset 0, a mono input stays mono", "[filter][fast]")
{
    const auto in = mono(pinkNoise(samples(0.5), 5, 0.4f));
    for (int type = 0; type < F::numTypes; ++type)
    {
        INFO("type " << type);
        const auto out =
            renderWith(in, {{F::Type, static_cast<float>(type)}, {F::Lfo, 1.0f}, {F::Env, 1.0f}});
        bool same = true;
        for (int i = 0; i < out.numSamples(); ++i)
            same = same && out.at(0, i) == out.at(1, i);
        CHECK(same);
    }
}

TEST_CASE("Filter: Drive changes the colour, not the level of quiet sound", "[filter][fast]")
{
    const auto in = sine(440.0, kRate, samples(1.0), dsp::dbToGain(-30.0f));
    const auto plain = renderWith(in, {{F::Freq, 8000.0f}, {F::Res, 0.0f}});
    const auto driven = renderWith(in, {{F::Freq, 8000.0f}, {F::Res, 0.0f}, {F::Drive, 24.0f}});
    CHECK(std::abs(db(rmsOver(driven, 0, samples(0.2), samples(1.0)) /
                      rmsOver(plain, 0, samples(0.2), samples(1.0)))) < 0.5);
}

TEST_CASE("Filter: driven hard, Warm stays clear of aliasing", "[filter][fast]")
{
    const int size = 16384, start = samples(0.3);
    const double tone = binCentredFrequency(853, size, kRate); // about 2.5 kHz
    const auto out = renderWith(sine(tone, kRate, start + size, 0.5f),
                                {{F::Circuit, 1.0f}, {F::Drive, 12.0f}, {F::Res, 50.0f}, {F::Freq, 5000.0f}});
    CHECK(aliasingRatioDb(out, 0, start, size, 853, kRate) < -60.0);
}

TEST_CASE("Filter: Clip keeps resonance under 0 dBFS; quiet sound passes exactly", "[filter][fast]")
{
    const auto quiet = pinkNoise(samples(0.5), 7, 0.1f);
    const auto on = renderWith(quiet, {{F::Res, 60.0f}, {F::Freq, 4000.0f}});
    const auto off = renderWith(quiet, {{F::Res, 60.0f}, {F::Freq, 4000.0f}, {F::Clip, 0.0f}});
    REQUIRE(peakDb(on) < -6.0);
    CHECK(nullResidualDb(on, off) < -120.0);

    const auto loud = sine(1000.0, kRate, samples(0.5), 1.0f);
    for (int type = 0; type < F::numTypes; ++type)
        for (int circuit = 0; circuit < 4; ++circuit)
        {
            INFO("type " << type << ", circuit " << circuit);
            const Settings base{{F::Type, static_cast<float>(type)},
                                {F::Circuit, static_cast<float>(circuit)},
                                {F::Freq, 1000.0f},
                                {F::Sweep, -40.0f},
                                {F::Res, 100.0f},
                                {F::Drive, 12.0f}};
            const auto clipped = renderWith(loud, base);
            REQUIRE(allFinite(clipped));
            CHECK(peakDb(clipped) <= 0.0);
            F d;
            setAll(d, base);
            d.params().set(F::Clip, 0.0f);
            const auto open = render(d, loud, RenderOptions{.sampleRate = kRate});
            CHECK(peakDb(open) <= 12.0);
        }
}

TEST_CASE("Filter: at 0 % Dry/Wet the output is the input, held back by the latency", "[filter][fast]")
{
    const auto in = pinkNoise(samples(1.0), 3, 0.5f);
    const auto out = renderWith(in, {{F::Mix, 0.0f}, {F::Res, 90.0f}, {F::Drive, 12.0f}, {F::Lfo, 1.0f}});
    const int l = latency();
    REQUIRE(l > 0);
    AudioBuffer later(2, in.numSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = l; i < in.numSamples(); ++i)
            later.at(c, i) = in.at(c, i - l);
    CHECK(nullResidualDb(later, out) < -120.0);
}

TEST_CASE("Filter: the latency is what it reports", "[filter][fast]")
{
    const auto in = whiteNoise(samples(0.5), 11, 0.3f);
    const auto out = renderWith(in, {{F::Type, 1.0f}, {F::Slope, 0.0f}, {F::Freq, 20.0f}, {F::Res, 0.0f}});
    CHECK(estimateLatency(in, out, 400) == latency());
}

TEST_CASE("Filter: changing the type, circuit, slope, shape or Sync doesn't click", "[filter][fast]")
{
    const auto in = sine(300.0, kRate, samples(1.5), 0.5f);
    struct Change
    {
        const char* name;
        F::Param param;
        float from, to;
    };
    for (const auto& change :
         {Change{"low to high pass", F::Type, 0.0f, 1.0f}, Change{"band pass to vowel", F::Type, 2.0f, 7.0f},
          Change{"comb to DJ", F::Type, 6.0f, 5.0f}, Change{"Clean to Warm", F::Circuit, 0.0f, 1.0f},
          Change{"Acid to Gritty", F::Circuit, 2.0f, 3.0f}, Change{"slope", F::Slope, 1.0f, 0.0f},
          Change{"shape", F::LfoShape, 0.0f, 4.0f}, Change{"Sync off", F::LfoSync, 1.0f, 0.0f}})
    {
        INFO(change.name);
        RenderOptions o;
        o.beforeBlock = [&change](Device& d, std::int64_t start)
        {
            if (start >= samples(0.75))
                d.params().set(change.param, change.to);
        };
        const auto out = renderWith(in,
                                    {{change.param, change.from},
                                     {F::Freq, 800.0f},
                                     {F::Sweep, -30.0f},
                                     {F::Lfo, 1.0f},
                                     {F::LfoAmount, 30.0f},
                                     {F::Res, 40.0f}},
                                    o);
        for (int c = 0; c < 2; ++c)
            CHECK(clickScore(out.slice(samples(0.5), samples(1.0)), c) < 8.0);
    }
}

TEST_CASE("Filter: its character matches golden metrics", "[filter][fast]")
{
    nlohmann::json metrics;
    // Each circuit's colour: a 110 Hz saw-like tone at -6 dBFS through a resonant, driven low pass;
    // how much of it is harmonics the input didn't have (the sound's grit), and its level.
    const int size = 16384, start = samples(0.3);
    const double tone = binCentredFrequency(37, size, kRate);
    auto saw = silence(start + size);
    for (int i = 0; i < saw.numSamples(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 40; ++h)
            v += std::sin(2.0 * std::numbers::pi * h * tone * i / kRate) / h;
        saw.at(0, i) = saw.at(1, i) = static_cast<float>(0.3 * v);
    }
    constexpr const char* kNames[] = {"clean", "warm", "acid", "gritty"};
    for (int circuit = 0; circuit < 4; ++circuit)
    {
        const auto out = renderWith(saw, {{F::Circuit, static_cast<float>(circuit)},
                                          {F::Freq, 800.0f},
                                          {F::Res, 70.0f},
                                          {F::Drive, 6.0f}});
        metrics[std::string(kNames[circuit]) + ".level_db"] =
            db(rmsOver(out, 0, start, start + size) / rmsOver(saw, 0, start, start + size));
        // How it breaks up: a 200 Hz tone at -6 dBFS, driven, under a resonant cutoff well above
        // its harmonics; Acid's leaning saturation shows as the second harmonic.
        const int cycles = 69;
        const auto driven =
            renderWith(sine(binCentredFrequency(cycles, size, kRate), kRate, start + size, 0.5f),
                       {{F::Circuit, static_cast<float>(circuit)},
                        {F::Freq, 6000.0f},
                        {F::Res, 70.0f},
                        {F::Drive, 6.0f}});
        metrics[std::string(kNames[circuit]) + ".thd_percent"] = thdPercent(driven, 0, start, size, cycles);
        const auto h = harmonicLevelsDb(driven, 0, start, size, cycles, 2);
        metrics[std::string(kNames[circuit]) + ".second_harmonic_db"] = std::max(-100.0, h[1] - h[0]);
    }
    // The defaults, the vowel and the comb on pink noise: their level.
    const auto pink = pinkNoise(samples(3.0), 11, 0.3f);
    const auto level = [&](Settings s)
    {
        const auto out = renderWith(pink, s);
        return db(rmsOver(out, 0, samples(0.5), samples(3.0)) / rmsOver(pink, 0, samples(0.5), samples(3.0)));
    };
    metrics["default.pink_level_db"] = level({});
    metrics["vowel.pink_level_db"] = level({{F::Type, 7.0f}});
    metrics["comb.pink_level_db"] = level({{F::Type, 6.0f}, {F::Freq, 220.0f}, {F::Res, 80.0f}});

    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/filter/character.json", metrics, 0.05);
    INFO(result.report);
    CHECK(result.passed);
}
