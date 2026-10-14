// The Wavetable Synth (devices/wavetable/SPEC.md).
#include "FactoryTables.h"
#include "TableFiles.h"
#include "WavetableSynth.h"
#include "fd/dsp/Fft.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using W = fd::devices::WavetableSynth;

namespace
{
constexpr double sr = 48000.0;

/// A held note: `key` for `seconds`, released, then `tail` seconds.
RenderOptions held(int key, double seconds, double tail = 0.3, float velocity = 0.8f)
{
    RenderOptions o;
    o.sampleRate = sr;
    o.events = {
        {.samplePosition = 0,
         .event = {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = velocity}},
        {.samplePosition = static_cast<std::int64_t>(seconds * sr),
         .event = {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}}};
    o.tailSamples = static_cast<int>(tail * sr);
    return o;
}

AudioBuffer silence(double seconds)
{
    return AudioBuffer(0, static_cast<int>(seconds * sr));
}
} // namespace

TEST_CASE("wavetable synth: its parameters keep the SPEC's IDs, and its tables are there", "[fast]")
{
    auto device = std::make_unique<W>();
    auto& w = *device;
    CHECK(W::NumParams == 379);
    CHECK(w.params().spec(W::osc(0, W::OscPos)).id == "osc1.pos");
    CHECK(w.params().spec(W::osc(2, W::OscRoute)).id == "osc3.route");
    CHECK(w.params().spec(W::filter(1, W::FilterEnv)).id == "filter2.env");
    CHECK(w.params().spec(W::env(3, W::EnvMode)).id == "env4.mode");
    CHECK(w.params().spec(W::lfo(7, W::LfoPoly)).id == "lfo8.poly");
    CHECK(w.params().spec(W::route(31, W::RouteScale)).id == "mod32.aux");
    CHECK(w.params().spec(W::Quality).id == "quality");
    CHECK(w.params().spec(W::Quality).hasFlag(ParamFlags::ChangesLatency));
    CHECK(fd::devices::wavetable::factoryTables().size() >= 60);
    for (const auto& t : fd::devices::wavetable::factoryTables())
    {
        INFO(t.id);
        const auto table = fd::devices::wavetable::factoryTable(t.id);
        REQUIRE(table != nullptr);
        CHECK(table->frames() >= 1);
        CHECK(table->highestHarmonic() >= 1);
    }
}

TEST_CASE("wavetable synth: a note plays at its pitch, and stops after its release", "[fast]")
{
    auto w = std::make_unique<W>();
    const auto out = render(*w, silence(1.0), held(69, 0.6));
    CHECK(rmsDb(out.slice(4800, 9600)) > -30.0);
    CHECK(peakDb(out.slice(static_cast<int>(0.9 * sr), static_cast<int>(0.05 * sr))) < -90.0);
    // A4: 440 Hz (zero crossings over half a second of the sustain).
    int crossings = 0;
    const int from = static_cast<int>(0.1 * sr), to = static_cast<int>(0.5 * sr);
    for (int i = from + 1; i < to; ++i)
        if (out.at(0, i - 1) < 0.0f && out.at(0, i) >= 0.0f)
            ++crossings;
    CHECK(crossings / ((to - from) / sr) == Approx(440.0).margin(3.0));
}

namespace
{
/// A long-term spectrum (Hann-windowed DFT at `hz`'s multiples and between) of one channel.
double inharmonicDb(const AudioBuffer& b, double hz, int from, double belowHz = sr / 2.0)
{
    // `hz` on a bin of a rectangular 8 192-point transform, the note's cycles periodic in it.
    // Stray energy counts up to `belowHz`.
    const int n = 8192;
    std::vector<std::complex<float>> buf(n);
    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t>(i)] = {b.at(0, from + i), 0.0f};
    dsp::Fft fft(13);
    fft.forward(buf.data());
    const auto step = static_cast<int>(std::lround(hz * n / sr));
    double all = 0.0, stray = 0.0;
    for (int k = 1; k < n / 2; ++k)
    {
        const double p = std::norm(buf[static_cast<size_t>(k)]);
        all += p;
        if (k % step != 0 && k * sr / n < belowHz)
            stray += p;
    }
    return 10.0 * std::log10(std::max(stray, 1e-30) / all);
}

double rms(const AudioBuffer& b, int from, int length, int channel = 0)
{
    double s = 0.0;
    for (int i = from; i < from + length; ++i)
        s += static_cast<double>(b.at(channel, i)) * b.at(channel, i);
    return 10.0 * std::log10(s / length + 1e-30);
}

std::unique_ptr<W> plain()
{
    // One oscillator straight out: no filter, flat velocity, no drift.
    auto w = std::make_unique<W>();
    auto& p = w->params();
    p.set(W::filter(0, W::FilterOn), 0.0f);
    p.set(W::AmpVelocity, 0.0f);
    p.set(W::VoiceDrift, 0.0f);
    p.set(W::env(0, W::EnvSustain), 100.0f);
    return w;
}
} // namespace

TEST_CASE("wavetable synth: in tune at every octave, and the whole voice without aliasing", "[fast]")
{
    // Fine is set so each note's pitch sits on a transform bin: the measure then sees aliasing only.
    for (const int key : {33, 57, 81, 93})
    {
        auto w = plain();
        auto& p = w->params();
        p.set(W::osc(0, W::OscWarp1), static_cast<float>(dsp::Warp::Sync));
        p.set(W::osc(0, W::OscWarp1Amt), 70.0f);
        const double hz = 440.0 * std::exp2((key - 69) / 12.0);
        const double bin = sr / 8192.0, onBin = std::round(hz / bin) * bin;
        p.set(W::osc(0, W::OscFine), static_cast<float>(1200.0 * std::log2(onBin / hz)));
        const auto out = render(*w, silence(0.6), held(key, 0.6, 0.0));
        INFO("key " << key);
        CHECK(inharmonicDb(out, onBin, 12000) < -80.0);
    }
    // Pitch, plainly: a sine table's zero crossings, within 0.1 cent over a second.
    auto w = plain();
    w->readExtraState(W::factoryTablePatch(0, "basic/sine"));
    for (const int key : {36, 60, 84})
    {
        const auto out = render(*w, silence(1.2), held(key, 1.2, 0.0));
        const double hz = 440.0 * std::exp2((key - 69) / 12.0);
        // Interpolated zero crossings over a second from 0.1 s.
        double first = -1.0, last = -1.0;
        int count = 0;
        for (int i = 4801; i < 4800 + 48000; ++i)
            if (out.at(0, i - 1) < 0.0f && out.at(0, i) >= 0.0f)
            {
                const double at = (i - 1) + out.at(0, i - 1) / (out.at(0, i - 1) - out.at(0, i));
                if (first < 0.0)
                    first = at;
                last = at;
                ++count;
            }
        const double measured = (count - 1) / ((last - first) / sr);
        INFO("key " << key);
        CHECK(std::abs(1200.0 * std::log2(measured / hz)) < 0.1);
    }
}

TEST_CASE("wavetable synth: unison keeps the level; filters in series or side by side", "[fast]")
{
    const auto levelWith = [](int copies)
    {
        auto w = plain();
        auto& p = w->params();
        // Even copies a semitone apart at the edges, high up: their beats average out in two seconds.
        p.set(W::osc(0, W::OscUni), static_cast<float>(copies));
        p.set(W::osc(0, W::OscUniMode), 0.0f);
        p.set(W::osc(0, W::OscDetune), 100.0f);
        p.set(W::osc(0, W::OscBlend), 100.0f);
        p.set(W::osc(0, W::OscRand), 100.0f);
        const auto out = render(*w, silence(2.5), held(81, 2.5, 0.0));
        return rms(out, 24000, 96000);
    };
    const double one = levelWith(1);
    for (const int copies : {4, 16})
    {
        INFO(copies << " copies");
        CHECK(levelWith(copies) == Approx(one).margin(1.5));
    }

    // Filter 1 a low pass at 500 Hz, filter 2 a high pass at 4 kHz: in series almost nothing
    // passes; side by side both bands do.
    const auto through = [](float routing)
    {
        auto w = plain();
        auto& p = w->params();
        p.set(W::filter(0, W::FilterOn), 1.0f);
        p.set(W::filter(0, W::FilterType), 0.0f); // LP 12
        p.set(W::filter(0, W::FilterCutoff), 500.0f);
        p.set(W::filter(0, W::FilterRes), 0.0f);
        p.set(W::filter(0, W::FilterEnv), 0.0f);
        p.set(W::filter(0, W::FilterKey), 0.0f);
        p.set(W::filter(1, W::FilterOn), 1.0f);
        p.set(W::filter(1, W::FilterType), 4.0f); // HP 12
        p.set(W::filter(1, W::FilterCutoff), 4000.0f);
        p.set(W::filter(1, W::FilterRes), 0.0f);
        p.set(W::filter(1, W::FilterKey), 0.0f);
        p.set(W::FiltersRouting, routing);
        p.set(W::osc(0, W::OscRoute), 2.0f); // both
        const auto out = render(*w, silence(1.0), held(45, 1.0, 0.0));
        return rms(out, 12000, 24000);
    };
    CHECK(through(100.0f) - through(0.0f) > 12.0);
}

TEST_CASE("wavetable synth: MPE moves only its note; a route reaches its target", "[fast]")
{
    // Two notes on their own channels; channel 2 bends up an octave (48 st range), channel 1 doesn't.
    auto w = plain();
    w->readExtraState(W::factoryTablePatch(0, "basic/sine"));
    w->params().set(W::VoiceBend, 48.0f);
    RenderOptions o;
    o.sampleRate = sr;
    o.events = {{0, {.type = EventType::NoteOn, .channel = 1, .key = 57, .value = 0.8f}},
                {0, {.type = EventType::NoteOn, .channel = 2, .key = 64, .value = 0.8f}},
                {100, {.type = EventType::PitchBend, .channel = 2, .value = 0.25f}}}; // +12 st
    const auto out = render(*w, silence(0.5), o);
    const auto magnitudeAt = [&out](double hz)
    { return std::abs(responseAt(out.channel(0) + 9600, 8192, hz, sr)); };
    CHECK(magnitudeAt(220.0) > 20.0 * magnitudeAt(440.0)); // channel 1 stays at A3
    CHECK(magnitudeAt(659.3) > 20.0 * magnitudeAt(329.6)); // channel 2 went from E4 to E5

    // Macro 1 → Osc 1 Level, -100 %: at Macro 1 = 100 % the oscillator is silent.
    auto m = plain();
    auto& p = m->params();
    p.set(W::route(0, W::RouteSource), static_cast<float>(W::Source::Macro1));
    p.set(W::route(0, W::RouteTarget), static_cast<float>(W::oscTarget(0, W::TLevel)));
    p.set(W::route(0, W::RouteAmount), -100.0f);
    const double open = rms(render(*m, silence(0.5), held(57, 0.5, 0.0)), 9600, 9600);
    p.set(W::macro(0), 100.0f);
    const double shut = rms(render(*m, silence(0.5), held(57, 0.5, 0.0)), 9600, 9600);
    CHECK(open > -30.0);
    CHECK(shut < -90.0);
}

TEST_CASE("wavetable synth: a table of your own travels in its state", "[fast]")
{
    // A two-frame table (a sine, then a square) put on Osc 2, saved and loaded again.
    std::vector<float> cycles(2 * 2048);
    for (int i = 0; i < 2048; ++i)
    {
        cycles[static_cast<size_t>(i)] = static_cast<float>(std::sin(dsp::twoPi * i / 2048.0));
        cycles[static_cast<size_t>(2048 + i)] = i < 1024 ? 0.8f : -0.8f;
    }
    const auto table = dsp::Wavetable::fromCycles(cycles.data(), 2048, 2);
    auto a = std::make_unique<W>();
    a->readExtraState(W::userTablePatch(1, "Mine", table));
    CHECK(a->tableId(1) == "user");
    CHECK(a->tableName(1) == "Mine");
    REQUIRE(a->table(1) != nullptr);
    CHECK(a->table(1)->frames() == 2);
    nlohmann::json state;
    a->writeExtraState(state);
    auto b = std::make_unique<W>();
    b->readExtraState(state);
    REQUIRE(b->table(1) != nullptr);
    CHECK(b->tableName(1) == "Mine");
    for (int i = 0; i < 2048; i += 97)
        CHECK(b->table(1)->samples(1)[i] == Approx(a->table(1)->samples(1)[i]).margin(1e-3));
    // Back to a factory table: the user table's data goes.
    b->readExtraState(W::factoryTablePatch(1, "analog/pwm"));
    nlohmann::json after;
    b->writeExtraState(after);
    CHECK(after.value("osc2.table", "") == "analog/pwm");
    CHECK_FALSE(after.contains("osc2.pcm"));
}

namespace
{
void put32(std::vector<unsigned char>& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
void put16(std::vector<unsigned char>& b, std::uint16_t v)
{
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>(v >> 8));
}
void putText(std::vector<unsigned char>& b, std::string_view s)
{
    b.insert(b.end(), s.begin(), s.end());
}

/// A 16-bit mono WAV of `samples`, with a `clm ` chunk when `clm` isn't empty.
std::vector<unsigned char> wav(const std::vector<float>& samples, std::string_view clm)
{
    std::vector<unsigned char> body;
    putText(body, "WAVE");
    putText(body, "fmt ");
    put32(body, 16);
    put16(body, 1);
    put16(body, 1);
    put32(body, 48000);
    put32(body, 96000);
    put16(body, 2);
    put16(body, 16);
    if (!clm.empty())
    {
        putText(body, "clm ");
        put32(body, static_cast<std::uint32_t>(clm.size()));
        putText(body, clm);
        if (clm.size() % 2 == 1)
            body.push_back(0);
    }
    putText(body, "data");
    put32(body, static_cast<std::uint32_t>(samples.size() * 2));
    for (const float s : samples)
        put16(body, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(s * 32767.0f))));
    std::vector<unsigned char> file;
    putText(file, "RIFF");
    put32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

std::string writeFile(const std::string& name, const std::vector<unsigned char>& bytes)
{
    const auto path = (std::filesystem::temp_directory_path() / ("fd-wavetable-" + name)).string();
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}
} // namespace

TEST_CASE("wavetable files: frames from the clm chunk, from .wt, or a single cycle", "[fast]")
{
    using fd::devices::wavetable::importTable;
    // Four frames of 256 samples, a sine at harmonic 1, 2, 3, 4.
    std::vector<float> four(4 * 256);
    for (int f = 0; f < 4; ++f)
        for (int i = 0; i < 256; ++i)
            four[static_cast<size_t>(f * 256 + i)] =
                0.5f * static_cast<float>(std::sin(dsp::twoPi * (f + 1) * i / 256.0));
    const auto clm = importTable(writeFile("clm.wav", wav(four, "<!>256 10000000 wavetable (test)")));
    REQUIRE(clm.has_value());
    CHECK(clm->frameLength == 256);
    CHECK(clm->frames == 4);
    const auto table = dsp::Wavetable::fromCycles(clm->samples.data(), clm->frameLength, clm->frames);
    CHECK(std::abs(table.spectrum(2)[3]) > 100.0f * std::abs(table.spectrum(2)[1])); // frame 3: harmonic 3

    // Surge's .wt: three float frames of 512.
    std::vector<unsigned char> wt;
    putText(wt, "vawt");
    put32(wt, 512);
    put16(wt, 3);
    put16(wt, 0);
    for (int i = 0; i < 3 * 512; ++i)
    {
        const float v = static_cast<float>(std::sin(dsp::twoPi * i / 512.0));
        std::uint32_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        put32(wt, raw);
    }
    const auto surge = importTable(writeFile("table.wt", wt));
    REQUIRE(surge.has_value());
    CHECK(surge->frameLength == 512);
    CHECK(surge->frames == 3);
    CHECK(surge->samples[128] == Approx(1.0f).margin(1e-6));

    // A single cycle of 600 samples, no metadata.
    std::vector<float> cycle(600);
    for (int i = 0; i < 600; ++i)
        cycle[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(dsp::twoPi * i / 600.0));
    const auto single = importTable(writeFile("single.wav", wav(cycle, {})));
    REQUIRE(single.has_value());
    CHECK(single->frameLength == 600);
    CHECK(single->frames == 1);
    // Not audio at all: a reason, nothing else.
    std::string why;
    CHECK_FALSE(importTable(writeFile("junk.wav", {'n', 'o', 'p', 'e'}), 0, &why).has_value());
    CHECK_FALSE(why.empty());
}

TEST_CASE("wavetable synth: FM, PM, RM and AM free of aliasing", "[fast]")
{
    // A saw carrier on a high note, modulated at full amount by a sine at the same pitch: every
    // sideband is a harmonic; anything else in the audible band is aliasing (above 20 kHz, the
    // oversampler's transition band lets a little through: inaudible). devices/wavetable/SPEC.md.
    const auto inharmonicAt = [](dsp::Warp warp, int quality)
    {
        auto w = plain();
        auto& p = w->params();
        nlohmann::json state = W::factoryTablePatch(0, "basic/saw");
        state.merge_patch(W::factoryTablePatch(1, "basic/sine")); // readExtraState takes the whole state
        w->readExtraState(state);
        p.set(W::Quality, static_cast<float>(quality));
        p.set(W::osc(0, W::OscWarp1), static_cast<float>(warp));
        p.set(W::osc(0, W::OscWarp1Amt), 100.0f);
        p.set(W::osc(0, W::OscWarp1Src), 1.0f);
        p.set(W::osc(1, W::OscOn), 1.0f);
        p.set(W::osc(1, W::OscLevel), -70.0f); // heard only through osc 1
        const double hz = 440.0 * std::exp2((81 - 69) / 12.0);
        const double bin = sr / 8192.0, onBin = std::round(hz / bin) * bin;
        const auto fine = static_cast<float>(1200.0 * std::log2(onBin / hz));
        p.set(W::osc(0, W::OscFine), fine);
        p.set(W::osc(1, W::OscFine), fine);
        return inharmonicDb(render(*w, silence(0.6), held(81, 0.6, 0.0)), onBin, 12000, 20000.0);
    };
    for (const auto [warp, name] : {std::pair{dsp::Warp::Fm, "FM"}, std::pair{dsp::Warp::Pm, "PM"},
                                    std::pair{dsp::Warp::Rm, "RM"}, std::pair{dsp::Warp::Am, "AM"}})
    {
        const double eco = inharmonicAt(warp, 0), normal = inharmonicAt(warp, 1),
                     high = inharmonicAt(warp, 2);
        WARN(name << " aliasing: Eco " << eco << " dB, Normal " << normal << " dB, High " << high << " dB");
        CHECK(normal < -70.0);
        CHECK(high < -85.0);
    }
}

TEST_CASE("wavetable synth: what it costs", "[.benchmark]")
{
    // Release only: seconds of processing per second of audio at 48 kHz, for the SPEC's CPU target
    // (8 notes, 3 oscillators, 8 copies each, under 10 % of a core at Normal).
    const auto measure = [](const char* what, int quality, const std::function<void(W&)>& setUp)
    {
        auto w = std::make_unique<W>();
        auto& p = w->params();
        for (int n = 0; n < 3; ++n)
        {
            p.set(W::osc(n, W::OscOn), 1.0f);
            p.set(W::osc(n, W::OscUni), 8.0f);
            p.set(W::osc(n, W::OscDetune), 30.0f);
        }
        p.set(W::Quality, static_cast<float>(quality));
        setUp(*w);
        RenderOptions o;
        o.sampleRate = sr;
        for (const int key : {48, 52, 55, 59, 60, 64, 67, 71})
            o.events.push_back(
                {.samplePosition = 0,
                 .event = {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = 0.8f}});
        const double seconds = 5.0;
        const auto start = std::chrono::steady_clock::now();
        const auto out = render(*w, silence(seconds), o);
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
        WARN(what << (quality == 0   ? ", Eco: "
                      : quality == 1 ? ", Normal: "
                                     : ", High: ")
                  << 100.0 * took.count() / seconds << " % of a core (peak " << peakDb(out) << " dB)");
    };
    for (int quality = 0; quality < 3; ++quality)
    {
        measure("8 notes x 3 oscillators x 8 copies, still", quality, [](W&) {});
        measure("the same, Position moved by an LFO and spread over the copies", quality,
                [](W& w)
                {
                    auto& p = w.params();
                    for (int n = 0; n < 3; ++n)
                    {
                        p.set(W::osc(n, W::OscSpread), 100.0f);
                        p.set(W::route(n, W::RouteSource), static_cast<float>(W::Source::Lfo1));
                        p.set(W::route(n, W::RouteTarget), static_cast<float>(W::oscTarget(n, W::TPosition)));
                        p.set(W::route(n, W::RouteAmount), 50.0f);
                    }
                });
        measure("the same, with Bend and Sync warps and FM from Osc 2", quality,
                [](W& w)
                {
                    auto& p = w.params();
                    for (int n = 0; n < 3; ++n)
                    {
                        p.set(W::osc(n, W::OscSpread), 100.0f);
                        p.set(W::osc(n, W::OscWarpSpread), 100.0f);
                        p.set(W::osc(n, W::OscWarp1),
                              static_cast<float>(n == 0 ? dsp::Warp::Fm : dsp::Warp::Sync));
                        p.set(W::osc(n, W::OscWarp1Amt), 40.0f);
                        p.set(W::osc(n, W::OscWarp2), static_cast<float>(dsp::Warp::BendPlus));
                        p.set(W::osc(n, W::OscWarp2Amt), 30.0f);
                        p.set(W::route(n, W::RouteSource), static_cast<float>(W::Source::Lfo1));
                        p.set(W::route(n, W::RouteTarget), static_cast<float>(W::oscTarget(n, W::TPosition)));
                        p.set(W::route(n, W::RouteAmount), 50.0f);
                    }
                });
    }
}

TEST_CASE("wavetable synth: what a cycle costs", "[.benchmark]")
{
    const auto table = fd::devices::wavetable::factoryTable("basic/shapes");
    dsp::CycleRenderer renderer;
    renderer.prepare();
    std::vector<float> buffer(dsp::CycleRenderer::bufferSize);
    for (const double hz : {65.0, 261.0, 1046.0})
        for (const auto [warp, name] :
             {std::pair{dsp::Warp::Off, "no warp"}, std::pair{dsp::Warp::BendPlus, "Bend +"},
              std::pair{dsp::Warp::Sync, "Sync"}})
        {
            const int harmonics = static_cast<int>(20000.0 / hz);
            const std::array<dsp::CycleRenderer::WarpSetting, 2> warps{{{warp, 0.5f, nullptr}, {}}};
            const int count = 2000;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < count; ++i)
                renderer.render(*table, 0.3 + 0.0001 * i, warps, harmonics, buffer.data());
            const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
            WARN(hz << " Hz, " << name << ": " << 1e6 * took.count() / count << " us a cycle");
        }
}

TEST_CASE("wavetable synth: a table the editor made keeps its recipe in the state; a factory table drops it",
          "[fast]")
{
    using namespace fd::devices::wavetable;
    Recipe r;
    r.name = "Drawn";
    r.frames = 2;
    Recipe::Layer f;
    f.kind = Recipe::SourceKind::Formula;
    f.formula = "sin(2*pi*x) + t*sin(6*pi*x)";
    r.layers.push_back(f);
    const auto baked = bake(r);

    auto w = std::make_unique<W>();
    w->readExtraState(W::recipePatch(1, r, baked));
    CHECK(w->tableId(1) == "user");
    CHECK(w->tableName(1) == "Drawn");
    REQUIRE(w->recipe(1).is_object());
    CHECK(w->recipe(1) == toJson(r));

    nlohmann::json saved;
    w->writeExtraState(saved);
    auto again = std::make_unique<W>();
    again->readExtraState(saved);
    CHECK(again->recipe(1) == toJson(r));
    CHECK(again->table(1)->frames() == 2);

    saved.merge_patch(W::factoryTablePatch(1, "basic/saw"));
    again->readExtraState(saved);
    CHECK(again->recipe(1).is_null());
    nlohmann::json resaved;
    again->writeExtraState(resaved);
    CHECK_FALSE(resaved.contains("osc2.recipe"));
}
