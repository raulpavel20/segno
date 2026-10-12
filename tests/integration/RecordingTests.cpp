// Recording (M2 step C5, ADRs 0030 and 0031, docs/design/recording.md): inputs, arming,
// monitoring, recording passes and project folders, through tracktion's hosted test device.

#include "RecordingFixture.h"
#include "model/Editing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("recording: MIDI overdub never merges into a looped clip: the pass makes its own (ADR 0048)", "[integration]")
{
    Recording r;
    const auto t = r.track(1);
    REQUIRE(r.session->addDevice(t, "fd:subtractive-synth") != 0);
    r.session->setMidiInputTarget(t);
    r.session->setMidiOverdub(true);
    // A one-bar loop out to four bars, one note in it.
    const auto looped = r.session->createMidiClip(t, 0.0, 2.0);
    r.session->setNotes(looped, {{.pitch = 48, .start = 0.0, .length = 0.5}});
    REQUIRE(r.session->setClipsLooping({looped}, true).empty());
    r.session->applyClipEdits({{.clip = looped, .track = t, .start = 0.0, .length = 8.0, .offset = 0.0}});

    r.session->setPosition(4.0);
    r.session->record();
    r.pump(0.2);
    r.session->computerKeyboardNote(60, 0.8f);
    r.pump(0.2);
    r.session->computerKeyboardNote(60, 0.0f);
    r.pump(0.1);
    r.session->stop();
    r.pump(0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    // The looped clip keeps its notes and loop, cut where the pass began; the pass is its own clip.
    const auto clips = r.info(t).clips;
    REQUIRE(clips.size() >= 2);
    CHECK(clips[0].id == looped);
    CHECK(clips[0].loop.on());
    CHECK(clips[0].notes.size() == 1);
    CHECK(clips[0].end() == Approx(4.0).margin(0.02));
    CHECK(clips[1].start == Approx(4.0).margin(0.02));
    CHECK_FALSE(clips[1].loop.on());
    REQUIRE(clips[1].notes.size() == 1);
    CHECK(clips[1].notes[0].pitch == 60);
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
    CHECK(allTakes(info).size() == 3);
    for (const auto& take : allTakes(info))
        for (const auto& c : take.clips)
            CHECK(c.start >= 4.0 - 0.02);

    r.session->setCountIn(fd::model::CountIn::OneBar);
    CHECK(r.session->countIn() == fd::model::CountIn::OneBar);
    r.session->setCountIn(fd::model::CountIn::Off);
    r.session->setMidiOverdub(true);
    CHECK(r.session->midiOverdub());
}

TEST_CASE("live: reordering modules is heard at once, and on undo", "[integration]")
{
    // The product owner's report: a module dragged to a new place in the chain changed the panels,
    // but the sound only after something else rebuilt the graph (clicking another track).
    Recording r;
    r.input = sine; // 0.5 peak
    const auto t = r.track(0);
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setMonitor(t, fd::model::Monitor::In);
    // Utility +30 dB into the Limiter (its ceiling −0.3 dB): loud, but held under the ceiling.
    const auto utility = r.session->addDevice(t, "fd:utility");
    const auto limiter = r.session->addDevice(t, "fd:limiter");
    REQUIRE(utility != 0);
    REQUIRE(limiter != 0);
    {
        auto handle = r.session->openDevice(utility);
        REQUIRE(handle != nullptr);
        const auto& params = handle->context().info.params;
        const auto gain = static_cast<size_t>(std::distance(
            params.begin(), std::find_if(params.begin(), params.end(), [](const auto& p) { return p.id == "gain"; })));
        REQUIRE(gain < params.size());
        handle->context().params.setPlain(gain, 30.0f);
    }
    r.pump(0.5);
    CHECK(r.outputLevel() < 1.2f);

    // The Limiter first: the Utility raises what it let through, nothing else touched.
    r.session->moveDevice(limiter, 0);
    r.pump(0.5);
    CHECK(r.outputLevel() > 5.0f);

    // Undone: held under the ceiling again.
    r.session->undo();
    r.pump(0.5);
    CHECK(r.outputLevel() < 1.2f);
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
    REQUIRE(allTakes(info).size() == 4);
    CHECK(allTakes(info)[0].name == "Take 1");
    CHECK(allTakes(info)[3].name == "Take 4");
    REQUIRE(info.clips.size() == 1);
    const auto& main = info.clips[0];
    CHECK(main.start == Approx(4.0).margin(0.01));
    CHECK(main.length == Approx(2.0).margin(0.01));
    CHECK(main.fromTake == allTakes(info)[2].clips.at(0).id); // the third lap: the last complete one
    CHECK(main.sourcePath == allTakes(info)[2].clips[0].sourcePath);
    CHECK(allTakes(info)[3].clips.at(0).length == Approx(0.5).margin(0.05)); // the half lap ends where it stopped
    std::set<std::string> files;
    for (const auto& take : allTakes(info))
        files.insert(take.clips.at(0).sourcePath);
    CHECK(files.size() == 4); // a file per lap
    CHECK(r.session->undoDescription() == "Record");
    r.session->undo();
    CHECK(allTakes(r.info(t)).empty());
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
    REQUIRE(allTakes(info).size() == 2);
    CHECK(allTakes(info)[0].name == "Before take 1");
    REQUIRE(allTakes(info)[0].clips.size() == 1);
    CHECK(juce::File(allTakes(info)[0].clips[0].sourcePath) == wav.getFile());
    CHECK(allTakes(info)[1].name == "Take 1");
    // The main lane: what was there before, cut around the new recording (ADR 0019).
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[1].fromTake == allTakes(info)[1].clips.at(0).id);
}

TEST_CASE("takes: use, step through, rename, delete, and copy or move to new tracks", "[integration]")
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
    REQUIRE(allTakes(info).size() >= 3);
    const auto take1 = allTakes(info)[0].id, take2 = allTakes(info)[1].id;

    REQUIRE(r.session->useTake(t, take1));
    CHECK(r.session->undoDescription() == "Use take");
    info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].fromTake == allTakes(info)[0].clips.at(0).id);
    REQUIRE(r.session->stepTake(info.clips[0].id, 1)); // Alt+↓: the next take
    CHECK(r.info(t).clips.at(0).fromTake == allTakes(r.info(t))[1].clips.at(0).id);

    r.session->renameTake(t, take2, "Keeper");
    CHECK(allTakes(r.info(t))[1].name == "Keeper");
    r.session->setTakesOpen(r.info(t).stacks.at(0).id, false);
    CHECK_FALSE(r.info(t).stacks.at(0).open);

    // Delete unused takes: every one but the one playing; their files stay.
    const auto unusedFile = juce::File(allTakes(r.info(t))[0].clips.at(0).sourcePath);
    r.session->deleteUnusedTakes(t);
    REQUIRE(allTakes(r.info(t)).size() == 1);
    CHECK(allTakes(r.info(t))[0].id == take2);
    CHECK(unusedFile.existsAsFile());
    r.session->undo();
    REQUIRE(allTakes(r.info(t)).size() >= 3);

    // Copies on new tracks: each plays its take, with the track's modules and mix (automation
    // stays behind: here a lane on the track's pan).
    r.session->setVolume(t, -6.0f);
    const auto panLane = r.session->addAutomationLane(Session::ParamTarget{t, "pan"});
    REQUIRE(r.session->setLanePoints(panLane, {{0.0, 0.2f, 0.0f}, {2.0, 0.8f, 0.0f}}));
    const auto tracksBefore = r.session->snapshot()->tracks.size();
    const auto made = r.session->takesToTracks(t, {take1, take2}, false);
    REQUIRE(made.size() == 2);
    CHECK(r.session->undoDescription() == "Copy takes to new tracks");
    r.session->updateSnapshotNow();
    CHECK(r.session->snapshot()->tracks.size() == tracksBefore + 2);
    const auto copy = r.info(made[0]);
    REQUIRE(copy.clips.size() == 1);
    CHECK(copy.clips[0].sourcePath == allTakes(r.info(t))[0].clips.at(0).sourcePath);
    CHECK(allTakes(copy).empty());
    CHECK(copy.mix.volumeDb == Approx(-6.0f).margin(0.01f));
    CHECK_FALSE(copy.input.armed);
    {
        // The copy's pan has no curve, and a lane assigned to it later writes into the project.
        auto* made0 = dynamic_cast<te::AudioTrack*>(te::findTrackForID(*r.session->impl().edit, te::EditItemID::fromRawID(made[0])));
        REQUIRE(made0 != nullptr);
        auto& pan = *made0->getVolumePlugin()->panParam;
        CHECK(pan.getCurve().getNumPoints() == 0);
        const auto lane = r.session->addAutomationLane(Session::ParamTarget{made[0], "pan"});
        REQUIRE(r.session->setLanePoints(lane, {{0.0, 0.0f, 0.0f}, {1.0, 1.0f, 0.0f}}));
        CHECK(pan.getCurve().getNumPoints() == 2);
        int inTree = 0; // in the project's tree, not a detached copy
        for (auto curve : made0->getVolumePlugin()->state)
            if (curve.hasType(te::IDs::AUTOMATIONCURVE) && curve[te::IDs::paramID].toString() == "pan")
                inTree = curve.getNumChildren();
        CHECK(inTree == 2);
        r.session->undo();
        r.session->undo();
    }
    r.session->updateSnapshotNow();
    CHECK(r.session->snapshot()->tracks[1].id == made[0]); // right after the track
    const auto takesBefore = allTakes(r.info(t)).size();
    r.session->takesToTracks(t, {take1}, true); // moved: off this track
    CHECK(allTakes(r.info(t)).size() == takesBefore - 1);
}

namespace
{
/// Plays notes on the computer keyboard, each held `hold` seconds with `gap` between them.
void playNotes(Recording& r, std::initializer_list<int> notes, double hold = 0.15, double gap = 0.1)
{
    for (int note : notes)
    {
        r.session->computerKeyboardNote(note, 0.8f);
        r.pump(hold);
        r.session->computerKeyboardNote(note, 0.0f);
        r.pump(gap);
    }
}
} // namespace

TEST_CASE("capture: the last phrase becomes a clip at the playhead, on a track without an instrument", "[integration]")
{
    Recording r;
    const auto t = r.track(1);
    r.pump(0.2);
    CHECK_FALSE(r.session->canCaptureMidi());
    // A note, a pause longer than two bars (4 s at 120 BPM), then the phrase.
    playNotes(r, {48});
    r.pump(5.0);
    playNotes(r, {60, 62, 64});
    CHECK(r.session->canCaptureMidi());
    r.session->setPosition(8.0); // bar 5
    const auto clip = r.session->captureMidi(t);
    REQUIRE(clip != 0);
    CHECK(r.session->undoDescription() == "Capture MIDI");
    CHECK_FALSE(r.session->canCaptureMidi()); // used up
    const auto info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].start == Approx(8.0).margin(0.001));
    REQUIRE(info.clips[0].notes.size() == 3); // not the note before the pause
    CHECK(info.clips[0].notes[0].pitch == 60);
    CHECK(info.clips[0].notes[0].start == Approx(0.0).margin(0.02)); // on the playhead's beat
    CHECK(info.clips[0].notes[2].pitch == 64);
    // No instrument was added (ADR 0033): the clip is silent until one is.
    CHECK(info.devices.empty());
    CHECK(info.instrument.empty());
}

TEST_CASE("capture: while playing it lands where it was played; nothing selected makes a track", "[integration]")
{
    Recording r;
    r.session->setPosition(2.2);
    r.session->play();
    r.pump(1.0); // at 3.2 s
    playNotes(r, {60, 67}, 0.2, 0.05);
    r.pump(0.3);
    r.session->stop();
    r.pump(0.1);
    const auto tracksBefore = r.session->snapshot()->tracks.size();
    const auto clip = r.session->captureMidi(std::nullopt);
    REQUIRE(clip != 0);
    r.session->updateSnapshotNow();
    REQUIRE(r.session->snapshot()->tracks.size() == tracksBefore + 1);
    const auto& made = r.session->snapshot()->tracks.back();
    REQUIRE(made.clips.size() == 1);
    const auto& c = made.clips[0];
    CHECK(c.start == Approx(3.0).margin(0.01)); // the beat before the first note (3.2 s)
    REQUIRE(c.notes.size() == 2);
    CHECK(c.start + r.session->snapshot()->tempo.secondsForBeats(c.notes[0].start) == Approx(3.2).margin(0.03));
    CHECK(made.instrument.empty());
    r.session->undo(); // one step: the track and its clip
    r.session->updateSnapshotNow();
    CHECK(r.session->snapshot()->tracks.size() == tracksBefore);
}

TEST_CASE("capture: armed MIDI tracks get what their input takes; a pass uses it up", "[integration]")
{
    Recording r;
    const auto a = r.track(0), b = r.track(1), c = r.track(2);
    r.session->setMidiInput(a, "keyboard");
    r.session->setMidiInput(b, "keyboard");
    r.session->setMidiChannel(b, 2); // the keyboard plays channel 1
    r.session->setArmed(a, true, true);
    r.session->setArmed(b, true, false);
    playNotes(r, {60, 64});
    REQUIRE(r.session->captureMidi(c) != 0); // the selected track doesn't matter while some are armed
    CHECK(r.info(a).clips.size() == 1);
    CHECK(r.info(b).clips.empty());
    CHECK(r.info(c).clips.empty());

    // A recording pass records what's played: it's used up for Capture.
    playNotes(r, {62});
    CHECK(r.session->canCaptureMidi());
    r.session->setArmed(b, false, false);
    r.session->setArmed(a, false, false);
    r.recordFor(0.5);
    CHECK_FALSE(r.session->canCaptureMidi());
}
