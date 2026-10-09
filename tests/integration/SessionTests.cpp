// engine::Session (ADR 0018): commands are undoable and snapshots mirror the Edit.

#include "engine/EngineBehaviour.h"
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

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;

TEST_CASE("session snapshots mirror tracks and clips; edits undo", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
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
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
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
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
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
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
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

TEST_CASE("a latency-changing setting reaches the device at once (ADR 0020)", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    const auto track = session.snapshot()->tracks[0].id;
    const auto comp = session.addDevice(track, "fd:compressor");
    REQUIRE(comp != 0);
    auto handle = session.openDevice(comp);
    REQUIRE(handle != nullptr);
    const auto lookahead = static_cast<size_t>(std::distance(
        handle->context().info.params.begin(),
        std::find_if(handle->context().info.params.begin(), handle->context().info.params.end(),
                     [](const auto& p) { return p.id == "lookahead"; })));
    REQUIRE(lookahead < handle->context().info.params.size());
    const auto* device = handle->context().device;
    REQUIRE(device != nullptr);
    CHECK(device->latencySamples() == 0);

    // Without audio running: the device must learn the setting from the edit itself, so the
    // playback graph is rebuilt around the new latency.
    auto& params = handle->context().params;
    {
        // Its own undo step (in the app, tracktion's transaction timer separates them).
        fd::engine::Session::EditGroup step(session, "Lookahead");
        params.beginGesture(lookahead);
        params.setPlain(lookahead, 2.0f); // 10 ms
        params.endGesture(lookahead);
    }
    CHECK(device->latencySamples() > 0);
    session.undo();
    session.updateSnapshotNow();
    REQUIRE(session.snapshot()->tracks[0].devices.size() == 1);
    CHECK(device->latencySamples() == 0);
}

#if defined(FD_UTILITY_VST3_PATH)
TEST_CASE("a scanned third-party plugin can be added to a chain", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
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

TEST_CASE("tracks with an instrument, MIDI files and live MIDI routing", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    const auto before = session.snapshot()->tracks.size();

    // One kind of track: an instrument is just a device in its chain.
    const auto synthTrack = session.addTrack();
    session.addDevice(synthTrack, "fd:subtractive-synth");
    session.updateSnapshotNow();
    REQUIRE(session.snapshot()->tracks.size() == before + 1);
    const auto& t = session.snapshot()->tracks.back();
    CHECK(t.id == synthTrack);
    CHECK(t.kind == fd::model::TrackInfo::Kind::Instrument);
    CHECK(t.instrument == "Subtractive Synth");
    REQUIRE(t.devices.size() == 1);
    CHECK(t.devices[0].deviceId == "subtractive-synth");

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
    CHECK(found->first->contentStartBeat == Approx(4.0)); // 2 s at 120 BPM
    CHECK(found->first->notes[1].pitch == 64);
    CHECK(found->first->notes[1].start == Approx(1.0).margin(0.01)); // beats: 0.5 s
    CHECK(found->first->notes[1].length == Approx(0.5).margin(0.01));

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

TEST_CASE("a compressor on one track ducks to another track (ADR 0025)", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);

    // Track A: a steady tone; track B (the key): silent for 2 s, then loud for 2 s.
    constexpr double rate = 48000.0;
    juce::TemporaryFile toneWav(".wav"), keyWav(".wav"), out(".wav");
    REQUIRE(fd::testkit::writeWav(toneWav.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(1000.0, rate, 4 * 48000, 0.25f, 2), rate));
    auto key = fd::testkit::sine(100.0, rate, 4 * 48000, 0.5f, 2);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 2 * 48000; ++i)
            key.at(c, i) = 0.0f;
    REQUIRE(fd::testkit::writeWav(keyWav.getFile().getFullPathName().toStdString(), key, rate));
    const auto a = session.snapshot()->tracks[0].id;
    const auto b = session.addTrack("Key");
    session.insertAudioFile(a, toneWav.getFile(), 0.0);
    session.insertAudioFile(b, keyWav.getFile(), 0.0);
    const auto comp = session.addDevice(a, "fd:compressor");
    REQUIRE(comp != 0);
    {
        auto handle = session.openDevice(comp);
        auto& params = handle->context().params;
        for (const auto& [id, value] : std::initializer_list<std::pair<std::string_view, float>>{
                 {"threshold", -30.0f}, {"ratio", 8.0f}, {"knee", 0.0f}, {"attack", 1.0f}, {"release", 50.0f}})
            for (size_t i = 0; i < handle->context().info.params.size(); ++i)
                if (handle->context().info.params[i].id == id)
                    params.setPlain(i, value);
    }

    // RMS of track A's render in [from, to) seconds.
    auto renderA = [&] {
        out.getFile().deleteFile(); // the renderer appends to an existing file
        bool done = false;
        auto job = session.render({0.0, 4.0, a, rate}, out.getFile(), [&](const std::string& error) {
            CHECK(error.empty());
            done = true;
        });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 600 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        REQUIRE(done);
        const auto audio = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(audio.has_value());
        return [buffer = audio->buffer](double from, double to) {
            double sum = 0.0;
            const int i0 = static_cast<int>(from * 48000), i1 = static_cast<int>(to * 48000);
            for (int i = i0; i < i1; ++i)
                sum += double(buffer.at(0, i)) * buffer.at(0, i);
            return 10.0 * std::log10(sum / (i1 - i0));
        };
    };

    // Without a sidechain, the compressor hears the tone itself: it is turned down all along.
    const auto own = renderA();
    CHECK(own(2.5, 3.5) == Approx(own(0.5, 1.5)).margin(0.5));

    // Fed by the key track: untouched while the key is silent, ducked while it plays.
    const auto sources = session.sidechainSources(comp);
    CHECK(std::find(sources.begin(), sources.end(), b) != sources.end());
    CHECK(std::find(sources.begin(), sources.end(), a) == sources.end()); // never its own track
    REQUIRE(session.setSidechainSource(comp, b));
    CHECK(session.undoDescription() == "Set sidechain");
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[0].devices[0].sidechainSource == b);
    const auto keyed = renderA();
    CHECK(keyed(0.5, 1.5) > own(0.5, 1.5) + 10.0); // the tone alone isn't compressed now
    CHECK(keyed(2.5, 3.5) < keyed(0.5, 1.5) - 10.0); // the key ducks it

    // A loop is refused: the key track can't take a sidechain from A while A hears it.
    const auto gate = session.addDevice(b, "fd:gate");
    const auto gateSources = session.sidechainSources(gate);
    CHECK(std::find(gateSources.begin(), gateSources.end(), a) == gateSources.end());
    CHECK_FALSE(session.setSidechainSource(gate, a));
    CHECK_FALSE(session.setSidechainSource(session.addDevice(a, "fd:eq"), b)); // no sidechain input

    // None again, and back by undo.
    REQUIRE(session.setSidechainSource(comp, std::nullopt));
    session.updateSnapshotNow();
    CHECK_FALSE(session.snapshot()->tracks[0].devices[0].sidechainSource.has_value());
    session.undo(); // none
    session.undo(); // the EQ
    session.undo(); // the gate
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[0].devices[0].sidechainSource == b);
}

TEST_CASE("projects round-trip through a file", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    juce::TemporaryFile wav(".wav"), project(fd::engine::Session::fileExtension);
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(220.0, 48000.0, 96000, 0.5f, 2), 48000.0));

    std::shared_ptr<const fd::model::Snapshot> saved;
    float eqValue = 0.0f, synthValue = 0.0f;
    {
        fd::engine::Session session(engine);
        const auto audio = session.snapshot()->tracks[0].id;
        session.insertAudioFile(audio, wav.getFile(), 2.0);
        const auto eq = session.addDevice(audio, "fd:eq");
        const auto sat = session.addDevice(audio, "fd:saturator");
        session.setDeviceEnabled(sat, false);
        auto eqHandle = session.openDevice(eq);
        eqHandle->context().params.setPlain(2, 1234.0f); // a band frequency
        eqValue = eqHandle->context().params.getPlain(2);
        eqHandle.reset();

        const auto synth = session.addTrack();
        session.addDevice(synth, "fd:subtractive-synth");
        session.updateSnapshotNow();
        auto synthHandle = session.openDevice(session.snapshot()->tracks.back().devices[0].id);
        synthHandle->context().params.setPlain(0, 42.0f); // Osc 1 Shape
        synthValue = synthHandle->context().params.getPlain(0);
        synthHandle.reset();
        (void)synth;

        const auto sampler = session.addTrack();
        session.addDevice(sampler, "fd:sampler");
        session.updateSnapshotNow();
        auto samplerHandle = session.openDevice(session.snapshot()->tracks.back().devices[0].id);
        samplerHandle->context().params.applyExtraState({{"sample.path", wav.getFile().getFullPathName().toStdString()}});
        samplerHandle.reset();
        (void)sampler;

        session.setTempo(97.0);
        session.setTimeSignature(3, 4);
        session.setLoopRange(4.0, 8.0);
        session.viewStateProvider = [] { return std::string(R"({"pixelsPerSecond":42})"); };
        CHECK(session.hasUnsavedChanges());
        REQUIRE(session.saveAs(project.getFile()));
        CHECK_FALSE(session.hasUnsavedChanges());
        CHECK(session.file() == project.getFile());
        session.updateSnapshotNow();
        saved = session.snapshot();
    }

    fd::engine::Session reopened(engine);
    int willChange = 0, changed = 0;
    reopened.onProjectWillChange = [&] { ++willChange; };
    reopened.onProjectChanged = [&] { ++changed; };
    std::string error;
    REQUIRE(reopened.open(project.getFile(), &error));
    CHECK(willChange == 1);
    CHECK(changed == 1);
    CHECK_FALSE(reopened.hasUnsavedChanges());
    CHECK_FALSE(reopened.canUndo());
    CHECK(reopened.savedViewState() == R"({"pixelsPerSecond":42})");

    reopened.updateSnapshotNow();
    const auto& s = *reopened.snapshot();
    CHECK(s.tempo.bpm == Approx(97.0));
    CHECK(s.tempo.numerator == 3);
    CHECK(reopened.loopRange().first == Approx(4.0));
    REQUIRE(s.tracks.size() == saved->tracks.size());
    for (size_t t = 0; t < s.tracks.size(); ++t)
    {
        INFO("track " << t);
        const auto& a = s.tracks[t];
        const auto& b = saved->tracks[t];
        CHECK(a.name == b.name);
        CHECK(a.kind == b.kind);
        CHECK(a.colour == b.colour);
        REQUIRE(a.clips.size() == b.clips.size());
        for (size_t c = 0; c < a.clips.size(); ++c)
        {
            CHECK(a.clips[c].start == Approx(b.clips[c].start));
            CHECK(a.clips[c].length == Approx(b.clips[c].length));
            CHECK(a.clips[c].sourcePath == b.clips[c].sourcePath);
        }
        REQUIRE(a.devices.size() == b.devices.size());
        for (size_t d = 0; d < a.devices.size(); ++d)
        {
            CHECK(a.devices[d].deviceId == b.devices[d].deviceId);
            CHECK(a.devices[d].enabled == b.devices[d].enabled);
        }
    }

    // Device parameters and non-parameter state come back too.
    auto eq = reopened.openDevice(s.tracks[0].devices[0].id);
    CHECK(eq->context().params.getPlain(2) == Approx(eqValue));
    auto synth = reopened.openDevice(s.tracks[s.tracks.size() - 2].devices[0].id);
    CHECK(synth->context().params.getPlain(0) == Approx(synthValue));
    auto sampler = reopened.openDevice(s.tracks.back().devices[0].id);
    nlohmann::json extra = nlohmann::json::object();
    sampler->context().device->writeExtraState(extra);
    CHECK(extra["sample.path"] == wav.getFile().getFullPathName().toStdString());

    // New project: a clean slate, no file.
    eq.reset();
    synth.reset();
    sampler.reset();
    reopened.newProject();
    CHECK_FALSE(reopened.file());
    CHECK(reopened.snapshot()->tracks.size() == 4);
    CHECK_FALSE(reopened.open(juce::File("/nonexistent.segno"), &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("clips never overlap on a track: placing cuts what is covered, in one undo step", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    juce::TemporaryFile wav(".wav");
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(220.0, 48000.0, 4 * 48000, 0.5f, 2), 48000.0));
    const auto track = session.snapshot()->tracks[0].id;
    auto clips = [&] {
        session.updateSnapshotNow();
        std::vector<std::pair<double, double>> spans;
        for (const auto& c : session.snapshot()->tracks[0].clips)
            spans.emplace_back(c.start, c.end());
        return spans;
    };

    const auto a = session.insertAudioFile(track, wav.getFile(), 0.0); // [0, 4)
    const auto b = session.insertAudioFile(track, wav.getFile(), 8.0); // [8, 12)
    REQUIRE((a != 0 && b != 0));

    // Dropping a file into the middle of A splits A around it.
    session.insertAudioFile(track, wav.getFile(), 1.0); // [1, 5): covers A's tail too
    auto spans = clips();
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].second == Approx(1.0)); // A's head
    CHECK(spans[1].first == Approx(1.0));
    CHECK(spans[2].first == Approx(8.0));
    session.undo(); // the insertion and the cut are one step
    CHECK(clips().size() == 2);

    // Moving B over A trims A's tail: no overlap remains.
    session.applyClipEdits({{.clip = b, .track = track, .start = 2.0, .length = 4.0, .offset = 0.0}});
    spans = clips();
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].second == Approx(2.0));
    for (size_t i = 1; i < spans.size(); ++i)
        CHECK(spans[i].first >= spans[i - 1].second - 1e-6);

    // A MIDI file dropped on a track without an instrument brings none (ADR 0033): it's silent.
    juce::TemporaryFile mid(".mid");
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0.0);
        seq.addEvent(juce::MidiMessage::noteOff(1, 60), 960.0);
        seq.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream out(mid.getFile());
        REQUIRE(file.writeTo(out));
    }
    const auto other = session.snapshot()->tracks[1].id;
    REQUIRE(session.insertMidiFile(other, mid.getFile(), 0.0) != 0);
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[1].devices.empty());
    CHECK(session.snapshot()->tracks[1].instrument.empty());
    CHECK(session.snapshot()->tracks[1].clips.size() == 1);
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[1].devices.empty());
    CHECK(session.snapshot()->tracks[1].clips.empty());
}

TEST_CASE("MIDI clips from scratch and note edits, each one undo step", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    using fd::model::Note;
    const auto track = session.snapshot()->tracks.front().id;

    // An empty clip over bar 2 (2..4 s at 120 BPM); no instrument comes with it (ADR 0033).
    const auto clip = session.createMidiClip(track, 2.0, 2.0);
    REQUIRE(clip != 0);
    session.updateSnapshotNow();
    auto info = [&] {
        const auto found = fd::model::findClip(*session.snapshot(), clip);
        return found ? std::optional(*found->first) : std::nullopt;
    };
    REQUIRE(info());
    CHECK(info()->kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(info()->notes.empty());
    CHECK(info()->contentStartBeat == Approx(4.0));
    CHECK(session.snapshot()->tracks.front().kind == fd::model::TrackInfo::Kind::Audio);
    CHECK(session.snapshot()->tracks.front().devices.empty());

    // Notes round-trip exactly (beats, MIDI velocity steps).
    const std::vector<Note> notes{{60, 0.0, 0.5, 100.0f / 127.0f}, {64, 1.0, 0.5, 80.0f / 127.0f}, {67, 2.0, 1.0, 1.0f}};
    session.setNotes(clip, notes);
    session.updateSnapshotNow();
    CHECK(info()->notes == notes);

    // A gesture's result (one note moved, one deleted) is one step; the unchanged note's state
    // is left alone.
    auto* midi = dynamic_cast<tracktion::MidiClip*>(session.impl().clip(clip));
    REQUIRE(midi != nullptr);
    auto stateOf = [&](int pitch) {
        for (auto* n : midi->getSequence().getNotes())
            if (n->getNoteNumber() == pitch)
                return n->state;
        return juce::ValueTree();
    };
    const auto untouched = stateOf(60);
    auto edited = notes;
    edited[1].start = 1.5;
    edited.pop_back();
    session.setNotes(clip, edited);
    session.setNotes(clip, edited); // no change: no undo step
    session.updateSnapshotNow();
    CHECK(info()->notes == edited);
    CHECK(stateOf(60) == untouched);

    session.undo();
    session.updateSnapshotNow();
    CHECK(info()->notes == notes);
    session.undo();
    session.updateSnapshotNow();
    CHECK(info()->notes.empty());
    session.redo();
    session.redo();
    session.updateSnapshotNow();
    CHECK(info()->notes == edited);

    // No overlaps (ADR 0019): a new clip over the second half cuts this one.
    REQUIRE(session.createMidiClip(track, 3.0, 2.0) != 0);
    session.updateSnapshotNow();
    CHECK(info()->length == Approx(1.0));

    // Undo back to the start: no clip, no instrument.
    while (session.canUndo())
        session.undo();
    session.updateSnapshotNow();
    CHECK_FALSE(info());
    CHECK(session.snapshot()->tracks.front().devices.empty());
}

TEST_CASE("module view state is saved with the project, never undoable or unsaved", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    juce::TemporaryFile project(fd::engine::Session::fileExtension);
    const std::string view = R"({"collapsed":["curve"],"folded":true})";
    std::uint64_t eq = 0;
    {
        fd::engine::Session session(engine);
        eq = session.addDevice(session.snapshot()->tracks[0].id, "fd:eq");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100); // tracktion's change notices
        REQUIRE(session.saveAs(project.getFile()));
        REQUIRE_FALSE(session.hasUnsavedChanges());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100); // the save's own updates
        const auto version = session.snapshot()->version;
        const bool couldUndo = session.canUndo();

        session.setDeviceViewState(eq, view);
        CHECK(session.deviceViewState(eq) == view);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        CHECK_FALSE(session.hasUnsavedChanges());
        CHECK(session.canUndo() == couldUndo);
        CHECK(session.snapshot()->version == version); // the arrangement doesn't rebuild

        // Undo restores a removed module with its view.
        session.removeDevice(eq);
        session.undo();
        CHECK(session.deviceViewState(eq) == view);

        session.setDeviceViewState(eq, {}); // back to the default...
        CHECK(session.deviceViewState(eq).empty());
        session.setDeviceViewState(eq, view); // ...and saved with the project
        REQUIRE(session.saveAs(project.getFile()));
    }
    fd::engine::Session session(engine);
    REQUIRE(session.open(project.getFile()));
    CHECK(session.deviceViewState(eq) == view);
}

TEST_CASE("notes past a MIDI clip's end lengthen it in the same undo step", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    using fd::model::Note;
    const auto track = session.snapshot()->tracks.front().id;
    const auto clip = session.createMidiClip(track, 2.0, 2.0); // bar 2 (120 BPM)
    REQUIRE(clip != 0);
    auto info = [&] {
        session.updateSnapshotNow();
        return *fd::model::findClip(*session.snapshot(), clip)->first;
    };

    // A note running into the next bar: the clip grows to that bar's end.
    session.setNotes(clip, {Note{60, 3.0, 2.0, 0.8f}}); // beats 3..5 = 1.5 s..2.5 s past the start
    CHECK(info().length == Approx(4.0));
    CHECK(info().notes.size() == 1);
    session.undo(); // notes and length together
    CHECK(info().length == Approx(2.0));
    CHECK(info().notes.empty());

    // With a clip right after it, the clip stays as it is.
    REQUIRE(session.createMidiClip(track, 4.0, 2.0) != 0);
    session.setNotes(clip, {Note{60, 3.0, 2.0, 0.8f}});
    CHECK(info().length == Approx(2.0));
    CHECK(info().notes.size() == 1);
}

TEST_CASE("session: a new project has no undo steps, even after tracktion's own start-up work", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::Session session(engine);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    CHECK_FALSE(session.canUndo());

    // A named group stays one step, and tracktion's follow-ups join it.
    const auto track = session.snapshot()->tracks[0].id;
    {
        fd::engine::Session::EditGroup group(session, "Assistant: test");
        session.createMidiClip(track, 0.0, 2.0);
        session.createMidiClip(track, 4.0, 2.0);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    CHECK(session.undoDescription() == "Assistant: test");
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[0].clips.empty());
    CHECK_FALSE(session.canUndo());
    CHECK(session.redoDescription() == "Assistant: test");
}
