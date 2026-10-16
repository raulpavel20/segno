// Segno's piece player (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7, §8.1, §22): warp
// markers put the audio where they pin it, in each mode; Beats keeps every hit as it is; a clip at
// its own tempo reads its file exactly, markers or not; transposition keeps the speed; Warp on and
// off, and a new tempo, keep the audio a clip plays.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/Metrics.h"
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

struct Player
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-warp-player", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-warp-player-" + juce::Uuid().toString());
    std::vector<double> transients; // what Beats is told the files' transients are

    Player()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        REQUIRE(root.createDirectory());
        fd::engine::setTransientSource(engine, [this](te::AudioClipBase&) { return transients; });
    }

    ~Player()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i = 0)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    std::uint64_t place(const AudioBuffer& audio, const std::string& name, double seconds = 0.0)
    {
        const auto file = root.getChildFile(name + ".wav");
        if (!file.existsAsFile())
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), audio, kRate));
        const auto id = session->insertAudioFile(track(), file, seconds);
        REQUIRE(id != 0);
        return id;
    }

    fd::model::ClipInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        for (const auto& t : session->snapshot()->tracks)
            for (const auto& c : t.clips)
                if (c.id == id)
                    return c;
        FAIL("no clip " << id);
        return {};
    }

    AudioBuffer render(double start, double end)
    {
        session->updateSnapshotNow();
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, kRate}, out.getFile(),
                                   [&](const std::string& e)
                                   {
                                       error = e;
                                       done = true;
                                   });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 12000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }
};

int frame(double seconds)
{
    return static_cast<int>(std::lround(seconds * kRate));
}

double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

/// A click every `every` seconds: a millisecond of 1 kHz, its start easy to find.
AudioBuffer clicks(double seconds, double every)
{
    AudioBuffer b(2, frame(seconds));
    for (int k = 0; k * every < seconds - 0.01; ++k)
        for (int i = 0; i < 48; ++i)
            for (int c = 0; c < 2; ++c)
                b.at(c, frame(k * every) + i) =
                    0.8f * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * i / kRate));
    return b;
}

AudioBuffer sine(double seconds, double hz)
{
    AudioBuffer b(2, frame(seconds));
    for (int i = 0; i < b.numSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            b.at(c, i) = 0.5f * static_cast<float>(std::sin(2.0 * kPi * hz * i / kRate));
    return b;
}

AudioBuffer noise(double seconds)
{
    AudioBuffer b(2, frame(seconds));
    std::uint32_t x = 777;
    for (int i = 0; i < b.numSamples(); ++i)
        for (int c = 0; c < 2; ++c)
        {
            x = x * 1664525u + 1013904223u;
            b.at(c, i) = 0.5f * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
        }
    return b;
}

double difference(const AudioBuffer& a, const AudioBuffer& b, int from = 0, int to = -1)
{
    if (to < 0)
        to = std::min(a.numSamples(), b.numSamples());
    double out = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < to; ++i)
            out = std::max(out, std::abs(static_cast<double>(a.at(c, i)) - b.at(c, i)));
    return out;
}

/// Where each click is loudest in a render, in seconds: the peak of each burst (a stretcher
/// smears a click's onset, never where its energy is).
std::vector<double> clickPeaks(const AudioBuffer& b)
{
    std::vector<double> out;
    const int window = frame(0.03);
    for (int i = 0; i < b.numSamples();)
    {
        if (std::abs(b.at(0, i)) < 0.2f)
        {
            ++i;
            continue;
        }
        // The loudest 1 ms of the burst that starts here.
        int best = i;
        double bestEnergy = 0.0;
        for (int j = i; j < std::min(b.numSamples() - 48, i + window); ++j)
        {
            double e = 0.0;
            for (int k = 0; k < 48; ++k)
                e += static_cast<double>(b.at(0, j + k)) * b.at(0, j + k);
            if (e > bestEnergy)
            {
                bestEnergy = e;
                best = j;
            }
        }
        out.push_back(best / kRate);
        i += frame(0.1);
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

/// Where the sound ends in a render (above -60 dB), in seconds.
double soundsUntil(const AudioBuffer& b)
{
    int last = 0;
    for (int i = 0; i < b.numSamples(); ++i)
        if (std::abs(b.at(0, i)) > 0.001f)
            last = i;
    return last / kRate;
}

/// Clicks every 0.25 s of a 2 s file at 120 BPM, with 1.0 s of it pinned to beat 1.6 (0.8 s): the
/// first four clicks come every 0.2 s, the rest every 0.25 s after the marker.
const std::vector<double> kPinnedClicks{0.0, 0.2, 0.4, 0.6, 0.8, 1.05, 1.3, 1.55};
} // namespace

TEST_CASE("piece player: a marker puts the audio where it pins it, in each mode", "[integration][warp]")
{
    for (const auto mode :
         {fd::model::WarpMode::Repitch, fd::model::WarpMode::Tones, fd::model::WarpMode::Beats})
    {
        INFO(fd::model::warpModeName(mode));
        Player p;
        for (int k = 0; k < 8; ++k)
            p.transients.push_back(k * 0.25);
        const auto id = p.place(clicks(2.0, 0.25), "Clicks");
        REQUIRE(p.session->setWarp({id}, {.on = true, .mode = mode, .tempo = 120.0}));
        REQUIRE(p.session->setWarpMarkers(id, {{1.0, 1.6}}));
        CHECK(p.info(id).warp.markers.size() == 1);
        const auto out = p.render(0.0, 2.5);
        const auto peaks = clickPeaks(out);
        REQUIRE(peaks.size() == kPinnedClicks.size());
        for (size_t k = 0; k < peaks.size(); ++k)
        {
            INFO("click " << k);
            // Each click's loudest millisecond starts where the click does, +0.5 ms of its rise.
            CHECK(peaks[k] == Approx(kPinnedClicks[k]).margin(0.001));
        }
    }
}

TEST_CASE("piece player: Beats slowed keeps every hit sample for sample", "[integration][warp]")
{
    Player p;
    for (int k = 0; k < 8; ++k)
        p.transients.push_back(k * 0.25);
    const auto file = clicks(2.0, 0.25);
    const auto id = p.place(file, "Hits");
    p.session->setTempo(100.0); // each hit 0.3 s apart now
    REQUIRE(p.session->setWarp({id}, {.on = true, .mode = fd::model::WarpMode::Beats, .tempo = 120.0}));
    const auto out = p.render(0.0, 3.0);
    for (int k = 0; k < 8; ++k)
    {
        INFO("hit " << k);
        double worst = 0.0;
        for (int i = 0; i < 48; ++i)
            worst = std::max(worst, std::abs(static_cast<double>(out.at(0, frame(0.3 * k) + i)) -
                                             file.at(0, frame(0.25 * k) + i)));
        CHECK(db(worst) < -120.0);
    }
    CHECK(soundsUntil(out) == Approx(0.3 * 7 + 0.001).margin(0.0005)); // the last hit's millisecond
}

TEST_CASE("piece player: at its own tempo a clip reads its file exactly, with markers on its own map",
          "[integration][warp]")
{
    Player p;
    const auto file = noise(2.0);
    const auto id = p.place(file, "Noise", 0.5);
    const auto plain = p.render(0.0, 3.0);
    for (const auto mode : {fd::model::WarpMode::Tones, fd::model::WarpMode::Repitch})
    {
        INFO(fd::model::warpModeName(mode));
        REQUIRE(p.session->setWarp({id}, {.on = true, .mode = mode, .tempo = 120.0}));
        REQUIRE(p.session->setWarpMarkers(id, {{0.5, 1.0}, {1.25, 2.5}}));
        CHECK(db(difference(plain, p.render(0.0, 3.0))) < -200.0);
    }
}

TEST_CASE("piece player: a warped clip muted is silent, and plays again unmuted", "[integration][warp]")
{
    Player p;
    const auto id = p.place(noise(2.0), "Noise");
    REQUIRE(p.session->setWarp({id}, {.on = true, .mode = fd::model::WarpMode::Tones, .tempo = 120.0}));
    const auto plain = p.render(0.0, 2.0);
    REQUIRE(fd::testkit::peak(plain) > 0.1);
    p.session->setClipsMuted({id}, true);
    CHECK(fd::testkit::peak(p.render(0.0, 2.0)) == 0.0);
    p.session->setClipsMuted({id}, false);
    CHECK(db(difference(plain, p.render(0.0, 2.0))) < -200.0);
}

TEST_CASE("piece player: transposed, Tones keeps the length and moves the pitch", "[integration][warp]")
{
    Player p;
    const auto id = p.place(sine(2.0, 440.0), "Tone");
    p.session->setTempo(100.0);
    REQUIRE(p.session->setWarp({id}, {.on = true, .tempo = 120.0, .transpose = 12}));
    CHECK(p.info(id).pitch.transpose == 12);
    auto out = p.render(0.0, 3.0);
    CHECK(frequency(out, 0.3, 2.1) == Approx(880.0).margin(2.0));
    CHECK(soundsUntil(out) == Approx(2.4).margin(0.01));
    // Formants change how it sounds, not its pitch or length.
    REQUIRE(p.session->setWarp({id}, {.formants = true}));
    const auto kept = p.render(0.0, 3.0);
    CHECK(frequency(kept, 0.3, 2.1) == Approx(880.0).margin(4.0));
    CHECK(db(difference(out, kept)) > -60.0);
    // Without Warp, the engine transposes it where it is.
    REQUIRE(p.session->setWarp({id}, {.on = false}));
    out = p.render(0.0, 3.0);
    CHECK(frequency(out, 0.3, 1.8) == Approx(880.0).margin(2.0));
    CHECK(soundsUntil(out) == Approx(2.0).margin(0.01));
}

TEST_CASE("piece player: Follow key plays a clip in the song's key by the smallest step, warped or not, and "
          "moves with the song's key in its step (P6c)",
          "[integration][warp][follow-key]")
{
    using fd::music::Key;
    Player p;
    // A held A whose name says A major (its sound alone has no clear key).
    const auto id = p.place(sine(2.0, 440.0), "Tone_Amaj");
    CHECK(p.session->foundKey(id) == Key{9, "major"});
    REQUIRE(p.session->setKey(Key{7, "major"})); // G major
    REQUIRE(p.session->setWarp({id}, {.followKey = true}));
    auto info = p.info(id);
    CHECK(info.followKey);
    CHECK(info.key == Key{9, "major"});
    CHECK(info.keyFound);
    CHECK(info.followSteps == -2);
    CHECK(frequency(p.render(0.0, 2.0), 0.3, 1.8) == Approx(392.0).margin(2.0));
    // Warped, the piece player plays it there too.
    REQUIRE(p.session->setWarp({id}, {.on = true, .tempo = 120.0}));
    CHECK(frequency(p.render(0.0, 2.0), 0.3, 1.8) == Approx(392.0).margin(2.0));
    // The song's key changes: it moves with it; undone, back, in one step.
    REQUIRE(p.session->setKey(Key{10, "major"})); // B♭ major
    CHECK(p.info(id).followSteps == 1);
    CHECK(frequency(p.render(0.0, 2.0), 0.3, 1.8) == Approx(466.16).margin(2.5));
    p.session->undo();
    CHECK(p.info(id).followSteps == -2);
    CHECK(frequency(p.render(0.0, 2.0), 0.3, 1.8) == Approx(392.0).margin(2.0));
    // E minor has G major's notes: the same step.
    REQUIRE(p.session->setKey(Key{4, "minor"}));
    CHECK(p.info(id).followSteps == -2);
    // Its Transpose adds to the step.
    REQUIRE(p.session->setWarp({id}, {.transpose = 12}));
    CHECK(frequency(p.render(0.0, 2.0), 0.3, 1.8) == Approx(784.0).margin(3.0));
    // A key set by hand: D major moves up to G major's notes, not down.
    REQUIRE(p.session->setWarp({id}, {.key = std::optional(Key{2, "major"})}));
    info = p.info(id);
    CHECK(info.key == Key{2, "major"});
    CHECK_FALSE(info.keyFound);
    CHECK(info.followSteps == 5);
    // Repitch sets its pitch by its speed: no step.
    REQUIRE(p.session->setWarp({id}, {.mode = fd::model::WarpMode::Repitch}));
    CHECK(p.info(id).followSteps == 0);
    REQUIRE(p.session->setWarp({id}, {.mode = fd::model::WarpMode::Tones}));
    // Back to what's found, still following; then not following: as itself (with its Transpose).
    REQUIRE(p.session->setWarp({id}, {.keyFound = true}));
    info = p.info(id);
    CHECK(info.key == Key{9, "major"});
    CHECK(info.keyFound);
    CHECK(info.followSteps == -2);
    REQUIRE(p.session->setWarp({id}, {.followKey = false}));
    info = p.info(id);
    CHECK_FALSE(info.followKey);
    CHECK(info.followSteps == 0);
    CHECK(frequency(p.render(0.0, 2.0), 0.3, 1.8) == Approx(880.0).margin(3.0));
    // None set by hand: nothing to follow.
    REQUIRE(p.session->setWarp({id}, {.key = std::optional<Key>{}, .followKey = true}));
    info = p.info(id);
    CHECK_FALSE(info.key);
    CHECK_FALSE(info.followKey);
}

TEST_CASE(
    "slice: a clip cut at its transients, the grid or its bars plays on as before, in one undo step (P6c)",
    "[integration][warp][slice]")
{
    using fd::model::SliceAt;
    Player p;
    // A click every half second, 2 bars at 120 BPM: placed, it warps at its own tempo.
    const auto id = p.place(clicks(4.0, 0.5), "Clicks");
    const auto before = p.render(0.0, 4.5);
    const auto track = p.track();
    const auto clipsOnTrack = [&]
    {
        p.session->updateSnapshotNow();
        for (const auto& t : p.session->snapshot()->tracks)
            if (t.id == track)
                return t.clips;
        return std::vector<fd::model::ClipInfo>{};
    };
    std::string why;
    const auto parts = p.session->slice(id, {.at = SliceAt::Transients}, &why);
    INFO(why);
    REQUIRE(parts.size() == 8); // the first click is the clip's start
    CHECK(parts.front() == id);
    CHECK(p.session->undoDescription() == "Slice");
    const auto sliced = clipsOnTrack();
    REQUIRE(sliced.size() == 8);
    for (size_t i = 0; i < sliced.size(); ++i)
    {
        INFO("slice " << i);
        CHECK(sliced[i].start == Approx(0.5 * static_cast<double>(i)).margin(0.005));
        CHECK(sliced[i].fadeIn.length == 0.0);
    }
    // Seamless: no edge fades between them, so it sounds exactly as before.
    CHECK(db(difference(before, p.render(0.0, 4.5))) < -120.0);
    p.session->undo();
    CHECK(clipsOnTrack().size() == 1);

    // Every beat of the grid, and every bar.
    CHECK(p.session->slice(id, {.at = SliceAt::Grid, .gridBeats = 1.0}).size() == 8);
    p.session->undo();
    CHECK(p.session->slice(id, {.at = SliceAt::Bars}).size() == 2);
    CHECK(db(difference(before, p.render(0.0, 4.5))) < -120.0);
    p.session->undo();
    // Warp markers: none yet, said so.
    CHECK(p.session->slice(id, {.at = SliceAt::Markers}, &why).empty());
    CHECK(why.find("no warp markers") != std::string::npos);
    CHECK(clipsOnTrack().size() == 1);
}

TEST_CASE("piece player: Warp on and off, and a new tempo, keep the audio a clip plays",
          "[integration][warp]")
{
    Player p;
    p.session->setTempo(100.0);
    const auto id = p.place(noise(4.0), "Played", 1.0);
    p.session->applyClipEdits({{.clip = id, .track = p.track(), .start = 1.0, .length = 2.0, .offset = 0.5}});
    // Its file at 120 BPM: the 2 s from 0.5 s in become 2.4 s at 100 BPM.
    REQUIRE(p.session->setWarp({id}, {.on = true, .tempo = 120.0}));
    auto c = p.info(id);
    CHECK(c.warp.on);
    CHECK(c.start == Approx(1.0));
    CHECK(c.length == Approx(2.4));
    CHECK(fd::model::fileTimeAt(c, c.offset) == Approx(0.5));
    CHECK(c.sourceLength == Approx(4.8)); // its file, as it plays
    // ×2: the same audio, twice as long in beats.
    REQUIRE(p.session->setWarp({id}, {.tempoTimes = 2.0}));
    c = p.info(id);
    CHECK(c.warp.tempo == Approx(240.0));
    CHECK(c.length == Approx(4.8));
    CHECK(fd::model::fileTimeAt(c, c.offset) == Approx(0.5));
    // Off again: where it was, as it was.
    REQUIRE(p.session->setWarp({id}, {.on = false}));
    c = p.info(id);
    CHECK_FALSE(c.warp.on);
    CHECK(c.length == Approx(2.0));
    CHECK(c.offset == Approx(0.5));
    CHECK(c.warp.tempo == Approx(240.0)); // kept for when it's on again
    // A loop keeps its laps.
    REQUIRE(p.session->setClipLoop(id, {0.5, 1.0}).empty());
    REQUIRE(p.session->setWarp({id}, {.on = true, .tempo = 120.0}));
    c = p.info(id);
    REQUIRE(c.loop.on());
    CHECK(c.loop.length == Approx(1.2));
    CHECK(c.length == Approx(2.4));
    // Undone, it's as it was before Warp.
    p.session->undo();
    c = p.info(id);
    CHECK_FALSE(c.warp.on);
    CHECK(c.loop.length == Approx(1.0));
}

TEST_CASE("piece player: markers out of order or past the limits are refused", "[integration][warp]")
{
    Player p;
    const auto id = p.place(noise(2.0), "Refused");
    REQUIRE(p.session->setWarp({id}, {.on = true, .tempo = 120.0}));
    CHECK_FALSE(p.session->setWarpMarkers(id, {{1.0, 2.0}, {0.5, 3.0}}));
    CHECK_FALSE(p.session->setWarpMarkers(id, {{1.0, 9.0}})); // ×4.5
    CHECK(p.session->setWarpMarkers(id, {{1.0, 8.0}}));       // ×4
    CHECK(p.info(id).warp.markers == std::vector<fd::model::WarpMarker>{{1.0, 8.0}});
}

TEST_CASE("piece player: 32 warped stereo clips with markers, rendered", "[.][warp][benchmark]")
{
    Player p;
    while (p.session->snapshot()->tracks.size() < 32)
    {
        p.session->addTrack();
        p.session->updateSnapshotNow();
    }
    p.session->setTempo(100.0);
    for (size_t t = 0; t < 32; ++t)
    {
        const auto file = p.root.getChildFile("Busy.wav");
        if (!file.existsAsFile())
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), noise(8.0), kRate));
        p.session->updateSnapshotNow();
        const auto id = p.session->insertAudioFile(p.session->snapshot()->tracks.at(t).id, file, 0.0);
        REQUIRE(p.session->setWarp({id}, {.on = true, .tempo = 120.0}));
        std::vector<fd::model::WarpMarker> markers;
        for (int k = 1; k < 16; ++k)
            markers.push_back({0.5 * k, 1.0 * k + (k % 2 == 0 ? 0.0 : 0.1)});
        REQUIRE(p.session->setWarpMarkers(id, markers));
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = p.render(0.0, 9.6);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "piece player: 9.6 s of 32 warped clips, a marker every beat, rendered in " << seconds
              << " s" << std::endl;
    CHECK(out.numSamples() == frame(9.6));
}

namespace
{
/// A drum loop: a hit on every beat (a burst of noise decaying fast), `bars` bars at `bpm`.
AudioBuffer drumLoop(double bpm, int bars, int hits = -1)
{
    const double beat = 60.0 / bpm;
    AudioBuffer b(2, frame(bars * 4 * beat));
    std::uint32_t x = 99;
    const int count = hits < 0 ? bars * 4 : hits;
    for (int k = 0; k < count; ++k)
        for (int i = 0; i < frame(0.15) && frame(k * beat) + i < b.numSamples(); ++i)
        {
            x = x * 1664525u + 1013904223u;
            const float v = 0.7f * std::exp(-static_cast<float>(i) / 2000.0f) *
                            (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
            for (int c = 0; c < 2; ++c)
                b.at(c, frame(k * beat) + i) = v;
        }
    return b;
}
} // namespace

TEST_CASE(
    "piece player: what follows the tempo when it's placed: loops by their tempo, not one-shots, noise or "
    "long files; bounces at the song's",
    "[integration][warp]")
{
    Player p;
    // A drum loop: its tempo found in its sound, 2 bars at 92 BPM; percussive, so Beats.
    const auto loop = p.place(drumLoop(92.0, 2), "Break", 0.0);
    auto c = p.info(loop);
    CHECK(c.warp.on);
    CHECK(c.warp.tempo == Approx(92.0).margin(0.01));
    CHECK(c.warp.mode == fd::model::WarpMode::Beats);
    CHECK(c.length == Approx(8 * 0.5).margin(1e-6)); // 8 beats at the song's 120 BPM
    // A tempo in its name says so, whatever its sound.
    const auto named = p.place(noise(240.0 / 92.0), "Pad_92bpm", 10.0);
    CHECK(p.info(named).warp.on);
    CHECK(p.info(named).warp.tempo == Approx(92.0));
    // A one-shot (a single hit), noise with no beat, a long take: at their files' speed.
    CHECK_FALSE(p.info(p.place(drumLoop(120.0, 1, 1), "Hit", 20.0)).warp.on);
    CHECK_FALSE(p.info(p.place(noise(2.0), "Noise", 30.0)).warp.on);
    CHECK_FALSE(p.info(p.place(drumLoop(120.0, 20), "Take", 40.0)).warp.on); // 40 s
    // Bounced, it's made in time: Warp on at the song's tempo, so it plays untouched.
    std::optional<Session::BounceResult> result;
    auto job = p.session->bounce({loop}, fd::model::BounceMode::InPlace,
                                 [&](const Session::BounceResult& r) { result = r; });
    REQUIRE(job != nullptr);
    for (int i = 0; i < 2000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    REQUIRE(result->clips.size() == 1);
    c = p.info(result->clips[0]);
    CHECK(c.warp.on);
    CHECK(c.warp.tempo == Approx(120.0));
}
