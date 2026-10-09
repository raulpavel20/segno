// Recording (M2 step C5, ADRs 0030 and 0031, docs/design/recording.md): inputs, arming,
// monitoring, recording passes and project folders, through tracktion's hosted test device.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/SessionImpl.h"
#include "model/Editing.h"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using fd::engine::Session;
namespace te = tracktion;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480; // 10 ms

struct Recording
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-recording-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    std::unique_ptr<te::test_utilities::EnginePlayer> player;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-recording-" + juce::Uuid().toString());
    std::int64_t clock = 0; // samples played
    std::function<float(std::int64_t sample, int channel)> input = [](std::int64_t, int) { return 0.0f; };
    juce::AudioBuffer<float> lastOutput;

    Recording()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        te::HostedAudioDeviceInterface::Parameters io;
        io.sampleRate = kRate;
        io.blockSize = kBlock;
        io.inputChannels = 2;
        io.outputChannels = 2;
        player = std::make_unique<te::test_utilities::EnginePlayer>(engine, io);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        pump(0.1);
    }

    ~Recording()
    {
        session->stop();
        pump(0.05);
        root.deleteRecursively();
    }

    /// Plays `seconds` through the hosted device, feeding `input`.
    void pump(double seconds)
    {
        juce::AudioBuffer<float> in(2, kBlock);
        for (int b = 0; b < static_cast<int>(seconds * kRate / kBlock + 0.5); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < kBlock; ++s)
                    in.setSample(c, s, input(clock + s, c));
            clock += kBlock;
            lastOutput = player->process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// A copy: snapshots are replaced.
    fd::model::TrackInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }

    void recordFor(double seconds)
    {
        session->record();
        pump(seconds);
        session->stop();
        pump(0.1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    /// The output's level over the last block.
    float outputLevel() const { return lastOutput.getMagnitude(0, 0, lastOutput.getNumSamples()); }
};

float sine(std::int64_t sample, int)
{
    return 0.5f * std::sin(static_cast<float>(sample) * 0.05f);
}
} // namespace

TEST_CASE("recording: an armed track records its audio input into the project's Recordings, one undo step",
          "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    REQUIRE_FALSE(r.session->audioInputs().empty());
    const auto inputName = r.session->audioInputs().front().id;
    r.session->setAudioInput(t, inputName);
    CHECK(r.session->undoDescription() == "Track input");
    r.session->setArmed(t, true, true);
    CHECK(r.info(t).input.audio == inputName);
    CHECK(r.info(t).input.armed);
    std::vector<std::uint64_t> told;
    r.session->onRecorded = [&](const auto& clips) { told = clips; };

    r.recordFor(1.0);
    CHECK(r.session->undoDescription() == "Record");
    auto clips = r.info(t).clips;
    REQUIRE(clips.size() == 1);
    CHECK(told == std::vector{clips[0].id});
    CHECK(clips[0].name == r.info(t).name); // named after its track
    CHECK(clips[0].kind == fd::model::ClipInfo::Kind::Audio);
    CHECK(clips[0].length == Approx(1.0).margin(0.05));
    const juce::File recorded(clips[0].sourcePath);
    CHECK(recorded.existsAsFile());
    CHECK(recorded.getParentDirectory() == r.session->recordingsFolder());
    CHECK(r.session->recordingsFolder().isAChildOf(r.root.getChildFile("Unsaved")));

    // Undo takes the clip back, never the file.
    r.session->undo();
    CHECK(r.info(t).clips.empty());
    CHECK(recorded.existsAsFile());
    r.session->redo();
    REQUIRE(r.info(t).clips.size() == 1);

    // The first save moves the recordings into the project's folder, with relative paths, so the
    // folder can move as a whole (ADR 0030).
    const auto project = r.root.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(r.session->saveAs(project));
    CHECK_FALSE(recorded.existsAsFile());
    const auto moved = project.getParentDirectory().getChildFile("Recordings").getChildFile(recorded.getFileName());
    CHECK(moved.existsAsFile());
    CHECK(juce::File(r.info(t).clips.at(0).sourcePath) == moved);
    const auto relocated = r.root.getChildFile("Elsewhere");
    REQUIRE(project.getParentDirectory().moveFileTo(relocated));
    REQUIRE(r.session->open(relocated.getChildFile("Song.segno")));
    clips = r.info(t).clips;
    REQUIRE(clips.size() == 1);
    CHECK(juce::File(clips[0].sourcePath) == relocated.getChildFile("Recordings").getChildFile(recorded.getFileName()));
}

TEST_CASE("recording: the selected instrument track records MIDI, on its channel", "[integration]")
{
    Recording r;
    const auto t = r.track(1);
    REQUIRE(r.session->addDevice(t, "fd:subtractive-synth") != 0);
    r.session->setMidiInputTarget(t); // selecting it softly arms it (B14)
    CHECK(r.info(t).input.softArmed);
    CHECK_FALSE(r.info(t).input.armed);
    auto play = [&](int note) {
        r.session->computerKeyboardNote(note, 0.8f);
        r.pump(0.2);
        r.session->computerKeyboardNote(note, 0.0f);
        r.pump(0.1);
    };

    r.session->record();
    r.pump(0.2);
    play(60);
    play(64);
    r.session->stop();
    r.pump(0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    auto clips = r.info(t).clips;
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].kind == fd::model::ClipInfo::Kind::Midi);
    REQUIRE(clips[0].notes.size() == 2);
    CHECK(clips[0].notes[0].pitch == 60);
    CHECK(clips[0].notes[1].pitch == 64);

    // On channel 2 it neither records nor plays channel 1 (the computer keyboard's).
    r.session->setMidiChannel(t, 2);
    r.session->setPosition(4.0);
    r.session->record();
    r.pump(0.2);
    play(67);
    r.session->stop();
    r.pump(0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(r.info(t).clips.size() == 1);

    // An explicitly armed track takes the arm from the selection.
    r.session->setArmed(r.track(0), true, true);
    CHECK_FALSE(r.info(t).input.softArmed);
}

TEST_CASE("recording: arming is exclusive by click, adds with Ctrl, and is never an undo step; tracks record at once",
          "[integration]")
{
    Recording r;
    r.input = [](std::int64_t s, int c) { return c == 0 ? sine(s, c) : 0.25f * sine(s * 2, c); };
    const auto t0 = r.track(0), t1 = r.track(1), t2 = r.track(2);
    const auto inputs = r.session->audioInputs();
    REQUIRE(inputs.size() >= 2);
    r.session->setAudioInput(t0, inputs[0].id);
    r.session->setAudioInput(t1, inputs[1].id);
    r.pump(0.6); // tracktion's undo timer settles
    juce::TemporaryFile saved(Session::fileExtension);
    REQUIRE(r.session->saveAs(saved.getFile()));
    const auto before = r.session->undoDescription();

    r.session->setArmed(t0, true, true);
    r.session->setArmed(t1, true, false); // Ctrl+click: one more
    CHECK(r.info(t0).input.armed);
    CHECK(r.info(t1).input.armed);
    r.session->setArmed(t2, true, true); // a click: that one alone
    CHECK_FALSE(r.info(t0).input.armed);
    CHECK(r.info(t2).input.armed);
    CHECK(r.session->undoDescription() == before);
    CHECK_FALSE(r.session->hasUnsavedChanges());

    r.session->setArmed(t0, true, true);
    r.session->setArmed(t1, true, false);
    r.recordFor(0.5);
    CHECK(r.info(t0).clips.size() == 1);
    CHECK(r.info(t1).clips.size() == 1);
    CHECK(r.info(t2).clips.empty());
    CHECK(r.info(t0).clips.at(0).sourcePath != r.info(t1).clips.at(0).sourcePath);
}

TEST_CASE("recording: monitoring per track: Auto while armed, In always, Off never", "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    auto hear = [&] {
        r.pump(0.3); // the graph is rebuilt with the new routing
        return r.outputLevel() > 0.05f;
    };
    CHECK_FALSE(hear()); // Auto, not armed
    r.session->setArmed(t, true, true);
    CHECK(hear());
    r.session->setMonitor(t, fd::model::Monitor::Off);
    CHECK_FALSE(hear());
    r.session->setArmed(t, false, true);
    r.session->setMonitor(t, fd::model::Monitor::In);
    CHECK(hear());
    CHECK(r.info(t).input.monitor == fd::model::Monitor::In);
}

TEST_CASE("recording: punch records only inside the loop brace; settings round-trip", "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    r.session->setLoopRange(1.0, 2.0);
    r.session->setPunch(true);
    CHECK(r.session->punch());
    r.recordFor(2.5);
    const auto clips = r.info(t).clips;
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].start == Approx(1.0).margin(0.02));
    CHECK(clips[0].end() == Approx(2.0).margin(0.02));

    r.session->setCountIn(fd::model::CountIn::OneBar);
    CHECK(r.session->countIn() == fd::model::CountIn::OneBar);
    r.session->setCountIn(fd::model::CountIn::Off);
    r.session->setMidiOverdub(true);
    CHECK(r.session->midiOverdub());
}

TEST_CASE("recording: the recording offset moves recordings earlier", "[integration]")
{
    Recording r;
    const auto clickAt = static_cast<std::int64_t>(0.5 * kRate);
    std::int64_t passStart = 0;
    // A click 0.5 s into each pass's input.
    r.input = [&](std::int64_t s, int) { return s - passStart == clickAt ? 1.0f : 0.0f; };
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    auto clickIn = [&](std::uint64_t clip) -> double {
        const auto found = fd::model::findClip(*r.session->snapshot(), clip);
        REQUIRE(found);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(found->first->sourcePath)));
        REQUIRE(reader != nullptr);
        juce::AudioBuffer<float> data(1, static_cast<int>(reader->lengthInSamples));
        reader->read(&data, 0, data.getNumSamples(), 0, true, false);
        for (int i = 0; i < data.getNumSamples(); ++i)
            if (std::abs(data.getSample(0, i)) > 0.5f)
                return static_cast<double>(i) / kRate;
        return -1.0;
    };
    passStart = r.clock;
    r.recordFor(1.0);
    REQUIRE(r.info(t).clips.size() == 1);
    const double plain = clickIn(r.info(t).clips[0].id);
    REQUIRE(plain > 0.0);

    r.session->setRecordingOffsets(20.0, 0.0);
    CHECK(r.session->recordingOffsets().first == Approx(20.0));
    r.session->setPosition(4.0);
    passStart = r.clock;
    r.recordFor(1.0);
    r.session->setRecordingOffsets(0.0, 0.0);
    const auto clips = r.info(t).clips;
    REQUIRE(clips.size() == 2);
    CHECK(clickIn(clips[1].id) == Approx(plain - 0.020).margin(0.002));
}
