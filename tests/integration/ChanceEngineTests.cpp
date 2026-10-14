// Chance and controllers in the engine (M3 P4b, docs/design/piano-roll.md §10, §11): notes keep
// their chance, spread and release velocity; a clip plays what model::noteFate decides, lap after
// lap, and a render plays the same; Re-roll changes it and undo brings it back; controller lanes
// play as MIDI events and read back as their points, recorded events as points that hold.

#include "engine/ClipNotes.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/WavIO.h"
#include "model/Chance.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::model::Note;
namespace te = tracktion;

namespace
{
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-chance-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-chance-" + juce::Uuid().toString());

    Fixture()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        REQUIRE(root.createDirectory());
    }

    ~Fixture()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track()
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.front().id;
    }

    fd::model::ClipInfo clip(std::uint64_t id)
    {
        session->updateSnapshotNow();
        for (const auto& t : session->snapshot()->tracks)
            for (const auto& c : t.clips)
                if (c.id == id)
                    return c;
        FAIL("no clip " << id);
        return {};
    }

    te::MidiClip& engineClip(std::uint64_t id)
    {
        auto* c = dynamic_cast<te::MidiClip*>(session->impl().clip(id));
        REQUIRE(c != nullptr);
        return *c;
    }

    /// What the clip plays, as the graph builds it: its note-ons, by time in seconds from its start.
    std::vector<std::pair<double, juce::MidiMessage>> noteOns(std::uint64_t id)
    {
        auto& c = engineClip(id);
        const auto sequence =
            c.getSequenceLooped().exportToPlaybackMidiSequence(c, te::MidiList::TimeBase::seconds, false);
        std::vector<std::pair<double, juce::MidiMessage>> out;
        for (const auto* e : sequence)
            if (e->message.isNoteOn())
                out.emplace_back(e->message.getTimeStamp(), e->message);
        return out;
    }

    fd::testkit::AudioBuffer render(double start, double end)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, 48000.0}, out.getFile(),
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

Note note(int pitch, double start, float chance = 1.0f)
{
    Note n{pitch, start, 0.25, 100.0f / 127.0f};
    n.chance = chance;
    return n;
}

/// A bar of eighth notes, each played half the time, two with a velocity spread.
std::vector<Note> chancyBar()
{
    std::vector<Note> notes;
    for (int k = 0; k < 8; ++k)
        notes.push_back(note(60 + k, k * 0.5, 0.5f));
    notes[1].velocitySpread = 30.0f / 127.0f;
    notes[6].velocitySpread = 30.0f / 127.0f;
    notes[7].chance = 1.0f;
    return notes;
}
} // namespace

TEST_CASE("chance (engine): notes keep chance, spread and release velocity, moved, saved and opened",
          "[integration]")
{
    Fixture f;
    const auto clipId = f.session->createMidiClip(f.track(), 0.0, 8.0);
    std::vector<Note> notes{note(60, 0.0, 0.25f), note(64, 1.0)};
    notes[0].velocitySpread = 10.0f / 127.0f;
    notes[1].releaseVelocity = 64.0f / 127.0f;
    f.session->setNotes(clipId, notes);
    CHECK(f.clip(clipId).notes == notes);

    // Moved, a note keeps them.
    notes[0].pitch = 62;
    f.session->setNotes(clipId, notes);
    CHECK(f.clip(clipId).notes == notes);
    // Back to always and as written: nothing is left on the note.
    auto plain = notes;
    plain[0].chance = 1.0f;
    plain[0].velocitySpread = 0.0f;
    f.session->setNotes(clipId, plain);
    CHECK(f.clip(clipId).notes == plain);
    for (const auto* n : f.engineClip(clipId).getSequence().getNotes())
    {
        CHECK_FALSE(n->state.hasProperty(fd::engine::noteIds::chance));
        CHECK_FALSE(n->state.hasProperty(fd::engine::noteIds::spread));
    }
    f.session->undo();
    CHECK(f.clip(clipId).notes == notes);

    const auto seed = f.session->rerollChance(clipId, 1234u);
    CHECK(seed == 1234u);
    const auto project = f.root.getChildFile("chance.segno");
    REQUIRE(f.session->saveAs(project));
    Session reopened(f.engine);
    std::string error;
    REQUIRE(reopened.open(project, &error));
    reopened.updateSnapshotNow();
    const auto& c = reopened.snapshot()->tracks.front().clips.front();
    CHECK(c.notes == notes);
    CHECK(c.chanceSeed == 1234u);
}

TEST_CASE("chance (engine): a looped clip plays what noteFate decides, lap by lap; Re-roll changes it, "
          "undo brings it back",
          "[integration]")
{
    Fixture f;
    const auto clipId = f.session->createMidiClip(f.track(), 0.0, 8.0); // four bars at 120 BPM
    f.session->setNotes(clipId, chancyBar());
    REQUIRE(f.session->setClipLoop(clipId, {0.0, 2.0}).empty()); // a bar, four laps
    REQUIRE(f.clip(clipId).loop.on());
    f.session->rerollChance(clipId, 99u);

    const auto expected = [&](std::uint32_t seed)
    {
        // (lap, pitch) → MIDI velocity, for each note that plays.
        std::map<std::pair<int, int>, int> out;
        const auto notes = chancyBar();
        for (int lap = 0; lap < 4; ++lap)
            for (const auto& n : notes)
                if (const auto fate = fd::model::noteFate(n, n.start, lap, seed, clipId); fate.plays)
                    out[{lap, n.pitch}] = static_cast<int>(std::lround(fate.velocity * 127.0f));
        return out;
    };
    const auto played = [&]
    {
        std::map<std::pair<int, int>, int> out;
        for (const auto& [time, message] : f.noteOns(clipId))
            out[{static_cast<int>(time / 2.0 + 1e-6), message.getNoteNumber()}] = message.getVelocity();
        return out;
    };
    const auto first = played();
    CHECK(first == expected(99u));
    CHECK(first.size() > 4 + 4);    // the sure note on every lap, and some of the rest
    CHECK(first.size() < 4 * 8);    // not all of them
    CHECK(first != expected(100u)); // the seed matters

    // Re-roll: another seed, other notes; undo: the first again.
    const auto next = f.session->rerollChance(clipId);
    CHECK(next != 99u);
    CHECK(f.clip(clipId).chanceSeed == next);
    CHECK(played() == expected(next));
    f.session->undo();
    CHECK(f.clip(clipId).chanceSeed == 99u);
    CHECK(played() == first);
}

TEST_CASE("chance (engine): a render plays what was decided, as the same notes written out would",
          "[integration]")
{
    // The clip with chance, rendered; then a project with only the notes that were decided to
    // play, at their decided velocities, rendered the same way: the two match sample for sample.
    Fixture f;
    auto track = f.track();
    REQUIRE(f.session->addDevice(track, "fd:subtractive-synth") != 0);
    const auto clipId = f.session->createMidiClip(track, 0.0, 8.0);
    f.session->setNotes(clipId, chancyBar());
    REQUIRE(f.session->setClipLoop(clipId, {0.0, 2.0}).empty());
    f.session->rerollChance(clipId, 7u);
    const auto withChance = f.render(0.0, 8.5);

    std::vector<Note> decided;
    for (int lap = 0; lap < 4; ++lap)
        for (const auto& n : chancyBar())
            if (const auto fate = fd::model::noteFate(n, n.start, lap, 7u, clipId); fate.plays)
            {
                Note plain{n.pitch, n.start + lap * 4.0, n.length, fate.velocity};
                decided.push_back(plain);
            }
    f.session->newProject();
    track = f.track();
    REQUIRE(f.session->addDevice(track, "fd:subtractive-synth") != 0);
    const auto plainClip = f.session->createMidiClip(track, 0.0, 8.0);
    f.session->setNotes(plainClip, decided);
    const auto written = f.render(0.0, 8.5);

    REQUIRE(withChance.numSamples() == written.numSamples());
    float worst = 0.0f, loudest = 0.0f;
    for (int c = 0; c < withChance.numChannels(); ++c)
        for (int i = 0; i < withChance.numSamples(); ++i)
        {
            worst = std::max(worst, std::abs(withChance.at(c, i) - written.at(c, i)));
            loudest = std::max(loudest, std::abs(written.at(c, i)));
        }
    CHECK(loudest > 0.01f); // it played something
    CHECK(worst < 1e-5f);
}

TEST_CASE("controllers (engine): lanes play as MIDI events and read back as their points; recorded events "
          "are points that hold",
          "[integration]")
{
    Fixture f;
    const auto clipId = f.session->createMidiClip(f.track(), 0.0, 8.0);
    using fd::model::ControllerLane;
    namespace cc = fd::model::controllers;
    const ControllerLane mod{.controller = cc::modWheel, .points = {{0.0, 0.0f, 0.0f}, {2.0, 1.0f, 0.0f}}};
    const ControllerLane pedal{
        .controller = cc::sustain,
        .points = {{0.0, 1.0f, fd::model::kStepCurve}, {3.0, 0.0f, fd::model::kStepCurve}}};
    const ControllerLane bend{.controller = cc::pitchBend, .points = {{1.0, 0.5f, 0.0f}, {1.5, 1.0f, 0.0f}}};
    f.session->setControllers(clipId, {mod, pedal, bend});
    auto lanes = f.clip(clipId).controllers;
    REQUIRE(lanes.size() == 3);
    CHECK(lanes[0] == mod); // by controller: CC 1, CC 64, pitch bend
    CHECK(lanes[1] == pedal);
    CHECK(lanes[2] == bend);

    // Played: the mod wheel ramps up, the pedal steps, the wheel bends up from rest.
    auto& c = f.engineClip(clipId);
    const auto sequence =
        c.getSequenceLooped().exportToPlaybackMidiSequence(c, te::MidiList::TimeBase::seconds, false);
    std::vector<int> modValues, pedalValues, bendValues;
    for (const auto* e : sequence)
    {
        const auto& m = e->message;
        if (m.isControllerOfType(1))
            modValues.push_back(m.getControllerValue());
        else if (m.isControllerOfType(64))
            pedalValues.push_back(m.getControllerValue());
        else if (m.isPitchWheel())
            bendValues.push_back(m.getPitchWheelValue());
    }
    REQUIRE(modValues.size() > 100);
    CHECK(modValues.front() == 0);
    CHECK(modValues.back() == 127);
    CHECK(std::is_sorted(modValues.begin(), modValues.end()));
    CHECK(pedalValues == std::vector<int>{127, 0});
    REQUIRE_FALSE(bendValues.empty());
    CHECK(bendValues.front() == 8192);
    CHECK(bendValues.back() == 16383);

    // One lane replaced, the others kept; an empty lane removes its events.
    f.session->setControllers(clipId, {{.controller = cc::modWheel, .points = {}}});
    lanes = f.clip(clipId).controllers;
    REQUIRE(lanes.size() == 2);
    CHECK(lanes[0].controller == cc::sustain);
    f.session->undo();
    CHECK(f.clip(clipId).controllers.size() == 3);

    // Recorded events (no lane's marks): points that hold.
    c.getSequence().addControllerEvent(te::BeatPosition::fromBeats(0.5), 11, 100 << 7, nullptr);
    c.getSequence().addControllerEvent(te::BeatPosition::fromBeats(1.5), 11, 20 << 7, nullptr);
    lanes = f.clip(clipId).controllers;
    const auto expression =
        std::find_if(lanes.begin(), lanes.end(), [](const auto& l) { return l.controller == 11; });
    REQUIRE(expression != lanes.end());
    REQUIRE(expression->points.size() == 2);
    CHECK(expression->points[0].curve == fd::model::kStepCurve);
    CHECK(expression->points[0].value == Approx(100.0f / 127.0f));
}
