// Granular's engine (devices/granular/SPEC.md, "DSP design", "How the modes start grains", "Tests"),
// JUCE-free.
#include "Granular.h"
#include "fd/device/Realtime.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <functional>
#include <nlohmann/json.hpp>
#include <numbers>

using namespace fd;
using namespace fd::devices;
using namespace fd::testkit;
using G = Granular;

namespace
{
constexpr double sr = 48000.0;

/// A sine of `hz` (half scale) as a source rooted on `root`, unanalysed.
std::shared_ptr<const GrainSource> sineSource(double hz, int root, double seconds = 4.0, int channels = 1,
                                              bool compact = false)
{
    audiofile::DecodedAudio a;
    a.sampleRate = sr;
    a.rootKey = root;
    const auto n = static_cast<size_t>(seconds * sr);
    a.channels.assign(static_cast<size_t>(channels), std::vector<float>(n));
    for (auto& ch : a.channels)
        for (size_t i = 0; i < n; ++i)
            ch[i] = 0.5f *
                    static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    return GrainSource::build(a, "Sine", {}, {.compact = compact, .analyse = false});
}

std::unique_ptr<G> granularWith(std::shared_ptr<const GrainSource> source)
{
    auto g = std::make_unique<G>();
    g->setSourceData(0, std::move(source));
    // Still: no spray of pan or position unless a test asks.
    g->params().set(G::PanSpray, 0.0f);
    g->params().set(G::PosSpray, 0.0f);
    return g;
}

TimedEvent noteOn(std::int64_t at, int key, float velocity = 0.8f)
{
    return {at, {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = velocity}};
}
TimedEvent noteOff(std::int64_t at, int key)
{
    return {at, {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}};
}

RenderOptions with(std::vector<TimedEvent> events, std::vector<int> blocks = {512})
{
    RenderOptions o;
    o.sampleRate = sr;
    o.blockPattern = std::move(blocks);
    o.events = std::move(events);
    return o;
}

/// Renders with the song stopped (testkit::render plays it).
AudioBuffer renderStopped(Device& d, int samples, const std::vector<TimedEvent>& events, int block = 256)
{
    d.prepare({.sampleRate = sr, .maxBlockSize = block});
    AudioBuffer out(2, samples);
    EventList list;
    list.reserve(256);
    size_t next = 0;
    for (int pos = 0; pos < samples; pos += block)
    {
        const int n = std::min(block, samples - pos);
        list.clear();
        while (next < events.size() && events[next].samplePosition < pos + n)
        {
            auto e = events[next++].event;
            e.sampleOffset =
                static_cast<int>(std::max<std::int64_t>(0, events[next - 1].samplePosition - pos));
            list.push(e);
        }
        std::array<float*, 2> outs{out.channel(0) + pos, out.channel(1) + pos};
        ProcessContext ctx;
        ctx.output = {outs.data(), 2};
        ctx.events = &list;
        ctx.numSamples = n;
        ctx.transport.playing = false;
        ctx.transport.samplePosition = pos;
        processRealtime(d, ctx);
    }
    return out;
}

AudioBuffer slice(const AudioBuffer& b, int from, int length)
{
    AudioBuffer s(b.numChannels(), length);
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = 0; i < length; ++i)
            s.at(c, i) = b.at(c, from + i);
    return s;
}

/// The fundamental (Hz) of the left channel over [from, from + length), by autocorrelation.
double fundamental(const AudioBuffer& b, int from, int length)
{
    // The first strong peak of the autocorrelation once it has dipped below zero.
    const float* x = b.channel(0) + from;
    const auto r = [&](int l)
    {
        double sum = 0.0, e0 = 0.0, e1 = 0.0;
        for (int i = 0; i + l < length; ++i)
        {
            sum += static_cast<double>(x[i]) * x[i + l];
            e0 += static_cast<double>(x[i]) * x[i];
            e1 += static_cast<double>(x[i + l]) * x[i + l];
        }
        return sum / std::sqrt(std::max(1e-12, e0 * e1));
    };
    int l = 1;
    while (l < length / 2 && r(l) > 0.0)
        ++l;
    double best = -1.0;
    int lag = 0;
    for (; l < length / 2; ++l)
    {
        const double here = r(l);
        if (here > best)
        {
            best = here;
            lag = l;
        }
        else if (best > 0.5 && here < best - 0.3)
            break;
    }
    // Between samples: the parabola through the peak and its neighbours.
    const double a = r(lag - 1), c = r(lag + 1), d = a - 2.0 * best + c;
    const double offset = d < 0.0 ? 0.5 * (a - c) / d : 0.0;
    return lag > 0 ? sr / (lag + offset) : 0.0;
}

/// The share (dB) of the left channel's power over [from, from + 16384) outside [lo, hi] Hz,
/// through a 4-term Blackman-Harris window (its leakage under -92 dB).
double outsideBandDb(const AudioBuffer& b, int from, double lo, double hi)
{
    constexpr int size = 16384;
    AudioBuffer windowed(1, size);
    for (int i = 0; i < size; ++i)
    {
        const double t = 2.0 * std::numbers::pi * i / (size - 1);
        const double w =
            0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
        windowed.at(0, i) = static_cast<float>(w * b.at(0, from + i));
    }
    const auto power = powerSpectrum(windowed, 0, 0, size);
    double inside = 0.0, outside = 0.0;
    for (size_t k = 1; k < power.size(); ++k)
    {
        const double hz = static_cast<double>(k) * sr / size;
        (hz >= lo && hz <= hi ? inside : outside) += power[k];
    }
    return 10.0 * std::log10(std::max(1e-30, outside) / std::max(1e-30, inside + outside));
}
} // namespace

TEST_CASE("granular: a note plays its source at the note's pitch", "[fast]")
{
    // A 220 Hz sine rooted on A3 (57): each note plays it at its own pitch.
    for (const int key : {45, 57, 69, 76})
    {
        auto g = granularWith(sineSource(220.0, 57));
        const auto out = render(*g, silence(48000), with({noteOn(0, key)}));
        const double hz = 220.0 * std::exp2((key - 57) / 12.0);
        INFO("key " << key);
        CHECK(allFinite(out));
        CHECK(rmsDb(slice(out, 12000, 24000)) > -30.0);
        CHECK(std::abs(fundamental(out, 12000, 8192) - hz) < hz * 0.01);
    }
}

TEST_CASE("granular: Key Track, Transpose and a source's Tune move the pitch", "[fast]")
{
    auto g = granularWith(sineSource(220.0, 57));
    g->params().set(G::PitchKey, 0.0f); // every note at the source's pitch
    g->params().set(G::PitchTranspose, 7.0f);
    g->params().set(G::source(0, G::SrcTune), 5.0f);
    const auto out = render(*g, silence(48000), with({noteOn(0, 81)}));
    CHECK(std::abs(fundamental(out, 12000, 4096) - 440.0) < 4.4);
}

TEST_CASE("granular: every mode plays", "[fast]")
{
    for (const auto mode :
         {G::Mode::Classic, G::Mode::Loop, G::Mode::Cloud, G::Mode::Pitched, G::Mode::Rhythm})
    {
        auto g = std::make_unique<G>();
        g->setSourceData(0, GrainSource::build(GrainSource::chord(4.0, sr), "Chord", {}));
        g->params().set(G::GrainMode, static_cast<float>(mode));
        INFO("mode " << static_cast<int>(mode));
        const auto playing = render(*g, silence(48000), with({noteOn(0, 57), noteOn(0, 64)}));
        CHECK(allFinite(playing));
        CHECK(rmsDb(slice(playing, 12000, 36000)) > -40.0);
        const auto stopped = renderStopped(*g, 48000, {noteOn(0, 57), noteOn(0, 64)});
        CHECK(allFinite(stopped));
        CHECK(rmsDb(slice(stopped, 12000, 36000)) > -40.0);
    }
}

TEST_CASE("granular: Pitched plays the note from any source, Formant only its character", "[fast]")
{
    // (Not a formant an octave under the note on a pure sine: four periods of the note are then two
    // whole cycles of the sine, whose Hann-windowed spectrum is zero at every harmonic of the note.)
    for (const float formant : {0.0f, 7.0f, -4.0f})
        for (const int key : {57, 64, 72})
        {
            auto g = granularWith(sineSource(220.0, 57));
            g->params().set(G::GrainMode, static_cast<float>(G::Mode::Pitched));
            g->params().set(G::filter(0, G::FilterOn), 0.0f);
            g->params().set(G::GrainFormant, formant);
            const auto out = render(*g, silence(48000), with({noteOn(0, key)}));
            const double hz = 440.0 * std::exp2((key - 69) / 12.0);
            INFO("key " << key << ", formant " << formant);
            CHECK(std::abs(fundamental(out, 12000, 4096) - hz) < hz * 0.01);
        }
}

TEST_CASE("granular: the pyramid keeps notes far above the root from aliasing", "[fast]")
{
    // A 9 kHz sine played two, three and four octaves up lies above the output's Nyquist: what
    // comes out is the anti-aliasing filter's leak, tens of dB down.
    auto reference = granularWith(sineSource(9000.0, 60));
    const double level = rmsDb(slice(render(*reference, silence(24000), with({noteOn(0, 60)})), 6000, 12000));
    for (const int up : {24, 36, 48})
        for (const int quality : {0, 1, 2})
        {
            auto g = granularWith(sineSource(9000.0, 60));
            g->params().set(G::Quality, static_cast<float>(quality));
            const auto out = render(*g, silence(24000), with({noteOn(0, 60 + up)}));
            INFO(up << " semitones up, quality " << quality);
            CHECK(rmsDb(slice(out, 6000, 12000)) < level - 50.0);
        }
}

TEST_CASE("granular: reading between samples leaves little but the tone", "[fast]")
{
    // A 2 kHz sine played a fifth down (1335 Hz) in long, still grains: what isn't the tone (its
    // grain-rate sidebands aside) is the interpolator's images and noise.
    // The SPEC's targets ("Performance": Quality): Eco under -40 dB, Normal -60, High -90.
    for (const auto [quality, floorDb] : {std::pair{0, -40.0}, std::pair{1, -60.0}, std::pair{2, -90.0}})
    {
        auto g = granularWith(sineSource(2000.0, 67, 6.0));
        g->params().set(G::Quality, static_cast<float>(quality));
        g->params().set(G::GrainSize, 1000.0f);
        g->params().set(G::filter(0, G::FilterOn), 0.0f);
        const auto out = render(*g, silence(72000), with({noteOn(0, 60)}));
        const double hz = 2000.0 * std::exp2(-7.0 / 12.0);
        const double outside = outsideBandDb(out, 24000, hz - 150.0, hz + 150.0);
        INFO("quality " << quality);
        WARN("quality " << quality << ": " << outside << " dB beside the tone");
        CHECK(outside < floorDb);
    }
}

TEST_CASE("granular: a long source's plain pyramid reads cleanly too", "[fast]")
{
    // Sources past two minutes (and a multisample's zones) keep 16-bit plain levels, read by the
    // 8-tap sinc at Normal and High.
    for (const auto [quality, floorDb] : {std::pair{0, -30.0}, std::pair{1, -60.0}, std::pair{2, -60.0}})
    {
        auto g = granularWith(sineSource(2000.0, 67, 6.0, 1, true));
        REQUIRE(g->sourceData(0)->compact);
        g->params().set(G::Quality, static_cast<float>(quality));
        g->params().set(G::GrainSize, 1000.0f);
        g->params().set(G::filter(0, G::FilterOn), 0.0f);
        const auto out = render(*g, silence(72000), with({noteOn(0, 60)}));
        const double hz = 2000.0 * std::exp2(-7.0 / 12.0);
        const double outside = outsideBandDb(out, 24000, hz - 150.0, hz + 150.0);
        INFO("quality " << quality);
        WARN("plain, quality " << quality << ": " << outside << " dB beside the tone");
        CHECK(outside < floorDb);
    }
}

TEST_CASE("granular: a render is the same for any block size, in every mode", "[fast]")
{
    const auto make = [](G::Mode mode)
    {
        auto g = std::make_unique<G>();
        g->setSourceData(0, GrainSource::build(GrainSource::chord(4.0, sr), "Chord", {}));
        auto& p = g->params();
        p.set(G::GrainMode, static_cast<float>(mode));
        p.set(G::PosSpray, 40.0f);
        p.set(G::PitchSpray, 7.0f);
        p.set(G::GrainSizeSpray, 50.0f);
        p.set(G::GrainJitter, 60.0f);
        p.set(G::GrainOverlap, 16.0f); // past Max Grains: grains are stolen
        p.set(G::GrainMax, 8.0f);
        p.set(G::GrainRepeats, 3.0f);
        p.set(G::GrainReverse, 30.0f);
        p.set(G::GColorCrush, 30.0f);
        p.set(G::GFilterOn, 1.0f);
        p.set(G::GFilterSpray, 2.0f);
        p.set(G::PitchGlide, 3.0f);
        p.set(G::env(0, G::EnvRelease), 80.0f);
        return g;
    };
    const std::vector<TimedEvent> events{noteOn(100, 57),   noteOn(3001, 64),   noteOff(20000, 57),
                                         noteOn(20000, 69), noteOff(30000, 64), noteOff(36000, 69)};
    for (const auto mode :
         {G::Mode::Classic, G::Mode::Loop, G::Mode::Cloud, G::Mode::Pitched, G::Mode::Rhythm})
        for (const bool tails : {true, false})
        {
            auto ref = make(mode);
            ref->params().set(G::VoiceTails, tails ? 1.0f : 0.0f);
            const auto reference = render(*ref, silence(48000), with(events));
            for (const auto& blocks :
                 std::vector<std::vector<int>>{{1}, {7}, {64}, {480}, {4096}, {3, 100, 31, 33, 1000}})
            {
                auto d = make(mode);
                d->params().set(G::VoiceTails, tails ? 1.0f : 0.0f);
                INFO("mode " << static_cast<int>(mode) << ", tails " << tails << ", first block "
                             << blocks.front());
                CHECK(bitIdentical(render(*d, silence(48000), with(events, blocks)), reference));
            }
        }
}

TEST_CASE("granular: Density Gain keeps the level as grains overlap", "[fast]")
{
    // Noise: grains from different places are uncorrelated, as the SPEC's target has it.
    audiofile::DecodedAudio noise;
    noise.sampleRate = sr;
    noise.rootKey = 60;
    const auto white = whiteNoise(static_cast<int>(4.0 * sr), 7u, 0.25f, 1);
    noise.channels = {std::vector<float>(white.channel(0), white.channel(0) + white.numSamples())};
    const auto source = GrainSource::build(noise, "Noise", {}, {.compact = false, .analyse = false});
    const auto level = [&source](float overlap, bool comp)
    {
        auto g = granularWith(source);
        g->params().set(G::GrainOverlap, overlap);
        g->params().set(G::PosSpray, 100.0f);
        g->params().set(G::GrainComp, comp ? 1.0f : 0.0f);
        return rmsDb(slice(render(*g, silence(48000), with({noteOn(0, 57)})), 12000, 36000));
    };
    // The SPEC: within 1 dB however many overlap.
    CHECK(std::abs(level(1.0f, true) - level(16.0f, true)) < 1.0);
    CHECK(std::abs(level(2.0f, true) - level(16.0f, true)) < 1.0);
    CHECK(level(16.0f, false) - level(2.0f, false) > 6.0);
}

TEST_CASE("granular: Quantize lands each grain's pitch on the scale asked for", "[fast]")
{
    const auto pitches = [](int quant, int chord, KeyInfo key)
    {
        auto g = granularWith(sineSource(261.6, 60));
        auto& p = g->params();
        p.set(G::PitchSpray, 24.0f);
        p.set(G::PitchQuant, static_cast<float>(quant));
        p.set(G::PitchChord, static_cast<float>(chord));
        p.set(G::GrainOverlap, 8.0f);
        auto o = with({noteOn(0, 60)});
        o.transport.key = key;
        (void)render(*g, silence(24000), o);
        std::vector<float> out;
        for (const auto& m : g->grains())
            out.push_back(m.pitch);
        return out;
    };
    const auto near = [](float x, float y) { return std::abs(x - y) < 1e-3f; };
    const auto onOctaves = pitches(3, 0, {});
    REQUIRE_FALSE(onOctaves.empty());
    for (const float s : onOctaves)
        CHECK(near(s, 12.0f * std::round(s / 12.0f)));
    const KeyInfo dMajor{
        .root = 2, .notes = (1 << 1) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11)};
    for (const float s : pitches(2, 0, dMajor))
        CHECK(dMajor.contains(static_cast<int>(std::lround(60.0f + s))));
    for (const float s : pitches(5, 0, {})) // Major: C E G in any octave
    {
        const int pc = ((static_cast<int>(std::lround(s)) % 12) + 12) % 12;
        CHECK((pc == 0 || pc == 4 || pc == 7));
    }
}

TEST_CASE("granular: a note plays no more grains than Max Grains", "[fast]")
{
    auto g = granularWith(sineSource(220.0, 57));
    g->params().set(G::GrainOverlap, 16.0f);
    g->params().set(G::GrainSize, 400.0f);
    g->params().set(G::GrainMax, 8.0f);
    int most = 0;
    auto o = with({noteOn(0, 57)});
    o.beforeBlock = [&most](Device& d, std::int64_t)
    { most = std::max(most, static_cast<int>(d.analysis().read(G::GrainsPlaying))); };
    const auto out = render(*g, silence(48000), o);
    CHECK(rmsDb(out) > -40.0);
    CHECK(most <= 8 + 1); // and one stepping aside
    CHECK(most >= 8);
}

TEST_CASE("granular: Grain Tails lets grains finish; off, they end with the amp", "[fast]")
{
    for (const bool tails : {true, false})
    {
        auto g = granularWith(sineSource(220.0, 57));
        g->params().set(G::GrainSize, 1000.0f);
        g->params().set(G::GrainOverlap, 2.0f);
        g->params().set(G::env(0, G::EnvRelease), 10.0f);
        g->params().set(G::VoiceTails, tails ? 1.0f : 0.0f);
        const auto out = render(*g, silence(96000), with({noteOn(0, 57), noteOff(24000, 57)}));
        INFO("tails " << tails);
        if (tails)
            CHECK(rmsDb(slice(out, 30000, 12000)) > -40.0);
        else
            CHECK(peakDb(slice(out, 30000, 12000)) < -100.0);
        CHECK(peakDb(slice(out, 90000, 6000)) < -100.0); // and everything ends
    }
}

TEST_CASE("granular: every pass rolls again; a kept roll plays the same pass", "[fast]")
{
    const auto make = []
    {
        auto g = std::make_unique<G>();
        g->setSourceData(0, GrainSource::build(GrainSource::chord(4.0, sr), "Chord", {}));
        g->params().set(G::PosSpray, 50.0f);
        g->params().set(G::PanSpray, 80.0f);
        return g;
    };
    const auto events = with({noteOn(0, 57), noteOn(4000, 64)});
    auto g = make();
    g->startPass(11);
    const auto first = render(*g, silence(24000), events);
    g->startPass(12);
    const auto second = render(*g, silence(24000), events);
    CHECK_FALSE(bitIdentical(first, second)); // fresh dice

    // Keep as Played holds the roll last played: every pass after plays it.
    g->keepRoll(true);
    g->startPass(13);
    CHECK(bitIdentical(render(*g, silence(24000), events), second));
    // The kept roll is stored, and reads back.
    nlohmann::json extra;
    g->writeExtraState(extra);
    auto h = make();
    h->readExtraState(extra);
    h->setSourceData(0, g->sourceData(0));
    CHECK(h->rollKept());
    CHECK(bitIdentical(render(*h, silence(24000), events), second));
    // Let it roll.
    g->keepRoll(false);
    g->startPass(14);
    CHECK_FALSE(bitIdentical(render(*g, silence(24000), events), second));
}

TEST_CASE("granular: Rhythm lands on the song's grid", "[fast]")
{
    // Quarter notes at 120 bpm: a beat each 24 000 samples. A note just after the downbeat waits
    // for the next beat.
    auto g = granularWith(sineSource(220.0, 57));
    g->params().set(G::GrainMode, static_cast<float>(G::Mode::Rhythm));
    g->params().set(G::GrainRate, 13.0f); // 1/4
    g->params().set(G::GrainSize, 50.0f);
    const auto out = render(*g, silence(72000), with({noteOn(1000, 57)}));
    CHECK(peakDb(slice(out, 0, 24000)) < -100.0);
    CHECK(rmsDb(slice(out, 24000, 2400)) > -30.0);
    CHECK(peakDb(slice(out, 24000 + 3000, 18000)) < -80.0); // a 50 ms grain, then nothing
    CHECK(rmsDb(slice(out, 48000, 2400)) > -30.0);
}

TEST_CASE("granular: Fire starts a grain the moment it rises past half", "[fast]")
{
    // Rhythm at 4 bars: no grain of its own for eight seconds; Fire starts one at 0.25 s.
    auto g = granularWith(sineSource(220.0, 57));
    g->params().set(G::GrainMode, static_cast<float>(G::Mode::Rhythm));
    g->params().set(G::GrainRate, 20.0f); // 4 bars
    g->params().set(G::GrainSize, 50.0f);
    auto o = with({noteOn(100, 57)}, {64});
    o.beforeBlock = [](Device& d, std::int64_t at)
    { d.params().set(G::GrainFire, at >= 12000 ? 100.0f : 0.0f); };
    const auto out = render(*g, silence(48000), o);
    CHECK(peakDb(slice(out, 0, 12000)) < -100.0);
    CHECK(rmsDb(slice(out, 12000, 2400)) > -30.0);
}

TEST_CASE("granular: what it costs", "[.benchmark]")
{
    // Release only: seconds of processing per second of audio at 48 kHz, for the SPEC's budgets
    // ("Performance": 8 notes x 64 grains of a mono source at Normal under 4 % of a core). A
    // cloud of 320 grains a second, each 200 ms long: 64 at once.
    const auto measure = [](const char* what, const std::function<void(G&)>& setUp)
    {
        auto g = std::make_unique<G>();
        g->setSourceData(0, sineSource(220.0, 57, 20.0));
        auto& p = g->params();
        p.set(G::GrainMode, static_cast<float>(G::Mode::Cloud));
        p.set(G::GrainSize, 200.0f);
        p.set(G::GrainDensity, 320.0f);
        p.set(G::GrainMax, 128.0f); // room: none of the 64 reads with Eco's interpolator
        p.set(G::PosSpray, 30.0f);
        setUp(*g);
        RenderOptions o = with({});
        for (const int key : {48, 52, 55, 59, 60, 64, 67, 71})
            o.events.push_back(noteOn(0, key));
        const double seconds = 5.0;
        const auto start = std::chrono::steady_clock::now();
        const auto out = render(*g, silence(static_cast<int>(seconds * sr)), o);
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
        WARN(what << ": " << 100.0 * took.count() / seconds << " % of a core (peak " << peakDb(out)
                  << " dB)");
    };
    for (const int quality : {0, 1, 2})
        measure(quality == 0   ? "8 notes x 64 grains, mono, Eco"
                : quality == 1 ? "8 notes x 64 grains, mono, Normal"
                               : "8 notes x 64 grains, mono, High",
                [quality](G& g) { g.params().set(G::Quality, static_cast<float>(quality)); });
    measure("the same, a stereo source", [](G& g) { g.setSourceData(0, sineSource(220.0, 57, 20.0, 2)); });
    measure("the same, a long (compact) source, Normal",
            [](G& g) { g.setSourceData(0, sineSource(220.0, 57, 150.0)); });
    measure("the same, every grain filtered", [](G& g) { g.params().set(G::GFilterOn, 1.0f); });
    measure("the same, every grain crushed", [](G& g) { g.params().set(G::GColorCrush, 30.0f); });
    measure("the same, a stereo source, every grain filtered and crushed (the SPEC's <= 10 %)",
            [](G& g)
            {
                g.setSourceData(0, sineSource(220.0, 57, 20.0, 2));
                g.params().set(G::GFilterOn, 1.0f);
                g.params().set(G::GColorCrush, 30.0f);
            });
    measure("the same, every grain filtered and crushed",
            [](G& g)
            {
                g.params().set(G::GFilterOn, 1.0f);
                g.params().set(G::GColorCrush, 30.0f);
            });
    measure("Pitched, 8 notes, 8 periods",
            [](G& g)
            {
                g.params().set(G::GrainMode, static_cast<float>(G::Mode::Pitched));
                g.params().set(G::GrainPeriods, 8.0f);
            });
    measure("Classic, 8 notes x 16 grains",
            [](G& g)
            {
                g.params().set(G::GrainMode, static_cast<float>(G::Mode::Classic));
                g.params().set(G::GrainOverlap, 16.0f);
            });
    measure("no voice filter", [](G& g) { g.params().set(G::filter(0, G::FilterOn), 0.0f); });
    measure("no voice filter, 8 notes x 8 grains",
            [](G& g)
            {
                g.params().set(G::filter(0, G::FilterOn), 0.0f);
                g.params().set(G::GrainDensity, 40.0f);
            });
    measure("no voice filter, 1 note x 64 grains",
            [](G& g)
            {
                g.params().set(G::filter(0, G::FilterOn), 0.0f);
                g.params().set(G::VoiceCount, 1.0f);
            });
    measure("both voice filters on",
            [](G& g)
            {
                g.params().set(G::filter(0, G::FilterOn), 1.0f);
                g.params().set(G::filter(1, G::FilterOn), 1.0f);
            });
}
