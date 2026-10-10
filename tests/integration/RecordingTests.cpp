// Recording (M2 step C5, ADRs 0030 and 0031, docs/design/recording.md): inputs, arming,
// monitoring, recording passes and project folders, through tracktion's hosted test device.

#include "RecordingFixture.h"
#include "model/Editing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using fd::engine::Session;
namespace te = tracktion;

using namespace recording_test;

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

TEST_CASE("recording: while looping only the brace records, after a lead-in; settings round-trip", "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    // Not looping: from the playhead, through the brace and past it.
    r.session->setLoopRange(4.0, 6.0);
    r.session->setPosition(3.0);
    r.recordFor(4.0);
    REQUIRE(r.info(t).clips.size() == 1);
    CHECK(r.info(t).clips[0].start == Approx(3.0).margin(0.02));
    CHECK(r.info(t).clips[0].end() == Approx(7.0).margin(0.05));
    r.session->undo();

    // Looping: the lead-in from 3 s is heard, only the brace records, a take per lap.
    r.session->setLooping(true);
    r.session->setPosition(3.0);
    r.recordFor(5.5); // a second of lead-in, then two laps and a quarter
    r.session->setLooping(false);
    const auto info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].start == Approx(4.0).margin(0.02));
    CHECK(info.clips[0].end() == Approx(6.0).margin(0.02));
    CHECK(info.takes.size() == 3);
    for (const auto& take : info.takes)
        for (const auto& c : take.clips)
            CHECK(c.start >= 4.0 - 0.02);

    r.session->setCountIn(fd::model::CountIn::OneBar);
    CHECK(r.session->countIn() == fd::model::CountIn::OneBar);
    r.session->setCountIn(fd::model::CountIn::Off);
    r.session->setMidiOverdub(true);
    CHECK(r.session->midiOverdub());
}

TEST_CASE("recording: the recording offset moves recordings earlier", "[integration]")
{
    // Declared before the recording, whose input reads it until it goes.
    const auto clickAt = static_cast<std::int64_t>(0.5 * kRate);
    std::int64_t passStart = 0;
    Recording r;
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

TEST_CASE("takes: a loop pass keeps every lap on a take lane; the main lane plays the newest complete one",
          "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    r.session->setLoopRange(4.0, 6.0);
    r.session->setLooping(true);
    r.session->setPosition(4.0);
    r.recordFor(6.5); // three laps and a half
    r.session->setLooping(false);
    auto info = r.info(t);
    REQUIRE(info.takes.size() == 4);
    CHECK(info.takes[0].name == "Take 1");
    CHECK(info.takes[3].name == "Take 4");
    REQUIRE(info.clips.size() == 1);
    const auto& main = info.clips[0];
    CHECK(main.start == Approx(4.0).margin(0.01));
    CHECK(main.length == Approx(2.0).margin(0.01));
    CHECK(main.fromTake == info.takes[2].clips.at(0).id); // the third lap: the last complete one
    CHECK(main.sourcePath == info.takes[2].clips[0].sourcePath);
    CHECK(info.takes[3].clips.at(0).length == Approx(0.5).margin(0.05)); // the half lap ends where it stopped
    std::set<std::string> files;
    for (const auto& take : info.takes)
        files.insert(take.clips.at(0).sourcePath);
    CHECK(files.size() == 4); // a file per lap
    CHECK(r.session->undoDescription() == "Record");
    r.session->undo();
    CHECK(r.info(t).takes.empty());
    CHECK(r.info(t).clips.empty());
}

TEST_CASE("takes: what a pass records over is kept as a take first", "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    juce::TemporaryFile wav(".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream(wav.getFile().createOutputStream().release());
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                         .withSampleRate(kRate)
                                                         .withNumChannels(1)
                                                         .withBitsPerSample(24));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> data(1, static_cast<int>(kRate * 3));
        data.clear();
        writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples());
    }
    REQUIRE(r.session->insertAudioFile(t, wav.getFile(), 0.0) != 0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    r.session->setPosition(1.0);
    r.recordFor(1.0);
    auto info = r.info(t);
    REQUIRE(info.takes.size() == 2);
    CHECK(info.takes[0].name == "Before take 1");
    REQUIRE(info.takes[0].clips.size() == 1);
    CHECK(juce::File(info.takes[0].clips[0].sourcePath) == wav.getFile());
    CHECK(info.takes[1].name == "Take 1");
    // The main lane: what was there before, cut around the new recording (ADR 0019).
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[1].fromTake == info.takes[1].clips.at(0).id);
}

TEST_CASE("takes: use, step through, star, rename, delete, and copy or move to new tracks", "[integration]")
{
    Recording r;
    r.input = sine;
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    r.session->setLoopRange(2.0, 4.0);
    r.session->setLooping(true);
    r.session->setPosition(2.0);
    r.recordFor(6.0); // three laps
    r.session->setLooping(false);
    auto info = r.info(t);
    REQUIRE(info.takes.size() >= 3);
    const auto take1 = info.takes[0].id, take2 = info.takes[1].id;

    REQUIRE(r.session->useTake(t, take1));
    CHECK(r.session->undoDescription() == "Use take");
    info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].fromTake == info.takes[0].clips.at(0).id);
    REQUIRE(r.session->stepTake(info.clips[0].id, 1)); // Alt+↓: the next take
    CHECK(r.info(t).clips.at(0).fromTake == r.info(t).takes[1].clips.at(0).id);

    r.session->setTakeStar(t, take2, true);
    r.session->renameTake(t, take2, "Keeper");
    CHECK(r.info(t).takes[1].star);
    CHECK(r.info(t).takes[1].name == "Keeper");
    r.session->showTakes(t, true);
    CHECK(r.info(t).showTakes);

    // Delete unused takes: every one but the one playing; their files stay.
    const auto unusedFile = juce::File(r.info(t).takes[0].clips.at(0).sourcePath);
    r.session->deleteUnusedTakes(t);
    REQUIRE(r.info(t).takes.size() == 1);
    CHECK(r.info(t).takes[0].id == take2);
    CHECK(unusedFile.existsAsFile());
    r.session->undo();
    REQUIRE(r.info(t).takes.size() >= 3);

    // Copies on new tracks: each plays its take, with the track's modules and mix.
    r.session->setVolume(t, -6.0f);
    const auto tracksBefore = r.session->snapshot()->tracks.size();
    const auto made = r.session->takesToTracks(t, {take1, take2}, false);
    REQUIRE(made.size() == 2);
    CHECK(r.session->undoDescription() == "Copy takes to new tracks");
    r.session->updateSnapshotNow();
    CHECK(r.session->snapshot()->tracks.size() == tracksBefore + 2);
    const auto copy = r.info(made[0]);
    REQUIRE(copy.clips.size() == 1);
    CHECK(copy.clips[0].sourcePath == r.info(t).takes[0].clips.at(0).sourcePath);
    CHECK(copy.takes.empty());
    CHECK(copy.mix.volumeDb == Approx(-6.0f).margin(0.01f));
    CHECK_FALSE(copy.input.armed);
    r.session->updateSnapshotNow();
    CHECK(r.session->snapshot()->tracks[1].id == made[0]); // right after the track
    const auto takesBefore = r.info(t).takes.size();
    r.session->takesToTracks(t, {take1}, true); // moved: off this track
    CHECK(r.info(t).takes.size() == takesBefore - 1);
}

TEST_CASE("takes: Capture MIDI turns what was just played into a clip at the playhead", "[integration]")
{
    Recording r;
    const auto t = r.track(1);
    REQUIRE(r.session->addDevice(t, "fd:subtractive-synth") != 0);
    r.session->setMidiInputTarget(t);
    r.pump(0.2);
    CHECK_FALSE(r.session->canCaptureMidi());
    for (int note : {60, 62, 64})
    {
        r.session->computerKeyboardNote(note, 0.8f);
        r.pump(0.15);
        r.session->computerKeyboardNote(note, 0.0f);
        r.pump(0.1);
    }
    CHECK(r.session->canCaptureMidi());
    r.session->setPosition(8.0); // bar 5
    const auto clip = r.session->captureMidi();
    REQUIRE(clip != 0);
    CHECK(r.session->undoDescription() == "Capture MIDI");
    CHECK_FALSE(r.session->canCaptureMidi());
    const auto info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].start == Approx(8.0).margin(0.001));
    REQUIRE(info.clips[0].notes.size() == 3);
    CHECK(info.clips[0].notes[0].pitch == 60);
    CHECK(info.clips[0].notes[2].pitch == 64);
}
