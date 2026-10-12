// Clip loops in the engine (M3 step C10, ADR 0048, docs/design/clip-loops.md): what a looped clip
// plays, sample for sample, through engine::Session and offline renders.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/WavIO.h"

#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;
namespace te = tracktion;

namespace
{
constexpr double kRate = 48000.0;

struct Looper
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-clip-loop-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-loops-" + juce::Uuid().toString());

    Looper()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        REQUIRE(root.createDirectory());
    }

    ~Looper()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i = 0)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    std::uint64_t place(std::uint64_t t, const AudioBuffer& audio, const std::string& name, double seconds = 0.0)
    {
        const auto file = root.getChildFile(name + ".wav");
        if (!file.existsAsFile())
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), audio, kRate));
        const auto id = session->insertAudioFile(t, file, seconds);
        REQUIRE(id != 0);
        return id;
    }

    AudioBuffer render(double start, double end, double rate = kRate)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, rate}, out.getFile(), [&](const std::string& e) {
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

    te::Clip& engineClip(std::uint64_t id)
    {
        auto* c = session->impl().clip(id);
        REQUIRE(c != nullptr);
        return *c;
    }
};

/// A ramp: every sample a different value, so a render shows exactly which sample played.
AudioBuffer ramp(double seconds)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.numSamples(); ++i)
            b.at(c, i) = 0.9f * static_cast<float>(i) / static_cast<float>(b.numSamples()) - 0.45f;
    return b;
}

double worstDifference(const AudioBuffer& a, const AudioBuffer& b)
{
    REQUIRE(a.numSamples() == b.numSamples());
    float worst = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < a.numSamples(); ++i)
            worst = std::max(worst, std::abs(a.at(c, i) - b.at(c, i)));
    return worst;
}

/// The largest difference outside 2 ms around each of `jumps` (seconds): where the ramp jumps
/// (a clip's edges, a loop's seams), two resamplers ring differently.
double worstDifferenceAway(const AudioBuffer& a, const AudioBuffer& b, double rate, std::initializer_list<double> jumps)
{
    REQUIRE(a.numSamples() == b.numSamples());
    float worst = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < a.numSamples(); ++i)
            if (std::none_of(jumps.begin(), jumps.end(), [&](double j) { return std::abs(i / rate - j) < 0.002; }))
                worst = std::max(worst, std::abs(a.at(c, i) - b.at(c, i)));
    return worst;
}

double rms(const AudioBuffer& a, double from, double to)
{
    double sum = 0.0;
    const int i0 = static_cast<int>(from * kRate), i1 = static_cast<int>(to * kRate);
    for (int i = i0; i < i1; ++i)
        sum += static_cast<double>(a.at(0, i)) * a.at(0, i);
    return std::sqrt(sum / (i1 - i0));
}
} // namespace

TEST_CASE("clip loops (engine): a looped audio clip plays its loop, sample for sample, on every lap", "[integration]")
{
    Looper f;
    f.session->setEdgeFades(false); // the samples exactly as the engine plays them
    const auto file = ramp(2.0);
    const auto t = f.track();
    const auto id = f.place(t, file, "ramp", 0.5);

    // The loop: 0.25 s from 0.25 s into the file, which goes on to 2 s (it ends inside the file).
    const int loopStart = 12000, loopLength = 24000, clipStart = 24000, clipLength = 84000; // 3.5 laps
    for (const int phase : {0, 6000})
    {
        INFO("phase " << phase);
        auto& clip = f.engineClip(id);
        auto* audio = dynamic_cast<te::AudioClipBase*>(&clip);
        REQUIRE(audio != nullptr);
        CHECK_FALSE(audio->getAutoTempo()); // a plain file: seconds, not beats
        clip.setLoopRange({te::TimePosition::fromSeconds(loopStart / kRate), te::TimeDuration::fromSeconds(loopLength / kRate)});
        clip.setPosition({{te::TimePosition::fromSeconds(clipStart / kRate), te::TimeDuration::fromSeconds(clipLength / kRate)},
                          te::TimeDuration::fromSeconds(phase / kRate)});
        REQUIRE(clip.isLooping());
        CHECK_FALSE(clip.beatBasedLooping());

        const auto out = f.render(0.0, 3.0);
        AudioBuffer expected(2, out.numSamples());
        for (int c = 0; c < 2; ++c)
            for (int n = clipStart; n < clipStart + clipLength; ++n)
                expected.at(c, n) = file.at(c, loopStart + (phase + n - clipStart) % loopLength);
        CHECK(worstDifference(out, expected) < 1e-6);
    }
}

TEST_CASE("clip loops (engine): a split looped clip plays on as the whole did; so does its bounce", "[integration]")
{
    Looper f;
    f.session->setEdgeFades(false);
    const auto t = f.track();
    const auto id = f.place(t, ramp(2.0), "ramp", 0.5);
    auto& clip = f.engineClip(id);
    clip.setLoopRange({te::TimePosition::fromSeconds(0.25), te::TimeDuration::fromSeconds(0.5)});
    clip.setPosition({{te::TimePosition::fromSeconds(0.5), te::TimeDuration::fromSeconds(1.75)}, te::TimeDuration::fromSeconds(0.1)});
    const auto whole = f.render(0.0, 3.0);
    // A bounce renders at the device's rate, so it's compared at that rate.
    const auto deviceRate = f.session->sampleRate();
    const auto wholeAtDevice = f.render(0.0, 3.0, deviceRate);

    // Split inside the second lap: the parts keep the loop and line up.
    const auto right = f.session->splitClips({id}, 1.37);
    REQUIRE(right.size() == 1);
    CHECK(f.engineClip(right[0]).isLooping());
    CHECK(worstDifference(f.render(0.0, 3.0), whole) < 1e-6);
    INFO("right part's offset " << f.engineClip(right[0]).getPosition().getOffset().inSeconds());

    // Bounced in place, the looped clips render as they played.
    f.session->updateSnapshotNow();
    std::optional<Session::BounceResult> result;
    auto job = f.session->bounce({id, right[0]}, fd::model::BounceMode::InPlace,
                                 [&](const Session::BounceResult& r) { result = r; });
    REQUIRE(job != nullptr);
    for (int i = 0; i < 2000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    CHECK(worstDifferenceAway(f.render(0.0, 3.0, deviceRate), wholeAtDevice, deviceRate, {0.5, 0.9, 1.4, 1.9, 2.25}) < 1e-4);
}

TEST_CASE("clip loops (engine): a looped MIDI clip plays the notes inside its loop on every lap", "[integration]")
{
    Looper f;
    const auto t = f.track();
    REQUIRE(f.session->addDevice(t, "fd:subtractive-synth") != 0);
    // 120 BPM: a beat is 0.5 s. Notes at beats 0, 1 and 3, an eighth of a beat each; the loop is
    // beats 1 to 3, so only the note at 1 is inside it.
    const auto id = f.session->createMidiClip(t, 0.0, 4.0);
    REQUIRE(id != 0);
    f.session->setNotes(id, {{.pitch = 60, .start = 0.0, .length = 0.125},
                             {.pitch = 60, .start = 1.0, .length = 0.125},
                             {.pitch = 60, .start = 3.0, .length = 0.125}});
    auto* midi = dynamic_cast<te::MidiClip*>(&f.engineClip(id));
    REQUIRE(midi != nullptr);
    midi->setLoopRangeBeats({te::BeatPosition::fromBeats(1.0), te::BeatDuration::fromBeats(2.0)});
    REQUIRE(midi->isLooping());

    for (const double phase : {0.0, 0.5}) // seconds: 0 and one beat into the loop
    {
        INFO("phase " << phase);
        midi->setPosition({{te::TimePosition::fromSeconds(0.0), te::TimeDuration::fromSeconds(4.0)}, te::TimeDuration::fromSeconds(phase)});
        const auto out = f.render(0.0, 4.5);
        // The note at the loop's start sounds at every lap: 2 beats (1 s) apart, from the lap
        // the clip's start is in. Between them, silence (the synth's release is 200 ms).
        const double first = phase == 0.0 ? 0.0 : 0.5;
        for (double at = first; at < 4.0; at += 1.0)
        {
            INFO("note at " << at);
            CHECK(rms(out, at + 0.01, at + 0.06) > 0.01);
            CHECK(rms(out, at + 0.4, at + 0.45) < 1e-4);
        }
        // The notes outside the loop never play: nothing before the first lap's note.
        if (phase > 0.0)
            CHECK(rms(out, 0.0, 0.45) < 1e-4);
    }
}

namespace
{
fd::model::ClipInfo infoOf(Looper& f, std::uint64_t clip)
{
    f.session->updateSnapshotNow();
    const auto found = fd::model::findClip(*f.session->snapshot(), clip);
    REQUIRE(found);
    return *found->first;
}

/// A WAV of `seconds` whose metadata says it's a loop of `beats` (an ACID chunk).
juce::File acidLoop(Looper& f, double seconds, int beats)
{
    const auto file = f.root.getChildFile("acid.wav");
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(kRate).withNumChannels(2).withBitsPerSample(24).withMetadataValues(
        {{juce::WavAudioFormat::acidBeats, juce::String(beats)},
         {juce::WavAudioFormat::acidNumerator, "4"},
         {juce::WavAudioFormat::acidDenominator, "4"},
         {juce::WavAudioFormat::acidTempo, "100"},
         {juce::WavAudioFormat::acidStretch, "1"}});
    auto writer = juce::WavAudioFormat().createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    juce::AudioBuffer<float> audio(2, static_cast<int>(seconds * kRate));
    audio.clear();
    REQUIRE(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()));
    return file;
}
} // namespace

TEST_CASE("clip loops: on moves nothing, off removes the repeats; a looped clip goes past its file; kept when saved",
          "[integration]")
{
    Looper f;
    f.session->setEdgeFades(false);
    const auto file = ramp(2.0);
    const auto t = f.track();
    const auto id = f.place(t, file, "ramp", 0.5);
    f.session->applyClipEdits({{.clip = id, .track = t, .start = 0.75, .length = 0.5, .offset = 0.25}});
    const auto before = f.render(0.0, 1.5);

    CHECK(f.session->setClipsLooping({id}, true).empty());
    CHECK(f.session->undoDescription() == "Loop clips");
    auto c = infoOf(f, id);
    CHECK(c.loop == fd::model::ClipLoop{0.25, 0.5});
    CHECK(c.offset == 0.0);
    CHECK(c.start == Approx(0.75));
    CHECK(c.length == Approx(0.5));
    CHECK(f.engineClip(id).isLooping());
    CHECK(worstDifference(f.render(0.0, 1.5), before) < 1e-6); // nothing sounds different

    // Extended far past its file's end (2.5 s): eight laps, each the loop.
    f.session->applyClipEdits({{.clip = id, .track = t, .start = 0.75, .length = 4.0, .offset = 0.0}});
    c = infoOf(f, id);
    CHECK(c.length == Approx(4.0));
    CHECK(c.loop.on());
    const auto out = f.render(0.0, 5.0);
    for (int lap = 0; lap < 8; ++lap)
    {
        const double at = 0.75 + 0.5 * lap + 0.1;
        CHECK(out.at(0, static_cast<int>(std::lround(at * kRate))) == Approx(file.at(0, static_cast<int>(std::lround(0.35 * kRate)))));
    }

    // Off: the repeats go; undo brings them back.
    CHECK(f.session->setClipsLooping({id}, false).empty());
    CHECK(f.session->undoDescription() == "Stop looping clips");
    c = infoOf(f, id);
    CHECK_FALSE(c.loop.on());
    CHECK(c.length == Approx(0.5));
    CHECK(c.offset == Approx(0.25));
    f.session->undo();
    c = infoOf(f, id);
    CHECK(c.loop.on());
    CHECK(c.length == Approx(4.0));

    // A new loop: the content at the clip's start is outside it, so the clip starts at its start.
    CHECK(f.session->setClipLoop(id, {0.5, 0.25}).empty());
    CHECK(f.session->undoDescription() == "Loop");
    c = infoOf(f, id);
    CHECK(c.loop == fd::model::ClipLoop{0.5, 0.25});
    CHECK(c.offset == 0.0);
    CHECK(c.length == Approx(4.0));

    // Saved and opened again: the same loop.
    const auto project = f.root.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(f.session->saveAs(project));
    REQUIRE(f.session->open(project));
    f.session->updateSnapshotNow();
    const auto& reopened = f.session->snapshot()->tracks.front().clips.front();
    CHECK(reopened.loop == fd::model::ClipLoop{0.5, 0.25});
    CHECK(reopened.length == Approx(4.0));
}

TEST_CASE("clip loops: MIDI loops in beats; clips that can't loop say why; audio is placed at its file's speed",
          "[integration]")
{
    Looper f;
    const auto t = f.track();

    // MIDI: one bar at 120 BPM, looped: four beats.
    const auto midi = f.session->createMidiClip(t, 0.0, 2.0);
    REQUIRE(midi != 0);
    CHECK(f.session->setClipsLooping({midi}, true).empty());
    CHECK(infoOf(f, midi).loop == fd::model::ClipLoop{0.0, 2.0});
    auto* engineMidi = dynamic_cast<te::MidiClip*>(&f.engineClip(midi));
    REQUIRE(engineMidi != nullptr);
    CHECK(engineMidi->getLoopLengthBeats().inBeats() == Approx(4.0));

    // A file with loop metadata is placed at its own speed, its own length.
    const auto acid = f.session->insertAudioFile(t, acidLoop(f, 3.0, 4), 4.0);
    REQUIRE(acid != 0);
    CHECK_FALSE(infoOf(f, acid).synced);
    CHECK(infoOf(f, acid).length == Approx(3.0));
    CHECK(f.session->setClipsLooping({acid}, true).empty());

    // An audio clip the engine stretches to the tempo (a project from before) can't loop.
    const auto synced = f.place(t, ramp(1.0), "synced", 10.0);
    dynamic_cast<te::AudioClipBase&>(f.engineClip(synced)).setAutoTempo(true);
    CHECK(infoOf(f, synced).synced);
    const auto why = f.session->setClipsLooping({synced}, true);
    CHECK(why.find("tempo") != std::string::npos);
    CHECK_FALSE(infoOf(f, synced).loop.on());

    // A clip with takes (a bounce keeps its source as one) can't loop.
    const auto plain = f.place(t, ramp(1.0), "plain", 14.0);
    std::optional<Session::BounceResult> result;
    REQUIRE(f.session->bounce({plain}, fd::model::BounceMode::InPlace, [&](const Session::BounceResult& r) { result = r; }) != nullptr);
    for (int i = 0; i < 2000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    REQUIRE(result->clips.size() == 1);
    CHECK(f.session->setClipsLooping({result->clips[0]}, true).find("takes") != std::string::npos);
    CHECK(f.session->setClipLoop(result->clips[0], {0.0, 0.5}).find("takes") != std::string::npos);
    CHECK_FALSE(infoOf(f, result->clips[0]).loop.on());
}
