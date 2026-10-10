// Clip fades and crossfades (M2 step C6, ADR 0036, docs/design/fades.md): clips play their spare
// audio through crossfades, fades follow the model's curves, edits stay click-free. Through
// engine::Session and offline renders.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/WavIO.h"
#include "model/Fades.h"

#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <random>

using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;

namespace
{
constexpr double kRate = 48000.0;

struct Fader
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-fade-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-fades-" + juce::Uuid().toString());

    Fader()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        REQUIRE(root.createDirectory());
    }

    ~Fader()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i = 0)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// Writes `audio` as a file named `name` and places it on a track at `seconds`.
    std::uint64_t place(std::uint64_t t, const AudioBuffer& audio, const std::string& name, double seconds = 0.0)
    {
        const auto file = root.getChildFile(name + ".wav");
        if (!file.existsAsFile())
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), audio, kRate));
        const auto id = session->insertAudioFile(t, file, seconds);
        REQUIRE(id != 0);
        return id;
    }

    AudioBuffer render(double start, double end)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, kRate}, out.getFile(), [&](const std::string& e) {
            error = e;
            done = true;
        });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 1000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }

    fd::model::ClipInfo clip(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto found = fd::model::findClip(*session->snapshot(), id);
        REQUIRE(found);
        return *found->first;
    }

    /// Moves or trims a clip on its track.
    void place(std::uint64_t clipId, double start, double length, double offset)
    {
        const auto c = clip(clipId);
        session->updateSnapshotNow();
        std::uint64_t t = 0;
        for (const auto& tr : session->snapshot()->tracks)
            for (const auto& x : tr.clips)
                if (x.id == clipId)
                    t = tr.id;
        session->applyClipEdits({{.clip = clipId, .track = t, .start = start, .length = length, .offset = offset}});
    }
};

AudioBuffer sine(double seconds, double hz = 441.0, float level = 0.5f)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            b.at(c, i) = level * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / kRate));
    return b;
}

AudioBuffer noise(double seconds, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            b.at(c, i) = d(rng);
    return b;
}

AudioBuffer constant(double seconds, float level)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            b.at(c, i) = level;
    return b;
}

double differenceDb(const AudioBuffer& a, const AudioBuffer& b)
{
    REQUIRE(a.numSamples() == b.numSamples());
    float worst = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < a.numSamples(); ++i)
            worst = std::max(worst, std::abs(a.at(c, i) - b.at(c, i)));
    return worst > 0.0f ? 20.0 * std::log10(worst) : -200.0;
}

double rmsDb(const AudioBuffer& a, double from, double to)
{
    double sum = 0.0;
    const int i0 = static_cast<int>(from * kRate), i1 = static_cast<int>(to * kRate);
    for (int i = i0; i < i1; ++i)
        sum += static_cast<double>(a.at(0, i)) * a.at(0, i);
    return 10.0 * std::log10(sum / (i1 - i0));
}
} // namespace

TEST_CASE("fades: a split plays on seamlessly; cut edges fade, so edits don't click", "[integration]")
{
    Fader f;
    const auto t = f.track();
    const auto c = f.place(t, sine(4.0), "sine");
    const auto whole = f.render(0.0, 4.0);
    CHECK_FALSE(f.clip(c).playback.any()); // a whole file: nothing cut

    // Split: the join is seamless, nothing fades there, and it nulls.
    const auto right = f.session->splitClips({c}, 1.7);
    REQUIRE(right.size() == 1);
    CHECK(f.clip(c).playback.out.kind == fd::model::Ramp::Kind::None);
    CHECK(differenceDb(f.render(0.0, 4.0), whole) < -120.0);

    // Pulled apart, mid-waveform: the cut edges fade 4 ms, and nothing clicks.
    f.place(right[0], 2.0, 2.0 - 0.3, 1.7 + 0.3 - 0.3);
    const auto r = f.clip(right[0]);
    CHECK(r.playback.in.edge);
    CHECK(f.clip(c).playback.out.edge);
    const auto faded = f.render(0.0, 4.0);
    CHECK(fd::testkit::clickScore(faded, 0) < 10.0);
    // With edge fades off, the same edit clicks: the test can tell.
    f.session->setEdgeFades(false);
    CHECK(f.session->undoDescription() == "Edge fades");
    const auto raw = f.render(0.0, 4.0);
    CHECK(fd::testkit::clickScore(raw, 0) > 30.0);
    f.session->undo();
    CHECK(f.session->edgeFades());
}

TEST_CASE("fades: a fade plays the model's curve, straight or bent; fades split with their clip", "[integration]")
{
    Fader f;
    const auto t = f.track();
    const auto c = f.place(t, constant(2.0, 0.5f), "dc");
    for (const float bend : {0.0f, 0.6f, -0.5f})
    {
        f.session->setFades({c}, fd::model::Fade{0.5, bend}, fd::model::Fade{0.25, -bend});
        CHECK(f.session->undoDescription() == "Fades");
        const auto out = f.render(0.0, 2.0);
        for (const double at : {0.05, 0.2, 0.35, 0.49})
            CHECK(out.at(0, static_cast<int>(at * kRate)) == Approx(0.5f * fd::model::fadeInGain(at / 0.5, bend)).margin(2e-3));
        for (const double at : {1.8, 1.9, 1.99})
            CHECK(out.at(0, static_cast<int>(at * kRate)) == Approx(0.5f * fd::model::fadeOutGain((at - 1.75) / 0.25, -bend)).margin(2e-3));
        CHECK(out.at(0, static_cast<int>(1.0 * kRate)) == Approx(0.5f)); // the middle untouched
    }
    const auto info = f.clip(c);
    CHECK(info.fadeIn.length == Approx(0.5));
    CHECK(info.fadeIn.bend == Approx(-0.5f));

    // A split: the left part keeps the fade-in, the right part the fade-out.
    const auto right = f.session->splitClips({c}, 1.0);
    REQUIRE(right.size() == 1);
    CHECK(f.clip(c).fadeIn.length == Approx(0.5));
    CHECK(f.clip(c).fadeOut.length == 0.0);
    CHECK(f.clip(right[0]).fadeIn.length == 0.0);
    CHECK(f.clip(right[0]).fadeOut.length == Approx(0.25));
}

TEST_CASE("fades: a crossfade plays each clip's spare audio past the join", "[integration]")
{
    Fader f;
    const auto t = f.track();
    // One recording, and the same audio in a second file: the reference, unbroken.
    const auto audio = sine(4.0, 330.0);
    const auto a = f.place(t, audio, "take-a");
    const auto whole = f.render(0.0, 4.0);
    // The first file up to 2 s, the copy from 2 s: they meet, but not seamlessly (two files).
    f.place(a, 0.0, 2.0, 0.0);
    const auto b = f.place(t, audio, "take-b", 2.0);
    f.place(b, 2.0, 2.0, 2.0);
    REQUIRE(f.clip(a).end() == Approx(2.0));
    const auto gap = f.render(0.0, 4.0);
    CHECK(differenceDb(gap, whole) > -40.0); // the edge fades dip at the join

    // Equal gain over the same material sums back to the original: it nulls, so both clips
    // played their spare audio at the right place.
    auto results = f.session->crossfade({a, b}, 0.02, fd::model::CrossfadeShape::EqualGain);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].crossfade);
    CHECK(results[0].crossfade->length == Approx(0.02));
    CHECK(results[0].crossfade->from == Approx(-0.01)); // centred
    CHECK(f.session->undoDescription() == "Crossfade");
    CHECK(f.clip(a).crossfadeOut);
    CHECK(f.clip(a).playback.extendEnd == Approx(0.01));
    CHECK(f.clip(b).playback.extendStart == Approx(0.01));
    CHECK(differenceDb(f.render(0.0, 4.0), whole) < -100.0);
    // Moved before the join, and after it: still null.
    REQUIRE(f.session->setCrossfade(a, fd::model::Crossfade{0.02, -0.02, fd::model::CrossfadeShape::EqualGain}));
    CHECK(differenceDb(f.render(0.0, 4.0), whole) < -100.0);
    REQUIRE(f.session->setCrossfade(a, fd::model::Crossfade{0.02, 0.0, fd::model::CrossfadeShape::EqualGain}));
    CHECK(differenceDb(f.render(0.0, 4.0), whole) < -100.0);

    // Undo takes a step back at a time; moving a clip away removes the crossfade.
    f.session->undo();
    CHECK(f.clip(a).crossfadeOut->from == Approx(-0.02));
    f.place(b, 2.5, 1.5, 2.5);
    CHECK_FALSE(f.clip(a).crossfadeOut);
    f.place(b, 2.0, 2.0, 2.0);
    CHECK_FALSE(f.clip(a).crossfadeOut); // gone for good, not waiting to come back
}

TEST_CASE("fades: equal power keeps the level across different material; no spare audio, no crossfade", "[integration]")
{
    Fader f;
    const auto t = f.track();
    const auto a = f.place(t, noise(4.0, 1), "noise-a");
    f.place(a, 0.0, 2.0, 0.0);
    const auto b = f.place(t, noise(4.0, 2), "noise-b", 2.0);
    f.place(b, 2.0, 2.0, 2.0);
    const auto results = f.session->crossfade({a, b}, 0.2);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].crossfade);
    CHECK(results[0].crossfade->shape == fd::model::CrossfadeShape::EqualPower);
    const auto out = f.render(0.0, 4.0);
    const double level = rmsDb(out, 1.0, 1.8);
    CHECK(rmsDb(out, 1.95, 2.05) == Approx(level).margin(0.5));
    CHECK(rmsDb(out, 2.2, 3.0) == Approx(level).margin(0.5));

    // Two whole files edge to edge: no spare audio, so no crossfade, and it says why.
    const auto u = f.track(1);
    const auto c = f.place(u, noise(1.0, 3), "noise-c");
    const auto d = f.place(u, noise(1.0, 4), "noise-d", 1.0);
    const auto refused = f.session->crossfade({c, d});
    REQUIRE(refused.size() == 1);
    CHECK_FALSE(refused[0].crossfade);
    CHECK(refused[0].why.find("trim them") != std::string::npos);
    CHECK_FALSE(f.session->setCrossfade(c, fd::model::Crossfade{0.01, -0.005}));
    // A split's join is seamless: nothing to crossfade there.
    const auto halves = f.session->splitClips({c}, 0.5);
    REQUIRE(halves.size() == 1);
    const auto seamless = f.session->crossfade({c, halves[0]});
    REQUIRE(seamless.size() == 1);
    CHECK_FALSE(seamless[0].crossfade);
}
