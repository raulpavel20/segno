// engine::Session (ADR 0018): commands are undoable and snapshots mirror the Edit.

#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "model/Editing.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <tracktion_engine/tracktion_engine.h>

#if defined(FD_HAVE_TEST_PATHS)
#include "fd_test_paths.h"
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;

TEST_CASE("session snapshots mirror tracks and clips; edits undo", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);

    const auto first = session.snapshot();
    REQUIRE(first->tracks.size() == 4);
    CHECK(first->tempo.bpm == Approx(120.0));
    CHECK(first->tempo.numerator == 4);
    CHECK(first->contentEnd() == 0.0);
    CHECK_FALSE(session.canUndo()); // a new project starts with a clean history

    int published = 0;
    session.onSnapshotChanged = [&] { ++published; };

    juce::TemporaryFile file(".wav");
    REQUIRE(fd::testkit::writeWav(file.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(440.0, 48000.0, 96000, 0.5f, 2), 48000.0));
    const auto trackId = first->tracks[1].id;
    const auto clipId = session.insertAudioFile(trackId, file.getFile(), 3.0);
    REQUIRE(clipId != 0);
    CHECK(session.insertAudioFile(trackId, juce::File("/nonexistent.wav"), 0.0) == 0);

    // Snapshots are rebuilt asynchronously after edits (coalesced).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(published >= 1);
    auto s = session.snapshot();
    CHECK(s->version > first->version);
    REQUIRE(s->tracks[1].clips.size() == 1);
    const auto& clip = s->tracks[1].clips[0];
    CHECK(clip.id == clipId);
    CHECK(clip.start == Approx(3.0));
    CHECK(clip.length == Approx(2.0));
    CHECK(clip.sourcePath == file.getFile().getFullPathName().toStdString());
    CHECK(s->contentEnd() == Approx(5.0));
    CHECK(first->tracks[1].clips.empty()); // published snapshots never change

    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[1].clips.empty());
    session.redo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[1].clips.size() == 1);

    // Transport position doesn't produce snapshots (after pending updates have settled).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    const auto version = session.snapshot()->version;
    session.setPosition(1.5);
    CHECK(session.position() == Approx(1.5));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(session.snapshot()->version == version);
}

TEST_CASE("session clip edits: move, trim, duplicate, split, delete; each one undo step", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);
    juce::TemporaryFile file(".wav");
    REQUIRE(fd::testkit::writeWav(file.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(440.0, 48000.0, 4 * 48000, 0.5f, 2), 48000.0));
    const auto tracks = session.snapshot()->tracks;
    const auto a = session.insertAudioFile(tracks[0].id, file.getFile(), 1.0);
    session.updateSnapshotNow();
    auto clipOf = [&](std::uint64_t id) { return fd::model::findClip(*session.snapshot(), id); };
    REQUIRE(clipOf(a));
    CHECK(clipOf(a)->first->sourceLength == Approx(4.0));

    // Move to track 2 at 2 s, trimmed to start 0.5 s into the file.
    session.applyClipEdits({{.clip = a, .track = tracks[2].id, .start = 2.0, .length = 3.0, .offset = 0.5}});
    session.updateSnapshotNow();
    auto moved = clipOf(a);
    REQUIRE(moved);
    CHECK(moved->second == 2);
    CHECK(moved->first->start == Approx(2.0));
    CHECK(moved->first->length == Approx(3.0));
    CHECK(moved->first->offset == Approx(0.5));

    // Split at 3 s: two clips that join seamlessly.
    const auto right = session.splitClips({a}, 3.0);
    session.updateSnapshotNow();
    REQUIRE(right.size() == 1);
    CHECK(clipOf(a)->first->length == Approx(1.0));
    CHECK(clipOf(right[0])->first->start == Approx(3.0));
    CHECK(clipOf(right[0])->first->offset == Approx(1.5));
    CHECK(session.splitClips({a}, 10.0).empty()); // outside the clip: nothing

    // Duplicate both parts after themselves.
    const auto copies = session.duplicateClips(fd::model::duplicatePlacements(*session.snapshot(), {a, right[0]}));
    session.updateSnapshotNow();
    REQUIRE(copies.size() == 2);
    CHECK(session.snapshot()->tracks[2].clips.size() == 4);

    session.deleteClips(copies);
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 2);

    // Undo walks back one gesture at a time: delete, duplicate, split, move.
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 4);
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 2);
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 1);
    session.undo();
    session.updateSnapshotNow();
    CHECK(clipOf(a)->second == 0);
    CHECK(clipOf(a)->first->start == Approx(1.0));
}

TEST_CASE("session transport: loop, metronome, tempo and signature", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);

    session.setLoopRange(8.0, 4.0); // reversed ends are fine
    CHECK(session.loopRange().first == Approx(4.0));
    CHECK(session.loopRange().second == Approx(8.0));
    session.setLooping(true);
    CHECK(session.isLooping());
    session.setMetronome(true);
    CHECK(session.isMetronomeOn());

    session.setTempo(96.5);
    session.setTimeSignature(7, 8);
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tempo.bpm == Approx(96.5));
    CHECK(session.snapshot()->tempo.numerator == 7);
    CHECK(session.snapshot()->tempo.denominator == 8);
    session.undo();
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tempo.bpm == Approx(120.0));
    CHECK(session.snapshot()->tempo.numerator == 4);
}

TEST_CASE("session device chains: add, move, bypass, remove; undo; panel handles", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    const auto track = session.snapshot()->tracks[0].id;
    auto chain = [&] {
        session.updateSnapshotNow();
        std::vector<std::string> ids;
        for (const auto& d : session.snapshot()->tracks[0].devices)
            ids.push_back(d.deviceId);
        return ids;
    };
    CHECK(chain().empty()); // tracktion's volume and meter plugins are not part of the chain

    const auto available = session.availableDevices();
    CHECK(std::any_of(available.begin(), available.end(), [](const auto& d) { return d.key == "fd:eq" && d.native; }));

    const auto eq = session.addDevice(track, "fd:eq");
    const auto sat = session.addDevice(track, "fd:saturator");
    const auto util = session.addDevice(track, "fd:utility", 0); // at the front
    REQUIRE(eq != 0);
    CHECK(session.addDevice(track, "fd:no-such-device") == 0);
    CHECK(chain() == std::vector<std::string>{"utility", "eq", "saturator"});

    session.moveDevice(util, 2);
    CHECK(chain() == std::vector<std::string>{"eq", "saturator", "utility"});
    session.setDeviceEnabled(sat, false);
    session.updateSnapshotNow();
    CHECK_FALSE(session.snapshot()->tracks[0].devices[1].enabled);

    auto handle = session.openDevice(eq);
    REQUIRE(handle != nullptr);
    CHECK(handle->deviceId() == "eq");
    CHECK(handle->context().info.id == "eq");
    // Edits through the panel's binding reach the device.
    handle->context().params.setPlain(0, 1.0f);
    CHECK(handle->context().params.getPlain(0) == Approx(1.0f));

    session.removeDevice(sat);
    CHECK(chain() == std::vector<std::string>{"eq", "utility"});
    session.undo(); // remove
    CHECK(chain() == std::vector<std::string>{"eq", "saturator", "utility"});
    session.undo(); // bypass
    session.undo(); // move
    CHECK(chain() == std::vector<std::string>{"utility", "eq", "saturator"});
}

#if defined(FD_UTILITY_VST3_PATH)
TEST_CASE("a scanned third-party plugin can be added to a chain", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);

    // Our own VST3 build stands in for a third-party plugin, registered as a scan would.
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(found, FD_UTILITY_VST3_PATH);
    REQUIRE(found.size() == 1);
    engine.getPluginManager().knownPluginList.addType(*found[0]);

    const auto available = session.availableDevices();
    const auto it = std::find_if(available.begin(), available.end(), [](const auto& d) { return !d.native; });
    REQUIRE(it != available.end());
    const auto id = session.addDevice(session.snapshot()->tracks[0].id, it->key);
    REQUIRE(id != 0);
    session.updateSnapshotNow();
    const auto& devices = session.snapshot()->tracks[0].devices;
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].kind == fd::model::DeviceSlot::Kind::External);
    CHECK(session.openDevice(id) == nullptr); // no native panel: shown as a card
}
#endif

TEST_CASE("instrument tracks, MIDI files and live MIDI routing", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    const auto before = session.snapshot()->tracks.size();

    // One undo step: the track and its instrument.
    const auto synthTrack = session.addInstrumentTrack();
    session.updateSnapshotNow();
    REQUIRE(session.snapshot()->tracks.size() == before + 1);
    const auto& t = session.snapshot()->tracks.back();
    CHECK(t.id == synthTrack);
    CHECK(t.kind == fd::model::TrackInfo::Kind::Instrument);
    REQUIRE(t.devices.size() == 1);
    CHECK(t.devices[0].deviceId == "subtractive-synth");
    CHECK(juce::String(t.name).contains("Subtractive Synth"));
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks.size() == before);
    session.redo();

    // A MIDI file: three notes at 0, 0.5 and 1 s (120 BPM file), inserted at 2 s.
    juce::TemporaryFile mid(".mid");
    {
        juce::MidiMessageSequence seq;
        for (int i = 0; i < 3; ++i)
        {
            seq.addEvent(juce::MidiMessage::noteOn(1, 60 + i * 4, 0.8f), i * 480.0);
            seq.addEvent(juce::MidiMessage::noteOff(1, 60 + i * 4), i * 480.0 + 240.0);
        }
        seq.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 0.0); // 120 BPM: 960 ticks per second
        seq.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream out(mid.getFile());
        REQUIRE(out.openedOk());
        REQUIRE(file.writeTo(out));
    }
    const auto clip = session.insertMidiFile(synthTrack, mid.getFile(), 2.0);
    REQUIRE(clip != 0);
    session.updateSnapshotNow();
    const auto found = fd::model::findClip(*session.snapshot(), clip);
    REQUIRE(found);
    CHECK(found->first->kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(found->first->start == Approx(2.0));
    REQUIRE(found->first->notes.size() == 3);
    CHECK(found->first->notes[1].pitch == 64);
    CHECK(found->first->notes[1].start == Approx(0.5).margin(0.01));
    CHECK(found->first->notes[1].length == Approx(0.25).margin(0.01));

    // Live MIDI follows the selected instrument track; no undo steps. The device list (with the
    // computer keyboard input) arrives asynchronously: the routing must follow when it does.
    const bool couldUndo = session.canUndo();
    session.setMidiInputTarget(synthTrack);
    CHECK(session.midiInputTarget() == synthTrack);
    auto routed = [&] {
        for (auto* instance : session.impl().edit->getAllInputDevices())
            if (instance->getInputDevice().getName() == fd::engine::Session::computerKeyboardName)
                return instance->getTargets().contains(tracktion::EditItemID::fromRawID(synthTrack));
        return false;
    };
    for (int i = 0; i < 50 && !routed(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(routed());
    session.computerKeyboardNote(60, 0.8f); // reaches the virtual input without errors
    session.computerKeyboardNote(60, 0.0f);
    session.setMidiInputTarget(std::nullopt);
    CHECK_FALSE(session.midiInputTarget());
    CHECK(session.canUndo() == couldUndo);
}
