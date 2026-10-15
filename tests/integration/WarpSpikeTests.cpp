// The audio editor's warping, step 0: the engine spike (M3 step P6b, ADR 0063,
// docs/design/audio-editor.md §15, §22). Each point is kept as a test of tracktion's real-time path
// as Segno uses it, with tracktion_engine-warped-clips.patch: a clip following a tempo change; a
// warped clip at its own tempo untouched, stretched or resampled; fades, edge fades and crossfades
// on warped clips; loops kept in beats; splits; bounces matching their render; Repitch, and
// transposing without Warp; the cost of 32 warped clips. Stretched audio is never the same sample
// for sample twice (a stretcher's output depends on where it started), so where it stretches, what
// it's held to is its loudness over time.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/WavIO.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;
namespace te = tracktion;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.141592653589793;

struct Warper
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-warp-spike", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-warp-" + juce::Uuid().toString());

    Warper()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        REQUIRE(root.createDirectory());
    }

    ~Warper()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i = 0)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    std::uint64_t place(const AudioBuffer& audio, const std::string& name, double seconds = 0.0,
                        size_t onTrack = 0)
    {
        const auto file = root.getChildFile(name + ".wav");
        if (!file.existsAsFile())
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), audio, kRate));
        const auto id = session->insertAudioFile(track(onTrack), file, seconds);
        REQUIRE(id != 0);
        return id;
    }

    te::AudioClipBase& clip(std::uint64_t id)
    {
        auto* c = dynamic_cast<te::AudioClipBase*>(session->impl().clip(id));
        REQUIRE(c != nullptr);
        return *c;
    }

    /// Warp on, the file's tempo `bpm`, as the design has it (§15): its LoopInfo first, then the
    /// stretch mode, on the real-time path. No stretcher (disabled): resampled, speed and pitch
    /// together (the patch's "resample").
    void warp(std::uint64_t id, double bpm,
              te::TimeStretcher::Mode mode = te::TimeStretcher::signalsmithDefault)
    {
        auto& c = clip(id);
        c.state.setProperty("resample", mode == te::TimeStretcher::disabled, nullptr);
        auto info = c.getLoopInfo();
        info.setNumBeats(c.getSourceLength().inSeconds() * bpm / 60.0);
        info.setBpm(bpm, c.getWaveInfo());
        info.setNumerator(4);
        info.setDenominator(4);
        c.setLoopInfo(info);
        c.setUsesProxy(false);
        c.setTimeStretchMode(mode);
        c.setAutoTempo(true);
    }

    AudioBuffer render(double start, double end, double rate = kRate)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, rate}, out.getFile(),
                                   [&](const std::string& e)
                                   {
                                       error = e;
                                       done = true;
                                   });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 2000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }
};

/// Noise, the same each time: every sample different, so any change shows.
AudioBuffer noise(double seconds, float level = 0.5f)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    std::uint32_t x = 22222;
    for (int i = 0; i < b.numSamples(); ++i)
        for (int c = 0; c < 2; ++c)
        {
            x = x * 1664525u + 1013904223u;
            b.at(c, i) = level * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
        }
    return b;
}

/// A sine, so a change of speed or pitch shows as a change of frequency.
AudioBuffer sine(double seconds, double hz, float level = 0.5f)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (int i = 0; i < b.numSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            b.at(c, i) = level * static_cast<float>(std::sin(2.0 * kPi * hz * i / kRate));
    return b;
}

/// A click every `every` seconds: a short burst, its start easy to find.
AudioBuffer clicks(double seconds, double every)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (double t = 0.0; t < seconds - 0.01; t += every)
        for (int i = 0; i < 48; ++i)
            for (int c = 0; c < 2; ++c)
                b.at(c, static_cast<int>(t * kRate) + i) =
                    0.8f * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * i / kRate));
    return b;
}

/// The largest difference between two renders, over [from, to) frames.
double difference(const AudioBuffer& a, const AudioBuffer& b, int from = 0, int to = -1)
{
    if (to < 0)
        to = std::min(a.numSamples(), b.numSamples());
    double out = 0.0;
    for (int c = 0; c < std::min(a.numChannels(), b.numChannels()); ++c)
        for (int i = from; i < to; ++i)
            out = std::max(out, std::abs(static_cast<double>(a.at(c, i)) - b.at(c, i)));
    return out;
}

double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

int frame(double seconds)
{
    return static_cast<int>(seconds * kRate);
}

/// How far apart two renders' loudness is over time: the largest difference, in dB, between their
/// levels in 20 ms windows (where either is above -60 dB).
double envelopeDifference(const AudioBuffer& a, const AudioBuffer& b)
{
    const int to = std::min(a.numSamples(), b.numSamples()), window = frame(0.02);
    double out = 0.0;
    for (int w0 = 0; w0 + window <= to; w0 += window)
    {
        double ea = 0.0, eb = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = w0; i < w0 + window; ++i)
            {
                ea += static_cast<double>(a.at(c, i)) * a.at(c, i);
                eb += static_cast<double>(b.at(c, i)) * b.at(c, i);
            }
        const double la = 10.0 * std::log10(ea / (2 * window) + 1e-12),
                     lb = 10.0 * std::log10(eb / (2 * window) + 1e-12);
        if (std::max(la, lb) > -60.0)
            out = std::max(out, std::abs(la - lb));
    }
    return out;
}

/// The frequency of a render's left channel over [from, to) seconds, by its rising zero crossings.
double frequency(const AudioBuffer& b, double from, double to)
{
    const int a = frame(from), e = std::min(b.numSamples(), frame(to));
    int first = -1, last = -1, count = 0;
    for (int i = a + 1; i < e; ++i)
        if (b.at(0, i - 1) < 0.0f && b.at(0, i) >= 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    return count > 1 ? (count - 1) * kRate / (last - first) : 0.0;
}

/// Where the sound starts and ends in a render (above -60 dB), in seconds.
std::pair<double, double> sounding(const AudioBuffer& b)
{
    int first = -1, last = -1;
    for (int i = 0; i < b.numSamples(); ++i)
        if (std::abs(b.at(0, i)) > 0.001f)
        {
            if (first < 0)
                first = i;
            last = i;
        }
    return {first / kRate, last / kRate};
}

/// Where each click starts in a render, in seconds.
std::vector<double> clickTimes(const AudioBuffer& b)
{
    std::vector<double> out;
    int quiet = 1 << 30;
    for (int i = 0; i < b.numSamples(); ++i)
    {
        if (std::abs(b.at(0, i)) > 0.05f && quiet > frame(0.05))
            out.push_back(i / kRate);
        quiet = std::abs(b.at(0, i)) > 0.05f ? 0 : quiet + 1;
    }
    return out;
}
} // namespace

TEST_CASE("warp spike: a tempo change keeps clips on their beats; a warped clip keeps its beats long, its "
          "pitch kept",
          "[integration][warp-spike]")
{
    Warper w;
    const auto id = w.place(sine(2.0, 440.0), "Sine", 1.0); // beats 2 to 6 at 120 BPM
    w.warp(id, 120.0);
    const auto midi = w.session->createMidiClip(w.track(1), 1.0, 2.0);
    const auto unwarped = w.place(sine(2.0, 440.0), "Plain", 1.0, 2);
    w.session->setTempo(100.0);
    w.session->updateSnapshotNow();
    CHECK(w.clip(id).getPosition().getStart().inSeconds() == Approx(1.2));
    CHECK(w.clip(id).getPosition().getLength().inSeconds() == Approx(2.4));
    CHECK(w.session->impl().clip(midi)->getPosition().getStart().inSeconds() == Approx(1.2));
    CHECK(w.clip(unwarped).getPosition().getStart().inSeconds() == Approx(1.2));  // on its beat
    CHECK(w.clip(unwarped).getPosition().getLength().inSeconds() == Approx(2.0)); // at its file's speed
    w.session->setClipsMuted({unwarped}, true);
    const auto out = w.render(0.0, 5.0);
    const auto [on, off] = sounding(out);
    CHECK(on == Approx(1.2).margin(0.001));
    CHECK(off == Approx(3.6).margin(0.001));
    CHECK(frequency(out, on + 0.2, off - 0.2) == Approx(440.0).margin(1.0));
}

TEST_CASE("warp spike 1: a warped clip at its own tempo plays its file: stretched, to float rounding; "
          "resampled, exactly",
          "[integration][warp-spike]")
{
    Warper w;
    const auto id = w.place(noise(2.0), "Noise", 1.0); // four beats at the song's 120 BPM
    const auto plain = w.render(0.0, 4.0);
    for (const auto mode : {te::TimeStretcher::signalsmithDefault, te::TimeStretcher::signalsmithCheaper})
    {
        w.warp(id, 120.0, mode);
        INFO(te::TimeStretcher::getNameOfMode(mode));
        CHECK(db(difference(plain, w.render(0.0, 4.0))) < -115.0); // measured -126 dB
    }
    w.warp(id, 120.0, te::TimeStretcher::disabled);
    CHECK(db(difference(plain, w.render(0.0, 4.0))) < -200.0);
    // From another tempo it plays another speed: the warp reaches the engine.
    w.warp(id, 100.0);
    CHECK(db(difference(plain, w.render(0.0, 4.0))) > -20.0);
}

TEST_CASE("warp spike 2: fades, edge fades and crossfades play on warped clips as on unwarped ones",
          "[integration][warp-spike]")
{
    Warper w;
    // Each with spare audio past the join, for the crossfade to play.
    const auto a = w.place(noise(3.0), "A", 0.0);
    const auto b = w.place(noise(3.0, 0.3f), "B", 3.0);
    w.session->applyClipEdits({{.clip = a, .track = w.track(), .start = 0.0, .length = 2.0, .offset = 0.0},
                               {.clip = b, .track = w.track(), .start = 2.0, .length = 2.0, .offset = 0.5}});
    w.session->setFades({a}, fd::model::Fade{0.25, 0.0f}, std::nullopt);
    REQUIRE(w.session->setCrossfade(a, fd::model::Crossfade{.length = 0.2, .from = -0.1}));
    const auto plain = w.render(0.0, 4.0);
    w.warp(a, 120.0);
    w.warp(b, 120.0);
    const auto warped = w.render(0.0, 4.0);
    CHECK(db(difference(plain, warped, 0, frame(0.3))) < -115.0);                // the fade-in
    CHECK(db(difference(plain, warped, frame(1.85), frame(2.15))) < -115.0);     // the crossfade (the patch)
    CHECK(db(difference(plain, warped, frame(2.2), frame(4.0) - 400)) < -115.0); // and on, to the edge fade
}

TEST_CASE("warp spike 3: a looped clip keeps its loop when it warps, in beats, lap after lap",
          "[integration][warp-spike]")
{
    Warper w;
    const auto id = w.place(noise(2.0), "Loop", 0.0);
    REQUIRE(w.session->setClipLoop(id, {0.5, 0.5}).empty()); // a beat from a beat in, at 120 BPM
    w.session->applyClipEdits({{.clip = id, .track = w.track(), .start = 0.0, .length = 4.0, .offset = 0.0}});
    const auto plain = w.render(0.0, 4.0);
    w.warp(id, 120.0);
    w.session->updateSnapshotNow();
    REQUIRE(w.clip(id).isLooping());
    CHECK(w.clip(id).getLoopStartBeats().inBeats() == Approx(1.0));
    CHECK(w.clip(id).getLoopLengthBeats().inBeats() == Approx(1.0));
    // Its seams fade as an unwarped loop's do (the loop read in beats, engine/Session.cpp).
    CHECK(db(difference(plain, w.render(0.0, 4.0))) < -115.0); // measured -122 dB
    // At 100 BPM each lap is the same audio, 0.6 s apart.
    w.session->setTempo(100.0);
    w.session->updateSnapshotNow();
    CHECK(w.clip(id).getPosition().getLength().inSeconds() == Approx(4.8));
    const auto slow = w.render(0.0, 5.0);
    const int lap = frame(0.6);
    double laps = 0.0;
    for (int i = lap; i < 4 * lap; ++i)
        laps = std::max(laps, std::abs(static_cast<double>(slow.at(0, i + lap)) - slow.at(0, i)));
    CHECK(db(laps) < -200.0);
}

TEST_CASE("warp spike 4: a split warped clip plays as the whole did", "[integration][warp-spike]")
{
    for (const double song : {120.0, 100.0}) // at its own tempo, then stretched
    {
        INFO(song << " BPM");
        Warper w;
        w.session->setTempo(song);
        const auto id = w.place(noise(2.0), "Whole", 0.0);
        w.warp(id, 120.0);
        w.session->updateSnapshotNow();
        const auto whole = w.render(0.0, 3.0);
        REQUIRE(w.session->splitClips({id}, 1.0).size() == 1);
        w.session->updateSnapshotNow();
        const auto split = w.render(0.0, 3.0);
        CHECK(db(difference(whole, split, 0, frame(0.95))) < -200.0);
        if (song == 120.0)
            CHECK(db(difference(whole, split)) < -115.0); // measured -124 dB
        else
            CHECK(envelopeDifference(whole, split) < 1.5); // measured 0.67 dB
    }
}

TEST_CASE("warp spike 5: a bounce of a warped clip matches its render, sample for sample",
          "[integration][warp-spike]")
{
    for (const double song : {0.0, 120.0, 100.0}) // 0: not warped
    {
        INFO(song << " BPM");
        Warper w;
        w.session->setTempo(song > 0.0 ? song : 120.0);
        const auto id = w.place(noise(2.0), "Bounced", 0.0);
        if (song > 0.0)
            w.warp(id, 120.0);
        w.session->updateSnapshotNow();
        const double rate = w.session->sampleRate(); // a bounce renders at the device's rate
        const auto rendered = w.render(0.0, 3.0, rate);
        std::optional<Session::BounceResult> result;
        auto job = w.session->bounce({id}, fd::model::BounceMode::InPlace,
                                     [&](const Session::BounceResult& r) { result = r; });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 2000 && !result; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(result);
        w.session->updateSnapshotNow();
        CHECK(db(difference(rendered, w.render(0.0, 3.0, rate))) < -200.0);
    }
}

TEST_CASE("warp spike 7: transposed without Warp, the same length; Repitch follows the tempo, resampled",
          "[integration][warp-spike]")
{
    Warper w;
    const auto id = w.place(sine(2.0, 440.0), "Tone", 0.0);
    auto& c = w.clip(id);
    c.setUsesProxy(false);
    c.setPitchChange(12.0f);
    auto out = w.render(0.0, 3.0);
    auto [on, off] = sounding(out);
    CHECK(frequency(out, 0.2, 1.8) == Approx(880.0).margin(2.0));
    CHECK(off == Approx(2.0).margin(0.002));
    // Repitch: a 120 BPM file at 100 BPM plays slower and lower, 2.4 s long.
    c.setPitchChange(0.0f);
    w.warp(id, 120.0, te::TimeStretcher::disabled);
    w.session->setTempo(100.0);
    w.session->updateSnapshotNow();
    out = w.render(0.0, 3.0);
    std::tie(on, off) = sounding(out);
    CHECK(frequency(out, 0.2, 2.2) == Approx(440.0 * 100.0 / 120.0).margin(1.0));
    CHECK(off == Approx(2.4).margin(0.002));
}

TEST_CASE("warp spike 6: 32 warped stereo clips in Tones, rendered", "[integration][warp-spike]")
{
    Warper w;
    while (w.session->snapshot()->tracks.size() < 32)
    {
        w.session->addTrack();
        w.session->updateSnapshotNow();
    }
    w.session->setTempo(100.0);
    for (size_t t = 0; t < 32; ++t)
        w.warp(w.place(noise(8.0), "Busy", 0.0, t), 120.0);
    w.session->updateSnapshotNow();
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = w.render(0.0, 9.6);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "warp spike 6: 9.6 s of 32 warped clips rendered in " << seconds << " s (" << 9.6 / seconds
              << "x real time, this build)" << std::endl;
    CHECK(out.numSamples() == frame(9.6));
}

// For the record (audio-editor.md §22): tracktion's warp markers (WarpReader) set the stretcher's
// speed by the position it plays, so a piece's change of speed is heard a stretcher's latency late;
// without a stretcher they don't play. Segno plays markers as pieces instead. Hidden: it reports.
TEST_CASE("warp spike: tracktion's warp markers, where a marker puts the audio", "[.][warp-spike]")
{
    Warper w;
    const auto id = w.place(clicks(2.0, 0.25), "Clicks", 0.0);
    w.warp(id, 120.0);
    auto& c = w.clip(id);
    c.setWarpTime(true);
    c.getWarpTimeManager().insertMarker(
        te::WarpMarker(te::TimePosition::fromSeconds(1.0), te::TimePosition::fromSeconds(0.8)));
    const auto out = w.render(0.0, 3.0);
    std::cout << "tracktion's warp markers, stretched: clicks at";
    for (double t : clickTimes(out))
        std::cout << " " << std::round(t * 1000.0) / 1000.0;
    std::cout << " (on the marker's map: 0 0.2 0.4 0.6 0.8 1.1 1.4 1.7)" << std::endl;
}

// Point 6's baseline: the same 32 clips, not warped (no stretcher), so the stretchers' own cost
// is the difference. Hidden: it reports.
TEST_CASE("warp spike 6: 32 unwarped stereo clips, rendered (the baseline)", "[.][warp-spike]")
{
    Warper w;
    while (w.session->snapshot()->tracks.size() < 32)
    {
        w.session->addTrack();
        w.session->updateSnapshotNow();
    }
    for (size_t t = 0; t < 32; ++t)
        w.place(noise(8.0), "Busy", 0.0, t);
    w.session->updateSnapshotNow();
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = w.render(0.0, 9.6);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "warp spike 6, baseline: 9.6 s of 32 unwarped clips rendered in " << seconds << " s" << std::endl;
    CHECK(out.numSamples() == frame(9.6));
}
