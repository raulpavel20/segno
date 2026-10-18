// MIDI effects in the DAW (M3 P11 step 1, ADR 0074, docs/design/midi-effects.md): where they go in a
// track's chain, the song they follow, Harmony per track, Listen, and the tools that drive them.

#include "Pitch.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "model/Racks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <tracktion_engine/tracktion_engine.h>

using fd::engine::Session;
namespace model = fd::model;

namespace
{
struct Daw
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-midi-effect-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-midi-effects-" + juce::Uuid().toString());

    Daw()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        (void)root.createDirectory();
    }
    ~Daw()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }
    /// The chain's modules, by id, in order.
    std::vector<std::uint64_t> chain(std::uint64_t track)
    {
        session->updateSnapshotNow();
        std::vector<std::uint64_t> out;
        for (const auto& d : session->snapshot()->channel(track)->devices)
            out.push_back(d.id);
        return out;
    }
};
} // namespace

TEST_CASE("midi effects: added or moved after the instrument, a MIDI effect lands before it", "[integration]")
{
    Daw daw;
    auto& s = *daw.session;
    const auto t = daw.track(0);
    const auto synth = s.addDevice(t, "fd:subtractive-synth");
    const auto eq = s.addDevice(t, "fd:eq");
    const auto pitch = s.addDevice(t, "fd:pitch"); // asked for at the end
    CHECK(daw.chain(t) == std::vector<std::uint64_t>{pitch, synth, eq});
    const auto chord = s.addDevice(t, "fd:chord", 2); // asked for after the synth
    CHECK(daw.chain(t) == std::vector<std::uint64_t>{pitch, chord, synth, eq});
    s.moveDevice(pitch, 3); // after the EQ: just before the synth
    CHECK(daw.chain(t) == std::vector<std::uint64_t>{chord, pitch, synth, eq});
    s.moveDevice(eq, 0); // an audio effect goes where it's asked
    CHECK(daw.chain(t) == std::vector<std::uint64_t>{eq, chord, pitch, synth});
    // A modulator in the chain counts among its places (modulators.md §6); still before the synth.
    const auto lfo = s.addModulator(t, model::ModulatorKind::Lfo, -1);
    const auto arp = s.addDevice(t, "fd:arpeggiator");
    const auto order = daw.chain(t);
    const auto at = [&](std::uint64_t id)
    { return std::find(order.begin(), order.end(), id) - order.begin(); };
    CHECK(at(arp) < at(synth));
    CHECK(at(lfo) > at(synth));
    // With no instrument, at the end.
    const auto other = s.addTrack();
    const auto delay = s.addDevice(other, "fd:delay");
    const auto alone = s.addDevice(other, "fd:pitch");
    CHECK(daw.chain(other) == std::vector<std::uint64_t>{delay, alone});
}

TEST_CASE("midi effects: listed apart, as MIDI effects", "[integration]")
{
    Daw daw;
    std::vector<std::string> names;
    for (const auto& d : daw.session->availableDevices())
        if (d.midiEffect)
        {
            CHECK(d.category == "MIDI effects");
            CHECK_FALSE(d.instrument);
            names.push_back(d.name);
        }
    CHECK(names == std::vector<std::string>{"Arpeggiator", "Chord", "Pitch"}); // A to Z
}

namespace
{
fd::engine::DevicePlugin& nativeOf(Session& s, std::uint64_t id)
{
    auto* p = dynamic_cast<fd::engine::DevicePlugin*>(s.impl().plugin(id));
    REQUIRE(p != nullptr);
    return *p;
}
} // namespace

TEST_CASE("midi effects: each is handed its track's song, again only when it changes", "[integration]")
{
    Daw daw;
    auto& s = *daw.session;
    // A track of chords: C for two beats (a second), then F.
    const auto keys = daw.track(0);
    const auto clip = s.createMidiClip(keys, 0.0, 2.0);
    std::vector<model::Note> notes;
    for (const int p : {60, 64, 67})
        notes.push_back({.pitch = p, .start = 0.0, .length = 2.0});
    for (const int p : {60, 65, 69})
        notes.push_back({.pitch = p, .start = 2.0, .length = 2.0});
    s.setNotes(clip, notes);
    // A lead track with Pitch on it.
    const auto lead = s.addTrack();
    s.createMidiClip(lead, 1.0, 1.0);
    const auto pitch = s.addDevice(lead, "fd:pitch");
    s.updateSnapshotNow();
    auto view = nativeOf(s, pitch).songView();
    REQUIRE(view != nullptr);
    REQUIRE(view->chords.size() == 2);
    CHECK(view->chords[0].root == 0);
    CHECK(view->chords[1].start == 2.0);
    CHECK(view->chords[1].root == 5);
    REQUIRE(view->clips.size() == 1); // the lead's own clip, in beats
    CHECK(view->clips[0].start == 2.0);

    // An edit elsewhere that leaves the song as it was: the same view, not handed again.
    s.setTrackColour(lead, 5);
    s.updateSnapshotNow();
    CHECK(nativeOf(s, pitch).songView() == view);

    // The keys track's Harmony off: no chords.
    CHECK(s.setTrackHarmony(keys, model::HarmonyUse::Off));
    s.updateSnapshotNow();
    CHECK(nativeOf(s, pitch).songView()->chords.empty());
    CHECK(s.snapshot()->channel(keys)->harmony == model::HarmonyUse::Off);
    s.undo();
    s.updateSnapshotNow();
    CHECK(nativeOf(s, pitch).songView()->chords.size() == 2);

    // Chords may change once a bar: the second chord, two beats long, joins the first.
    CHECK(s.setChordChange(model::ChordChange::Bar));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->chordChange == model::ChordChange::Bar);
    CHECK(nativeOf(s, pitch).songView()->chords.size() == 1);
    CHECK_FALSE(s.setChordChange(model::ChordChange::Bar)); // already so
}

TEST_CASE("midi effects: Listen, any other track's notes, never a loop", "[integration]")
{
    Daw daw;
    auto& s = *daw.session;
    const auto a = daw.track(0);
    const auto b = s.addTrack();
    const auto c = s.addTrack();
    const auto onA = s.addDevice(a, "fd:pitch");
    const auto onB = s.addDevice(b, "fd:chord");
    const auto has = [](const std::vector<std::uint64_t>& v, std::uint64_t id)
    { return std::find(v.begin(), v.end(), id) != v.end(); };
    auto sources = s.sidechainSources(onA);
    CHECK_FALSE(has(sources, a)); // never its own
    CHECK(has(sources, b));
    CHECK(has(sources, c));
    REQUIRE(s.setSidechainSource(onA, b));
    CHECK(s.undoDescription() == "Set Listen");
    s.updateSnapshotNow();
    CHECK(model::findDevice(s.snapshot()->channel(a)->devices, onA)->sidechainSource == b);
    // B can't hear A now: A hears B.
    CHECK_FALSE(has(s.sidechainSources(onB), a));
    CHECK_FALSE(s.setSidechainSource(onB, a));
    // The panel's picker: None, then the tracks it can hear.
    auto handle = s.openDevice(onA);
    REQUIRE(handle != nullptr);
    REQUIRE(handle->context().sidechain != nullptr);
    const auto names = handle->context().sidechain->sources();
    CHECK(names.front() == "None");
    CHECK(handle->context().sidechain->selected() >= 1);
    handle.reset();
    // Deleting B: A hears nothing, in the same step; undo brings both back.
    REQUIRE(s.deleteTrack(b));
    s.updateSnapshotNow();
    CHECK_FALSE(model::findDevice(s.snapshot()->channel(a)->devices, onA)->sidechainSource.has_value());
    s.undo();
    s.updateSnapshotNow();
    CHECK(model::findDevice(s.snapshot()->channel(a)->devices, onA)->sidechainSource == b);
    // None again.
    REQUIRE(s.setSidechainSource(onA, std::nullopt));
    s.updateSnapshotNow();
    CHECK_FALSE(model::findDevice(s.snapshot()->channel(a)->devices, onA)->sidechainSource.has_value());
}

TEST_CASE("midi effects: Keep as notes, what they play kept in the clip, the notes before as a take",
          "[integration]")
{
    Daw daw;
    auto& s = *daw.session;
    const auto t = daw.track(0);
    const auto clip = s.createMidiClip(t, 0.0, 2.0); // a bar at 120 BPM
    s.setNotes(clip,
               {{.pitch = 60, .start = 0.0, .length = 1.0}, {.pitch = 64, .start = 2.0, .length = 1.0}});
    const auto up = s.addDevice(t, "fd:pitch");
    const auto chord = s.addDevice(t, "fd:chord"); // after it: not kept when keeping up to Pitch
    s.addDevice(t, "fd:subtractive-synth");
    auto* pitch = dynamic_cast<fd::engine::DevicePlugin*>(s.impl().plugin(up));
    REQUIRE(pitch != nullptr);
    const auto& spec = pitch->device().info().params[fd::devices::Pitch::Semitones];
    pitch->setStoredValue(fd::devices::Pitch::Semitones, spec.toNormalized(12.0f));

    std::string why;
    REQUIRE(s.keepAsNotes(up, {}, &why));
    CHECK(s.undoDescription() == "Keep as notes");
    s.updateSnapshotNow();
    const auto* info = s.snapshot()->channel(t);
    REQUIRE(info != nullptr);
    const auto* kept = [&]() -> const model::ClipInfo*
    {
        for (const auto& c : info->clips)
            if (c.kind == model::ClipInfo::Kind::Midi)
                return &c;
        return nullptr;
    }();
    REQUIRE(kept != nullptr);
    REQUIRE(kept->notes.size() == 2);
    CHECK(kept->notes[0].pitch == 72);
    CHECK(kept->notes[1].pitch == 76);
    CHECK(kept->notes[1].start == Catch::Approx(2.0));
    // Pitch off, Chord (after it) as it was.
    CHECK_FALSE(model::findDevice(info->devices, up)->enabled);
    CHECK(model::findDevice(info->devices, chord)->enabled);
    // The notes before, a take of the clip's.
    REQUIRE(kept->stack != 0);
    const auto* stack = info->stack(kept->stack);
    REQUIRE(stack != nullptr);
    std::vector<std::string> takes;
    for (const auto& take : stack->takes)
        takes.push_back(take.name);
    CHECK(std::find(takes.begin(), takes.end(), "Before MIDI effects") != takes.end());
    CHECK(std::find(takes.begin(), takes.end(), "Kept notes") != takes.end());
    // One step back: as it was.
    s.undo();
    s.updateSnapshotNow();
    info = s.snapshot()->channel(t);
    CHECK(model::findDevice(info->devices, up)->enabled);
    bool original = false;
    for (const auto& c : info->clips)
        original = original ||
                   (c.kind == model::ClipInfo::Kind::Midi && c.notes.size() == 2 && c.notes[0].pitch == 60);
    CHECK(original);
    // Not a MIDI effect: refused, saying why.
    CHECK_FALSE(s.keepAsNotes(s.addDevice(t, "fd:eq"), {}, &why));
    CHECK_FALSE(why.empty());
}

TEST_CASE("midi effects: Keep as notes keeps an arpeggio, a looped clip's laps as notes", "[integration]")
{
    Daw daw;
    auto& s = *daw.session;
    const auto t = daw.track(0);
    // A chord held for a beat, the clip looping a beat over two seconds (four laps).
    const auto clip = s.createMidiClip(t, 0.0, 2.0);
    s.setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 1.0},
                      {.pitch = 64, .start = 0.0, .length = 1.0},
                      {.pitch = 67, .start = 0.0, .length = 1.0}});
    REQUIRE(s.setClipLoop(clip, {.start = 0.0, .length = 0.5}).empty());
    s.updateSnapshotNow();
    INFO("clip ends at " << s.snapshot()->channel(t)->clips.front().end());
    const auto arp = s.addDevice(t, "fd:arpeggiator");
    std::string why;
    // A loop's laps: keep them, the loop off.
    REQUIRE(s.keepAsNotes(arp, {}, &why));
    s.updateSnapshotNow();
    for (const auto& c : s.snapshot()->channel(t)->clips)
        if (c.kind == model::ClipInfo::Kind::Midi)
        {
            CHECK_FALSE(c.loop.on());
            // Up at 1/16 over each lap's beat: C E G C, a step each, every lap.
            CHECK(c.end() == Catch::Approx(2.0));
            REQUIRE(c.notes.size() == 16);
            CHECK(c.notes[0].pitch == 60);
            CHECK(c.notes[1].pitch == 64);
            CHECK(c.notes[1].start == Catch::Approx(0.25));
            CHECK(c.notes[2].pitch == 67);
        }
}
