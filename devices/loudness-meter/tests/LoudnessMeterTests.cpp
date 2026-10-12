// Loudness Meter (devices/loudness-meter/SPEC.md): the audio passes unchanged; EBU Tech 3341 and
// 3342 through the module; Song counts each moment of the song once, Running everything; stereo;
// what normalising does; the measurement kept with the project.
#include "LoudnessMeter.h"
#include "fd/device/Realtime.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <nlohmann/json.hpp>
#include <random>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using M = fd::devices::LoudnessMeter;

namespace
{
/// A 1 kHz sine in both channels: (level in dBFS, seconds) after each other.
struct Segment
{
    double dbfs, seconds;
};

std::vector<float> tones(const std::vector<Segment>& segments, double rate)
{
    std::vector<float> out;
    double phase = 0.0;
    for (const auto& s : segments)
    {
        const double amplitude = s.dbfs <= -200.0 ? 0.0 : std::pow(10.0, s.dbfs / 20.0);
        const auto n = static_cast<size_t>(std::lround(s.seconds * rate));
        for (size_t i = 0; i < n; ++i)
        {
            out.push_back(static_cast<float>(amplitude * std::sin(phase)));
            phase += 2.0 * M_PI * 1000.0 / rate;
        }
    }
    return out;
}

/// A meter fed by hand: the song's position and whether it plays are the test's.
struct Meter
{
    explicit Meter(double rate = 48000.0, int measure = 0) : rate(rate)
    {
        meter.params().set(M::Measure, static_cast<float>(measure));
        meter.prepare({.sampleRate = rate, .maxBlockSize = 1024});
    }

    /// Plays `left`/`right` from song position `at` (samples), in uneven blocks.
    void play(const std::vector<float>& left, const std::vector<float>& right, std::int64_t at,
              bool playing = true)
    {
        std::vector<float> outL(1024), outR(1024);
        size_t done = 0;
        for (int block = 1; done < left.size(); block = block * 7 % 1021 + 1)
        {
            const auto n = std::min(left.size() - done, static_cast<size_t>(block));
            float* in[2] = {const_cast<float*>(left.data() + done), const_cast<float*>(right.data() + done)};
            float* out[2] = {outL.data(), outR.data()};
            ProcessContext ctx;
            ctx.input = {in, 2};
            ctx.output = {out, 2};
            ctx.numSamples = static_cast<int>(n);
            ctx.transport.playing = playing;
            ctx.transport.samplePosition = at + static_cast<std::int64_t>(done);
            processRealtime(meter, ctx);
            done += n;
        }
    }
    void play(const std::vector<float>& both, std::int64_t at = 0, bool playing = true)
    {
        play(both, both, at, playing);
    }
    /// Plays seconds `from` to `to` of `song`, where the song has them.
    void playPart(const std::vector<float>& song, double from, double to)
    {
        const auto a = static_cast<size_t>(std::lround(from * rate)),
                   b = static_cast<size_t>(std::lround(to * rate));
        play(std::vector<float>(song.begin() + static_cast<long>(a), song.begin() + static_cast<long>(b)),
             static_cast<std::int64_t>(a));
    }

    [[nodiscard]] float read(int slot) const { return const_cast<M&>(meter).analysis().read(slot); }
    void request(int r)
    {
        meter.analysis().request(r);
        play(std::vector<float>(1, 0.0f), 0, false); // a block, so it's taken
    }

    double rate;
    M meter;
};

/// What a meter running over the whole song in one go reads (dsp::LoudnessMeter).
float offlineIntegrated(const std::vector<float>& song, double rate)
{
    dsp::LoudnessMeter m;
    m.prepare(rate, 2);
    const float* in[2] = {song.data(), song.data()};
    m.process(in, static_cast<int>(song.size()));
    return m.integrated();
}
} // namespace

TEST_CASE("loudness meter: the audio passes unchanged, with no latency", "[fast]")
{
    M m;
    const auto in = stereoSines(440.0, 660.0, 48000.0, 48000);
    CHECK(nullResidualDb(render(m, in, {.sampleRate = 48000.0}), in) == silenceDb);
    CHECK(nullResidualDb(render(m, in, {.sampleRate = 48000.0, .inPlace = true}), in) == silenceDb);
    CHECK(m.latencySamples() == 0);
    CHECK(m.tailSeconds() == 0.0);
}

TEST_CASE("loudness meter: EBU Tech 3341 cases 1-5, 9 and 12, at 44.1, 48 and 96 kHz", "[fast]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const int measure : {0, 1}) // Song (playing from the start) and Running
        {
            INFO(rate << " Hz, " << (measure == 0 ? "Song" : "Running"));
            for (const double level : {-23.0, -33.0}) // 1 and 2
            {
                Meter m(rate, measure);
                m.play(tones({{level, 20.0}}, rate));
                CHECK(m.read(M::Momentary) == Approx(level).margin(0.1));
                CHECK(m.read(M::ShortTerm) == Approx(level).margin(0.1));
                CHECK(m.read(M::Integrated) == Approx(level).margin(0.1));
            }
            for (const auto& segments : std::vector<std::vector<Segment>>{
                     {{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}},                               // 3
                     {{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}}, // 4
                     {{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}}})                              // 5
            {
                Meter m(rate, measure);
                m.play(tones(segments, rate));
                CHECK(m.read(M::Integrated) == Approx(-23.0).margin(0.1));
            }
            {
                // 9: short-term steady at -23 after 3 s.
                std::vector<Segment> s;
                for (int k = 0; k < 5; ++k)
                    s.push_back({-20.0, 1.34}), s.push_back({-30.0, 1.66});
                Meter m(rate, measure);
                m.play(tones(s, rate));
                CHECK(m.read(M::ShortTerm) == Approx(-23.0).margin(0.1));
            }
            {
                // 12: momentary steady at -23 after 1 s.
                std::vector<Segment> s;
                for (int k = 0; k < 25; ++k)
                    s.push_back({-20.0, 0.18}), s.push_back({-30.0, 0.22});
                Meter m(rate, measure);
                m.play(tones(s, rate));
                CHECK(m.read(M::Momentary) == Approx(-23.0).margin(0.1));
            }
        }
}

TEST_CASE("loudness meter: EBU Tech 3341 cases 11 and 14, the maximums as they grow", "[fast]")
{
    const double rate = 48000.0;
    SECTION("11: short-term")
    {
        Meter m(rate);
        std::int64_t at = 0;
        for (int i = 0; i < 20; ++i)
        {
            const auto t = tones({{-300.0, i * 0.15}, {-38.0 + i, 3.0}, {-300.0, 3.0 - i * 0.15}}, rate);
            m.play(t, at);
            at += static_cast<std::int64_t>(t.size());
            INFO("tone " << i);
            CHECK(m.read(M::MaxShortTerm) == Approx(-38.0 + i).margin(0.1));
        }
    }
    SECTION("14: momentary")
    {
        Meter m(rate);
        std::int64_t at = 0;
        for (int i = 0; i < 20; ++i)
        {
            const auto t = tones({{-300.0, i * 0.02}, {-38.0 + i, 0.4}, {-300.0, 0.4 - i * 0.02}}, rate);
            m.play(t, at);
            at += static_cast<std::int64_t>(t.size());
            INFO("tone " << i);
            CHECK(m.read(M::MaxMomentary) == Approx(-38.0 + i).margin(0.1));
        }
    }
}

TEST_CASE("loudness meter: EBU Tech 3342 cases 1-4, the loudness range", "[fast]")
{
    const double rate = 48000.0;
    const std::vector<std::pair<std::vector<Segment>, double>> cases{
        {{{-20.0, 20.0}, {-30.0, 20.0}}, 10.0},
        {{{-20.0, 20.0}, {-15.0, 20.0}}, 5.0},
        {{{-40.0, 20.0}, {-20.0, 20.0}}, 20.0},
        {{{-50.0, 20.0}, {-35.0, 20.0}, {-20.0, 20.0}, {-35.0, 20.0}, {-50.0, 20.0}}, 15.0}};
    for (const auto& [segments, range] : cases)
    {
        Meter m(rate);
        m.play(tones(segments, rate));
        CHECK(m.read(M::Range) == Approx(range).margin(0.1));
    }
    // Calibration: a 1 kHz stereo sine at -18 dBFS reads -18.0 LUFS.
    Meter m(rate);
    m.play(tones({{-18.0, 5.0}}, rate));
    CHECK(m.read(M::Integrated) == Approx(-18.0).margin(0.05));
}

TEST_CASE("loudness meter: Song counts each moment of the song once, the latest time it played", "[fast]")
{
    const double rate = 48000.0;
    // A 60 s song: 30 s of verse at -30, 30 s of chorus at -16.
    const auto song = tones({{-30.0, 30.0}, {-16.0, 30.0}}, rate);
    const float once = offlineIntegrated(song, rate);

    SECTION("played twice: the same")
    {
        Meter m(rate);
        m.play(song);
        const float first = m.read(M::Integrated), range = m.read(M::Range), peak = m.read(M::TruePeakMax);
        CHECK(first == Approx(once).margin(0.05));
        m.play(song);
        CHECK(m.read(M::Integrated) == Approx(first).margin(0.01));
        CHECK(m.read(M::Range) == Approx(range).margin(0.01));
        CHECK(m.read(M::TruePeakMax) == Approx(peak).margin(0.01));
        CHECK(m.read(M::Counted) == Approx(60.0).margin(0.1));
    }
    SECTION("the verse looped five times: still the song played once; Running reads it quieter")
    {
        Meter song_(rate), running(rate, 1);
        for (auto* m : {&song_, &running})
        {
            m->play(song);
            for (int k = 0; k < 5; ++k)
                m->playPart(song, 5.0, 15.0);
        }
        CHECK(song_.read(M::Integrated) == Approx(once).margin(0.05));
        CHECK(running.read(M::Integrated) < once - 1.0f); // the quiet verse counted five more times
    }
    SECTION("a part replaced by a louder one and played again: the edited song's reading")
    {
        Meter m(rate);
        m.play(song);
        auto edited = song;
        for (size_t i = static_cast<size_t>(10 * rate); i < static_cast<size_t>(20 * rate); ++i)
            edited[i] *= 4.0f; // +12 dB
        m.playPart(edited, 10.0, 20.0);
        CHECK(m.read(M::Integrated) == Approx(offlineIntegrated(edited, rate)).margin(0.1));
    }
    SECTION("a jump in the middle of a step drops that step; the rest stay")
    {
        Meter m(rate);
        m.playPart(song, 0.0, 1.05);
        m.playPart(song, 5.0, 6.0);
        const auto& s = m.meter.store();
        CHECK(s.heard(9));
        CHECK_FALSE(s.heard(10)); // 1.0 to 1.1 s, cut at 1.05
        CHECK(s.heard(50));
        CHECK(s.heardCount() == 20);
    }
    SECTION("stopped: the live readings move, nothing is counted, and the meter says it's waiting")
    {
        Meter m(rate);
        m.play(song);
        const float counted = m.read(M::Integrated);
        m.play(tones({{-6.0, 4.0}}, rate), 0, false);
        CHECK(m.read(M::ShortTerm) == Approx(-6.0).margin(0.1));
        CHECK(m.read(M::Integrated) == counted);
        CHECK(m.read(M::Counting) == static_cast<float>(M::CountingState::WaitingForSong));
    }
    SECTION("paused: integrated and range hold; resumed, counting goes on")
    {
        Meter m(rate);
        m.playPart(song, 0.0, 30.0);
        const float verse = m.read(M::Integrated);
        m.request(M::Pause);
        m.playPart(song, 30.0, 60.0);
        CHECK(m.read(M::Integrated) == verse);
        CHECK(m.read(M::ShortTerm) == Approx(-16.0).margin(0.1)); // live
        CHECK(m.read(M::Counting) == static_cast<float>(M::CountingState::Paused));
        m.request(M::Resume);
        m.playPart(song, 30.0, 60.0);
        CHECK(m.read(M::Integrated) == Approx(once).margin(0.05));
    }
    SECTION("Reset clears what's counted; switching Measure starts again")
    {
        Meter m(rate);
        m.play(song);
        m.request(M::Reset);
        CHECK(std::isnan(m.read(M::Integrated)));
        CHECK(m.meter.store().heardCount() == 0);
        m.play(song);
        m.meter.params().set(M::Measure, 1.0f);
        m.play(std::vector<float>(480, 0.0f), 0, false);
        CHECK(m.meter.store().heardCount() == 0);
    }
    SECTION("before the song's start (a count-in): not counted")
    {
        Meter m(rate);
        m.play(tones({{-20.0, 2.0}}, rate), -static_cast<std::int64_t>(rate)); // -1 s to 1 s
        CHECK(m.meter.store().heardCount() == 10);
    }
}

TEST_CASE("loudness meter: stereo correlation and balance", "[fast]")
{
    const double rate = 48000.0;
    const auto tone = tones({{-12.0, 4.0}}, rate);
    {
        Meter m(rate);
        m.play(tone);
        CHECK(m.read(M::Correlation) == Approx(1.0).margin(0.001));
        CHECK(m.read(M::Balance) == Approx(0.0).margin(0.01));
    }
    {
        auto inverted = tone;
        for (auto& x : inverted)
            x = -x;
        Meter m(rate);
        m.play(tone, inverted, 0);
        CHECK(m.read(M::Correlation) == Approx(-1.0).margin(0.001));
        CHECK(m.read(M::CorrelationMin) == Approx(-1.0).margin(0.001));
    }
    {
        const auto noise = whiteNoise(static_cast<int>(4 * rate), 11);
        std::vector<float> l(noise.channel(0), noise.channel(0) + noise.numSamples());
        const auto other = whiteNoise(static_cast<int>(4 * rate), 23);
        std::vector<float> r(other.channel(0), other.channel(0) + other.numSamples());
        Meter m(rate);
        m.play(l, r, 0);
        CHECK(m.read(M::Correlation) == Approx(0.0).margin(0.05));
    }
    {
        auto quieter = tone;
        for (auto& x : quieter)
            x *= 0.5f; // right 6 dB down
        Meter m(rate);
        m.play(tone, quieter, 0);
        CHECK(m.read(M::Balance) == Approx(6.02).margin(0.1));
    }
    {
        Meter m(rate);
        m.play(std::vector<float>(static_cast<size_t>(rate), 0.0f));
        CHECK(std::isnan(m.read(M::Correlation))); // silence: no reading
    }
}

TEST_CASE("loudness meter: what normalising does, and peak to loudness", "[fast]")
{
    // Louder than the target: turned down to it, whatever the peaks.
    CHECK(M::normalisedGain(-9.0f, -0.5f, -14.0f, -1.0f) == Approx(-5.0f));
    CHECK(M::normalisedGain(-9.0f, 2.0f, -14.0f, -1.0f) == Approx(-5.0f));
    // Quieter: turned up only as far as the peaks stay under the ceiling.
    CHECK(M::normalisedGain(-20.0f, -9.0f, -14.0f, -1.0f) == Approx(6.0f));
    CHECK(M::normalisedGain(-20.0f, -5.0f, -14.0f, -1.0f) == Approx(4.0f));
    CHECK(M::normalisedGain(-20.0f, 0.0f, -14.0f, -1.0f) == Approx(0.0f));
    CHECK(std::isnan(M::normalisedGain(NAN, -5.0f, -14.0f, -1.0f)));

    Meter m;
    m.meter.params().set(M::Target, -14.0f);
    m.play(tones({{-9.0, 10.0}}, 48000.0));
    CHECK(m.read(M::Normalised) == Approx(-5.0).margin(0.1));
    CHECK(m.read(M::PeakToLoudness) == Approx(m.read(M::TruePeakMax) - m.read(M::Integrated)).margin(0.001));
    CHECK(m.read(M::PeakToLoudness) == Approx(0.0).margin(0.1)); // a 1 kHz sine reads its peak in LUFS
}

TEST_CASE("loudness meter: the song's measurement is kept with the project", "[fast]")
{
    const double rate = 48000.0;
    const auto song = tones({{-30.0, 20.0}, {-16.0, 20.0}, {-22.0, 5.0}}, rate);
    Meter m(rate);
    m.playPart(song, 0.0, 30.0);
    m.playPart(song, 35.0, 45.0); // a gap from 30 to 35 s
    nlohmann::json extra = nlohmann::json::object();
    m.meter.writeExtraState(extra);
    REQUIRE(extra.contains("song.measurement"));
    CHECK(extra["song.measurement"]["runs"].size() == 2);

    const auto same = [&](Meter& other)
    {
        for (const int slot :
             {M::Integrated, M::Range, M::MaxMomentary, M::MaxShortTerm, M::TruePeakMax, M::Counted})
        {
            INFO("slot " << slot);
            CHECK(other.read(slot) == Approx(m.read(slot)).margin(0.01));
        }
        CHECK(other.meter.store().shortTermAt(250) == Approx(m.meter.store().shortTermAt(250)).margin(0.01));
        CHECK_FALSE(other.meter.store().heard(320));
    };
    SECTION("read before it's prepared, as a project opens")
    {
        M fresh;
        fresh.readExtraState(extra);
        Meter reopened(rate);
        reopened.meter.readExtraState(extra); // a host may also apply it after prepare()
        reopened.play(std::vector<float>(480, 0.0f), 0, false);
        same(reopened);
    }
    SECTION("presets leave it alone: a state without it changes nothing")
    {
        m.meter.readExtraState(nlohmann::json::object());
        m.play(std::vector<float>(480, 0.0f), 0, false);
        CHECK(m.meter.store().heardCount() == 400);
    }
    SECTION("measured in Running, opened in Song: not counted")
    {
        extra["song.measurement"]["mode"] = "running";
        Meter reopened(rate);
        reopened.meter.readExtraState(extra);
        reopened.play(std::vector<float>(480, 0.0f), 0, false);
        CHECK(reopened.meter.store().heardCount() == 0);
    }
}

TEST_CASE("loudness meter: past three hours, it stops counting and says so", "[fast]")
{
    Meter m;
    m.play(tones({{-20.0, 0.5}}, 48000.0), static_cast<std::int64_t>(M::storeSteps) * 4800);
    CHECK(m.meter.store().heardCount() == 0);
    CHECK(m.read(M::Counting) == static_cast<float>(M::CountingState::Full));
}
