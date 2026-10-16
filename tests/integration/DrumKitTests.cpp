// The Drum Kit (M3 P7, docs/design/drum-kit.md §18, ADR 0064), through engine::Session: a kit added
// as an instrument; samples onto pads, each a Sampler at its own pitch; modules onto pads, an
// instrument in place of the pad's own; a pad's settings, its choke group heard; pads moved,
// swapped, copied, cleared and pasted, each one undo step; a kit kept with its project; a rack's
// own commands refused.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"
#include "model/DrumKit.h"
#include "model/Racks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;
namespace model = fd::model;

namespace
{
constexpr double kRate = 48000.0;

struct Kits
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-kit-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-kits-" + juce::Uuid().toString());
    std::uint64_t t = 0;

    Kits()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->setPresetsFolder(root.getChildFile("Presets"));
        (void)root.createDirectory();
        t = track(0);
    }

    ~Kits()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    model::TrackInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* c = session->snapshot()->channel(id);
        REQUIRE(c != nullptr);
        return *c;
    }

    model::DeviceSlot slot(std::uint64_t id)
    {
        session->updateSnapshotNow();
        for (const auto& c : session->snapshot()->tracks)
            if (const auto* d = model::findDevice(c.devices, id))
                return *d;
        FAIL("no module " << id);
        return {};
    }

    model::RackInfo kit(std::uint64_t id)
    {
        const auto s = slot(id);
        REQUIRE(s.rack != nullptr);
        REQUIRE(s.rack->kit);
        return *s.rack;
    }

    /// A pad of the kit as the snapshot has it (null: empty).
    std::optional<model::RackChain> pad(std::uint64_t kitId, int note)
    {
        const auto k = kit(kitId);
        if (const auto* p = model::kit::padOn(k, note))
            return *p;
        return std::nullopt;
    }

    /// A sound file in the test's folder.
    juce::File sample(const std::string& name, const AudioBuffer& sound)
    {
        const auto file = root.getChildFile(name + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sound, kRate));
        return file;
    }

    /// One of our module's parameters, plain.
    float param(std::uint64_t device, const std::string& paramId)
    {
        auto handle = session->openDevice(device);
        REQUIRE(handle != nullptr);
        const auto& params = handle->context().info.params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == paramId)
                return handle->context().params.getPlain(i);
        FAIL("no parameter " << paramId);
        return 0.0f;
    }

    void setParam(std::uint64_t device, const std::string& paramId, float plain)
    {
        auto handle = session->openDevice(device);
        REQUIRE(handle != nullptr);
        const auto& params = handle->context().info.params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == paramId)
            {
                handle->context().params.setPlain(i, plain);
                return;
            }
        FAIL("no parameter " << paramId);
    }

    /// Notes on the track: a beat (half a second) a note, each a sixteenth long.
    void notes(const std::vector<int>& pitches)
    {
        const auto clip = session->createMidiClip(t, 0.0, 4.0);
        std::vector<model::Note> list;
        for (size_t i = 0; i < pitches.size(); ++i)
            list.push_back({.pitch = pitches[i], .start = static_cast<double>(i), .length = 0.25, .velocity = 1.0f});
        session->setNotes(clip, list);
    }

    AudioBuffer render(double seconds = 2.0)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({0.0, seconds, std::nullopt, kRate}, out.getFile(),
                                   [&](const std::string& e)
                                   {
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
};

double levelDb(const AudioBuffer& b, double from, double seconds)
{
    const int start = static_cast<int>(from * kRate), length = static_cast<int>(seconds * kRate);
    AudioBuffer part(b.numChannels(), length);
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = 0; i < length && start + i < b.numSamples(); ++i)
            part.at(c, i) = b.at(c, start + i);
    return fd::testkit::rmsDb(part);
}
} // namespace

TEST_CASE("drum kit: added as an instrument, empty, one undo step; a rack's own commands don't apply to it",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    REQUIRE(kit != 0);
    CHECK(s.undoDescription() == "Add Drum Kit");
    const auto slot = k.slot(kit);
    CHECK(slot.deviceId == "drum-kit");
    CHECK(slot.name == "Drum Kit");
    CHECK(slot.instrument);
    CHECK(k.info(k.t).kind == model::TrackInfo::Kind::Instrument);
    CHECK(k.kit(kit).chains.empty());
    CHECK(k.kit(kit).selectedPad == 36);
    CHECK(k.kit(kit).padsFrom == 36);
    CHECK(s.moduleType(kit) == "Drum Kit \xc2\xb7 0 pads");
    // It's listed with the instruments, and its presets are the Drum Kit's.
    const auto available = s.availableDevices();
    CHECK(std::any_of(available.begin(), available.end(),
                      [](const auto& d) { return d.key == "fd:drum-kit" && d.instrument && d.native; }));
    REQUIRE(Session::nativeInfo("fd:drum-kit") != nullptr);
    CHECK(Session::nativeInfo("fd:drum-kit")->id == "drum-kit");
    // A rack's commands: refused.
    CHECK(s.addRackChain(kit) == -1);
    CHECK(s.ungroupRack(kit).empty());
    CHECK_FALSE(s.renameRack(kit, "Kit"));
    CHECK_FALSE(s.saveRack(kit, k.root.getChildFile("kit.segnorack")));
    s.undo();
    CHECK(k.info(k.t).devices.empty());
}

TEST_CASE("drum kit: samples onto pads, each a One-shot Sampler at its own pitch, named for its file; one step",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    const auto kick = k.sample("Kick 1", fd::testkit::sine(60.0, kRate, 9600, 0.5f, 1));
    const auto snare = k.sample("Snare", fd::testkit::whiteNoise(9600, 2, 0.3f, 1));
    const auto hat = k.sample("Hat", fd::testkit::whiteNoise(4800, 3, 0.2f, 1));
    CHECK(s.loadPads(kit, 36, {kick, snare, hat}) == std::vector<int>{36, 37, 38});
    CHECK(s.undoDescription() == "Load 3 samples on pads");
    const auto info = k.kit(kit);
    REQUIRE(info.chains.size() == 3);
    for (const auto& [note, name] : {std::pair{36, "Kick 1"}, std::pair{37, "Snare"}, std::pair{38, "Hat"}})
    {
        INFO("pad " << note);
        const auto pad = k.pad(kit, note);
        REQUIRE(pad);
        CHECK(pad->name == name);
        CHECK(pad->plays == 60);
        REQUIRE(pad->devices.size() == 1);
        CHECK(pad->devices[0].deviceId == "sampler");
        CHECK(k.param(pad->devices[0].id, "mode") == 1.0f); // One-shot
        CHECK(k.param(pad->devices[0].id, "root") == 60.0f); // at its own pitch
    }
    CHECK(s.moduleType(kit) == "Drum Kit \xc2\xb7 3 pads");
    // Each pad plays its own note.
    k.notes({36, 38});
    const auto out = k.render(1.5);
    CHECK(levelDb(out, 0.0, 0.2) > -30.0);  // the kick
    CHECK(levelDb(out, 0.5, 0.1) > -40.0);  // the hat on 38
    CHECK(levelDb(out, 0.25, 0.2) < -80.0); // between them, nothing
    s.undo(); // the notes
    s.undo(); // the clip
    s.undo();
    CHECK(k.kit(kit).chains.empty());
}

TEST_CASE("drum kit: a sample onto a pad with a Sampler goes into it, its settings kept; its root at Plays",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    const auto first = k.sample("Kick 1", fd::testkit::sine(60.0, kRate, 9600, 0.5f, 1));
    const auto second = k.sample("Kick 2", fd::testkit::sine(55.0, kRate, 9600, 0.5f, 1));
    s.loadPads(kit, 36, {first});
    const auto sampler = k.pad(kit, 36)->devices.at(0).id;
    k.setParam(sampler, "amp.d", 120.0f);
    REQUIRE(s.setPad(kit, 36, {.plays = 62}));
    s.loadPads(kit, 36, {second});
    const auto pad = k.pad(kit, 36);
    REQUIRE(pad);
    CHECK(pad->devices.at(0).id == sampler);           // the same Sampler
    CHECK(k.param(sampler, "amp.d") == Approx(120.0f)); // its settings kept
    CHECK(k.param(sampler, "root") == 62.0f);           // at the pad's Plays: its own pitch
    CHECK(pad->name == "Kick 1");                       // a pad keeps its name
    CHECK(s.undoDescription() == "Load Kick 2 on C1");
}

TEST_CASE("drum kit: modules onto pads, an instrument first and in place of the pad's own; an emptied pad goes",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    s.loadPads(kit, 36, {k.sample("Kick", fd::testkit::sine(60.0, kRate, 9600, 0.5f, 1))});
    const auto sampler = k.pad(kit, 36)->devices.at(0).id;

    // An effect after the pad's last module.
    const auto saturator = s.addDevice(Session::ChainPlace{.rack = kit, .pad = 36}, "fd:saturator");
    REQUIRE(saturator != 0);
    CHECK(k.pad(kit, 36)->devices.size() == 2);
    CHECK(k.pad(kit, 36)->devices[1].id == saturator);
    CHECK(s.placeOf(saturator)->pad == 36);

    // An instrument in place of the Sampler, first, the Saturator kept; one step.
    const auto synth = s.addDevice(Session::ChainPlace{.rack = kit, .pad = 36}, "fd:subtractive-synth");
    REQUIRE(synth != 0);
    CHECK(s.undoDescription() == "Add module");
    {
        const auto pad = k.pad(kit, 36);
        REQUIRE(pad->devices.size() == 2);
        CHECK(pad->devices[0].id == synth);
        CHECK(pad->devices[1].id == saturator);
        CHECK(pad->name == "Kick"); // its name stays
    }
    s.undo();
    CHECK(k.pad(kit, 36)->devices.at(0).id == sampler);

    // A synth onto an empty pad: a new pad, named for it.
    const auto tom = s.addDevice(Session::ChainPlace{.rack = kit, .pad = 43}, "fd:subtractive-synth");
    REQUIRE(tom != 0);
    CHECK(k.pad(kit, 43)->name == "Subtractive");
    CHECK(k.pad(kit, 43)->plays == 60);

    // A module moved off a pad: the pad left with nothing goes.
    REQUIRE(s.moveDevice(tom, Session::ChainPlace{.rack = kit, .pad = 45}));
    CHECK_FALSE(k.pad(kit, 43));
    CHECK(k.pad(kit, 45));
    s.removeDevice(tom);
    CHECK_FALSE(k.pad(kit, 45));
    // A rack never goes on a pad.
    const auto rack = s.addDevice(Session::ChainPlace{.rack = kit, .pad = 50}, "fd:drum-kit");
    CHECK(rack == 0);
    CHECK_FALSE(k.pad(kit, 50));
}

TEST_CASE("drum kit: a pad's settings, each one step; Plays tunes its instrument; choke groups heard",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    // The open hat a long tone, the closed hat silence: what's heard after it is the open hat.
    s.loadPads(kit, 46, {k.sample("Hat open", fd::testkit::sine(330.0, kRate, 96'000, 0.5f, 1))});
    s.loadPads(kit, 42, {k.sample("Hat closed", fd::testkit::silence(4800, 1))});
    REQUIRE(s.setPad(kit, 46, {.choke = 1}));
    CHECK(s.undoDescription() == "Choke group");
    REQUIRE(s.setPad(kit, 42, {.choke = 1}));
    REQUIRE(s.setPad(kit, 46, {.name = "Open"}));
    CHECK(s.undoDescription() == "Rename pad");
    REQUIRE(s.setPad(kit, 46, {.volumeDb = -6.0f}));
    CHECK(s.undoDescription() == "Pad volume");
    REQUIRE(s.setPad(kit, 46, {.pan = 0.0f}));
    {
        const auto pad = k.pad(kit, 46);
        CHECK(pad->name == "Open");
        CHECK(pad->choke == 1);
        CHECK(pad->volumeDb == Approx(-6.0f).margin(0.01));
    }
    CHECK_FALSE(s.setPad(kit, 50, {.choke = 1})); // an empty pad has no settings
    k.notes({46, 42});
    const auto choked = k.render(2.0);
    CHECK(levelDb(choked, 0.1, 0.3) > -20.0);  // the open hat rings
    CHECK(levelDb(choked, 0.55, 0.4) < -100.0); // the closed hat stopped it
    REQUIRE(s.setPad(kit, 42, {.choke = 0}));
    const auto apart = k.render(2.0);
    CHECK(levelDb(apart, 0.55, 0.4) > -20.0); // in no group, it rings on

    // Off and solo, heard.
    REQUIRE(s.setPad(kit, 46, {.on = false}));
    CHECK(s.undoDescription() == "Pad on/off");
    CHECK(levelDb(k.render(1.0), 0.1, 0.3) < -100.0);
    REQUIRE(s.setPad(kit, 46, {.on = true}));
    REQUIRE(s.setPad(kit, 42, {.solo = true}));
    CHECK(s.undoDescription() == "Solo pad");
    CHECK(levelDb(k.render(1.0), 0.1, 0.3) < -100.0); // the other pad soloed: the open hat's silent
}

TEST_CASE("drum kit: Plays sets the note a pad plays into its instrument", "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    // A 440 Hz sample: at C3 it's 440 Hz, at C4 an octave up.
    s.loadPads(kit, 36, {k.sample("A", fd::testkit::sine(440.0, kRate, 24'000, 0.5f, 1))});
    k.notes({36});
    const auto atOwn = k.render(0.5);
    REQUIRE(s.setPad(kit, 36, {.plays = 72}));
    CHECK(s.undoDescription() == "Pad plays");
    const auto up = k.render(0.5);
    // Zero crossings over 0.2 s: twice as many an octave up.
    const auto crossings = [](const AudioBuffer& b)
    {
        int n = 0;
        for (int i = 2400; i < 2400 + 9600; ++i)
            n += (b.at(0, i - 1) < 0.0f) != (b.at(0, i) < 0.0f) ? 1 : 0;
        return n;
    };
    CHECK(crossings(atOwn) == Approx(176).margin(3));
    CHECK(crossings(up) == Approx(352).margin(4));
}

TEST_CASE("drum kit: pads moved, swapped, copied, cleared, copied and pasted, each one undo step",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    s.loadPads(kit, 36, {k.sample("Kick", fd::testkit::sine(60.0, kRate, 9600, 0.5f, 1)),
                         k.sample("Snare", fd::testkit::whiteNoise(9600, 2, 0.3f, 1))});
    s.setPad(kit, 36, {.choke = 2});

    // Moved onto an empty pad: its sound and settings go with it.
    REQUIRE(s.movePad(kit, 36, 40, false));
    CHECK(s.undoDescription() == "Move pad");
    CHECK_FALSE(k.pad(kit, 36));
    CHECK(k.pad(kit, 40)->name == "Kick");
    CHECK(k.pad(kit, 40)->choke == 2);
    CHECK(k.kit(kit).selectedPad == 40);
    s.undo();
    CHECK(k.pad(kit, 36)->name == "Kick");

    // Onto a pad with a sound: they swap.
    REQUIRE(s.movePad(kit, 36, 37, false));
    CHECK(k.pad(kit, 36)->name == "Snare");
    CHECK(k.pad(kit, 37)->name == "Kick");
    s.undo();

    // Copied (Ctrl+drag): both pads have it; the copy's modules are its own.
    REQUIRE(s.movePad(kit, 36, 48, true));
    CHECK(s.undoDescription() == "Copy pad");
    CHECK(k.pad(kit, 48)->name == "Kick");
    CHECK(k.pad(kit, 48)->choke == 2);
    CHECK(k.pad(kit, 48)->devices.at(0).id != k.pad(kit, 36)->devices.at(0).id);
    CHECK(k.pad(kit, 48)->devices.at(0).deviceId == "sampler");
    s.undo();
    CHECK_FALSE(k.pad(kit, 48));

    // Cleared.
    REQUIRE(s.clearPad(kit, 37));
    CHECK(s.undoDescription() == "Clear pad");
    CHECK_FALSE(k.pad(kit, 37));
    s.undo();
    CHECK(k.pad(kit, 37)->name == "Snare");

    // Copy pad, then Paste pad over another pad of another kit.
    REQUIRE(s.copyPad(kit, 36));
    CHECK(s.canPastePad());
    const auto other = s.addDevice(k.track(1), "fd:drum-kit");
    REQUIRE(s.pastePad(other, 38));
    CHECK(s.undoDescription() == "Paste pad");
    CHECK(k.pad(other, 38)->name == "Kick");
    k.notes({36});
    CHECK(levelDb(k.render(0.5), 0.0, 0.2) > -30.0); // the first kit still plays its kick
}

TEST_CASE("drum kit: its view state, never an undo step; the track's own audio passes once", "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto noise = fd::testkit::whiteNoise(48'000, 7, 0.2f, 2);
    REQUIRE(s.insertAudioFile(k.t, k.sample("noise", noise), 0.0) != 0);
    const auto before = k.render(1.0);
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    std::vector<juce::File> files;
    for (int i = 0; i < 16; ++i)
        files.push_back(k.sample("Pad " + std::to_string(i), fd::testkit::sine(100.0 + 50.0 * i, kRate, 4800, 0.2f, 1)));
    REQUIRE(s.loadPads(kit, 36, files).size() == 16);
    INFO("with the kit against before: " << fd::testkit::nullResidualDb(k.render(1.0), before) << " dB");
    CHECK(fd::testkit::nullResidualDb(k.render(1.0), before) < -100.0);

    const auto step = s.undoDescription();
    s.selectPad(kit, 40);
    s.showPads(kit, 54);
    CHECK(k.kit(kit).selectedPad == 40);
    CHECK(k.kit(kit).padsFrom == 52); // a row's first
    CHECK(s.undoDescription() == step);
}

TEST_CASE("drum kit: kept with its project, pads, settings and chokes", "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    s.loadPads(kit, 46, {k.sample("Hat open", fd::testkit::sine(330.0, kRate, 96'000, 0.5f, 1))});
    s.loadPads(kit, 42, {k.sample("Hat closed", fd::testkit::silence(4800, 1))});
    s.setPad(kit, 46, {.choke = 1});
    s.setPad(kit, 42, {.plays = 64, .choke = 1});
    s.selectPad(kit, 42);
    k.notes({46, 42});
    const auto first = k.render(2.0);
    const auto file = k.root.getChildFile("Kit project").getChildFile("Kit project.segno");
    REQUIRE(s.saveAs(file));
    REQUIRE(s.open(file));
    k.session->updateSnapshotNow();
    const auto& devices = k.info(k.track(0)).devices;
    REQUIRE(devices.size() == 1);
    const auto reopened = k.kit(devices[0].id);
    REQUIRE(reopened.chains.size() == 2);
    CHECK(k.pad(devices[0].id, 42)->plays == 64);
    CHECK(k.pad(devices[0].id, 46)->choke == 1);
    CHECK(reopened.selectedPad == 42);
    const auto second = k.render(2.0);
    INFO("reopened against saved: " << fd::testkit::nullResidualDb(second, first) << " dB");
    CHECK(fd::testkit::nullResidualDb(second, first) < -100.0);
}

TEST_CASE("drum kit: kits are its presets: saved, loaded whole into another kit, compared A/B, and a default",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    s.loadPads(kit, 36, {k.sample("Kick", fd::testkit::sine(60.0, kRate, 9600, 0.5f, 1)),
                         k.sample("Rim", fd::testkit::whiteNoise(2400, 4, 0.3f, 1))});
    const auto saturator = s.addDevice(Session::ChainPlace{.rack = kit, .pad = 36}, "fd:saturator");
    s.setPad(kit, 37, {.plays = 65, .choke = 3});
    s.setPad(kit, 36, {.volumeDb = -3.0f});
    REQUIRE(s.mapMacro(kit, 0, saturator, "drive", 0.0f, 0.8f));
    REQUIRE(s.setMacro(kit, 0, 0.5f, std::string("Crunch")));
    k.notes({36, 37});
    const auto first = k.render(1.5);

    const auto& info = *Session::nativeInfo("fd:drum-kit");
    auto host = s.presetHost(kit);
    REQUIRE(host != nullptr);
    fd::PresetController controller(info, *host);
    const auto saved = controller.saveAs("My kit", {"Electronic"}, "", "", false);
    REQUIRE(saved.preset != nullptr);
    CHECK_FALSE(controller.modified());

    // Into another kit on another track: the same pads, the same sound.
    const auto t2 = k.track(1);
    const auto other = s.addDevice(t2, "fd:drum-kit");
    auto otherHost = s.presetHost(other);
    REQUIRE(otherHost != nullptr);
    s.presetLibrary().rescan();
    const auto* preset = s.presetLibrary().find(info, saved.preset->key());
    REQUIRE(preset != nullptr);
    REQUIRE(fd::PresetController(info, *otherHost).load(*preset));
    CHECK(s.undoDescription() == "Load preset My kit");
    const auto loaded = k.kit(other);
    REQUIRE(loaded.chains.size() == 2);
    CHECK(k.pad(other, 36)->name == "Kick");
    CHECK(k.pad(other, 36)->volumeDb == Approx(-3.0f).margin(0.01));
    CHECK(k.pad(other, 36)->devices.size() == 2);
    CHECK(k.pad(other, 37)->plays == 65);
    CHECK(k.pad(other, 37)->choke == 3);
    CHECK(loaded.macros[0].name == "Crunch");
    CHECK(loaded.macros[0].value == Approx(0.5f));
    REQUIRE(loaded.macros[0].mappings.size() == 1);
    CHECK(loaded.macros[0].mappings[0].module == k.pad(other, 36)->devices[1].id);
    CHECK_FALSE(fd::PresetController(info, *otherHost).modified());
    // Heard the same: the first kit muted, the second playing the same notes.
    s.setMute(k.t, true);
    const auto clip = s.createMidiClip(t2, 0.0, 4.0);
    s.setNotes(clip, {{.pitch = 36, .start = 0.0, .length = 0.25, .velocity = 1.0f},
                      {.pitch = 37, .start = 1.0, .length = 0.25, .velocity = 1.0f}});
    const auto second = k.render(1.5);
    INFO("the preset's kit against the first: " << fd::testkit::nullResidualDb(second, first) << " dB");
    CHECK(fd::testkit::nullResidualDb(second, first) < -90.0);

    // A/B: two whole kits to switch between.
    fd::PresetController ab(info, *otherHost);
    ab.switchCompare(); // B: the same kit to start with
    CHECK(ab.onB());
    REQUIRE(s.clearPad(other, 37));
    CHECK(k.kit(other).chains.size() == 1);
    ab.switchCompare(); // back to A: its two pads
    CHECK(k.kit(other).chains.size() == 2);
    ab.switchCompare(); // B again: one
    CHECK(k.kit(other).chains.size() == 1);

    // Your default: a new kit starts as it.
    REQUIRE(fd::PresetController(info, *host).useAsDefault());
    const auto fresh = s.addDevice(k.track(2), "fd:drum-kit");
    CHECK(k.kit(fresh).chains.size() == 2);
    CHECK(k.pad(fresh, 36)->name == "Kick");
}

TEST_CASE("drum kit: a synth on a pad is choked as quickly as a Sampler is", "[integration]")
{
    for (const auto& [synth, release] :
         {std::pair{"fd:subtractive-synth", "amp.r"}, std::pair{"fd:wavetable", "env1.r"}})
    {
        INFO(synth);
        Kits k;
        auto& s = *k.session;
        const auto kit = s.addDevice(k.t, "fd:drum-kit");
        // A held synth on the open hat's pad, its release long; the closed hat silence.
        const auto open = s.addDevice(Session::ChainPlace{.rack = kit, .pad = 46}, synth);
        REQUIRE(open != 0);
        k.setParam(open, release, 2000.0f);
        s.loadPads(kit, 42, {k.sample("Hat closed", fd::testkit::silence(4800, 1))});
        s.setPad(kit, 46, {.choke = 1});
        s.setPad(kit, 42, {.choke = 1});
        const auto clip = s.createMidiClip(k.t, 0.0, 4.0);
        s.setNotes(clip, {{.pitch = 46, .start = 0.0, .length = 3.0, .velocity = 1.0f},
                          {.pitch = 42, .start = 1.0, .length = 0.25, .velocity = 1.0f}});
        const auto out = k.render(2.0);
        CHECK(levelDb(out, 0.2, 0.25) > -40.0);    // the synth sounds
        CHECK(levelDb(out, 0.5021, 0.3) < -100.0); // and stops within 2 ms of the closed hat
    }
}

TEST_CASE("drum kit, step 2: its macros set modules on several pads at once, are automated as a rack's, "
          "and have no snapshots, Morph, Vary or locks",
          "[integration]")
{
    Kits k;
    auto& s = *k.session;
    const auto kit = s.addDevice(k.t, "fd:drum-kit");
    s.loadPads(kit, 41,
               {k.sample("Tom 1", fd::testkit::sine(100.0, kRate, 9600, 0.5f, 1)),
                k.sample("Tom 2", fd::testkit::sine(140.0, kRate, 9600, 0.5f, 1))});
    const auto low = k.pad(kit, 41)->devices.at(0).id;
    const auto high = k.pad(kit, 42)->devices.at(0).id;

    // One macro tunes both toms (drum-kit.md §10): each Sampler's Transpose follows it.
    REQUIRE(s.mapMacro(kit, 0, low, "transpose", 0.5f, 0.7f));
    REQUIRE(s.mapMacro(kit, 0, high, "transpose", 0.5f, 0.7f));
    REQUIRE(s.setMacro(kit, 0, std::nullopt, std::string("Toms")));
    REQUIRE(s.setMacro(kit, 0, 1.0f));
    const auto at = [&](std::uint64_t module)
    { return s.paramValue({k.t, "m" + std::to_string(module) + "/transpose"}); };
    REQUIRE(at(low));
    CHECK(*at(low) == Approx(0.7f).margin(0.01));
    CHECK(*at(high) == Approx(0.7f).margin(0.01));
    REQUIRE(k.kit(kit).macros[0].mappings.size() == 2);
    // One undo step: the macro back where it was (its parameters follow it as the engine plays).
    CHECK(s.undoDescription() == "Macro");
    s.undo();
    CHECK(k.kit(kit).macros[0].value == Approx(0.0f).margin(1e-3));

    // Automated as a rack's: its macros are the kit's parameters, named as renamed.
    const auto params = s.automatableParameters(k.t);
    CHECK(std::any_of(params.begin(), params.end(), [&](const Session::ParamChoice& p)
                      { return p.key == "m" + std::to_string(kit) + "/macro1" && p.name == "Toms"; }));

    // Snapshots, Morph, Vary and locks stay a rack's for now.
    CHECK(s.storeSnapshot(kit) < 0);
    CHECK_FALSE(s.setMorph(kit, {.on = true}));
    CHECK_FALSE(s.varyRack(kit, Session::varyAmount));
    CHECK_FALSE(s.lockMacro(kit, 0, true));
    CHECK(k.kit(kit).snapshots.empty());
}
