// The song's key (M3 P3, ADR 0056, docs/design/key-and-scale.md §18): set and removed through
// engine::Session, one undo step each (or one for a Key popover's choice), saved with the
// project, and handed to modules through TransportInfo::key.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "engine/SongKey.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <tracktion_engine/tracktion_engine.h>

using fd::music::Key;

namespace
{
std::atomic<std::uint32_t> heard{0};

const fd::DeviceInfo probeInfo{.id = "key-probe", .name = "Key probe", .category = "Test"};

/// A module that keeps the last key it was handed, and passes its input through.
class KeyProbe final : public fd::Device
{
public:
    KeyProbe() : Device(probeInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(fd::ProcessContext& context) noexcept override
    {
        heard.store(fd::engine::packKey(context.transport.key));
        for (int c = 0; c < context.output.numChannels; ++c)
            if (c < context.input.numChannels && context.output.channel(c) != context.input.channel(c))
                std::copy_n(context.input.channel(c), context.numSamples, context.output.channel(c));
    }
};

const fd::DeviceFactory probeFactory{[]() -> const fd::DeviceInfo& { return probeInfo; },
                                     []() -> std::unique_ptr<fd::Device>
                                     { return std::make_unique<KeyProbe>(); }};

std::optional<Key> keyOf(fd::engine::Session& session)
{
    session.updateSnapshotNow();
    return session.snapshot()->keys.first();
}

/// Renders a moment of the mix offline, so every module processes on the project's copy.
void renderOnce(fd::engine::Session& session)
{
    juce::TemporaryFile out(".wav");
    bool done = false;
    auto job = session.render({0.0, 0.25, std::nullopt, session.sampleRate()}, out.getFile(),
                              [&](const std::string&) { done = true; });
    REQUIRE(job != nullptr);
    for (int i = 0; i < 1000 && !done; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(done);
}
} // namespace

TEST_CASE("the song's key: set and removed, one undo step each (ADR 0056)", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::Session session(engine);

    CHECK_FALSE(keyOf(session).has_value()); // a new project has none
    CHECK_FALSE(session.canUndo());

    REQUIRE(session.setKey(Key{9, "minor"}));
    CHECK(keyOf(session) == Key{9, "minor"});
    CHECK(session.undoDescription() == "Set key to A minor");

    // The same key again changes nothing and adds no step.
    REQUIRE(session.setKey(Key{9 + 12, "minor"}));
    session.undo();
    CHECK_FALSE(keyOf(session).has_value());
    CHECK_FALSE(session.canUndo());
    session.redo();
    CHECK(keyOf(session) == Key{9, "minor"});

    REQUIRE(session.setKey(Key{1, "dorian"}));
    CHECK(session.undoDescription() == "Set key to C♯ Dorian");
    REQUIRE(session.setKey(std::nullopt));
    CHECK(session.undoDescription() == "Remove the key");
    CHECK_FALSE(keyOf(session).has_value());
    session.undo();
    CHECK(keyOf(session) == Key{1, "dorian"});

    // tracktion's own fields follow, for its features and ARA's key signatures.
    auto* setting = session.impl().edit->pitchSequence.getPitch(0);
    REQUIRE(setting != nullptr);
    CHECK(setting->getPitch() % 12 == 1);
    CHECK(setting->getScale() == tracktion::Scale::dorian);
    CHECK(setting->accidentalsSharp.get());
    REQUIRE(session.setKey(Key{3, "minor"}));
    CHECK_FALSE(setting->accidentalsSharp.get()); // E♭ minor
    CHECK(setting->getScale() == tracktion::Scale::minor);

    // A scale this build doesn't know is refused.
    CHECK_FALSE(session.setKey(Key{0, "no-such-scale"}));
    CHECK(keyOf(session) == Key{3, "minor"});
}

TEST_CASE("a Key popover's choice is one undo step, or none when it ends where it began", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::Session session(engine);

    {
        fd::engine::Session::KeyChoice choice(session);
        session.setKey(Key{2, "major"});
        session.setKey(Key{2, "minor"});
        session.setKey(Key{4, "minor"});
    }
    CHECK(keyOf(session) == Key{4, "minor"});
    CHECK(session.undoDescription() == "Set key to E minor");
    session.undo();
    CHECK_FALSE(keyOf(session).has_value());
    CHECK_FALSE(session.canUndo()); // all three were one step
    session.redo();
    CHECK(keyOf(session) == Key{4, "minor"});

    // Changed, then changed back: no step at all.
    session.undo();
    {
        fd::engine::Session::KeyChoice choice(session);
        session.setKey(Key{7, "major"});
        session.setKey(std::nullopt);
    }
    CHECK_FALSE(keyOf(session).has_value());
    CHECK_FALSE(session.canUndo());

    // Another command in between closes the step: three steps.
    {
        fd::engine::Session::KeyChoice choice(session);
        session.setKey(Key{7, "major"});
        session.setTempo(100.0);
        session.setKey(Key{9, "major"});
    }
    CHECK(session.undoDescription() == "Set key to A major");
    session.undo();
    CHECK(keyOf(session) == Key{7, "major"});
    session.undo(); // the tempo
    session.undo();
    CHECK_FALSE(keyOf(session).has_value());
    CHECK_FALSE(session.canUndo());

    // Back to where the choice began, but after another command: the last step still counts,
    // since it didn't begin there.
    {
        fd::engine::Session::KeyChoice choice(session);
        session.setKey(Key{7, "major"});
        session.setTempo(110.0);
        session.setKey(std::nullopt);
    }
    CHECK_FALSE(keyOf(session).has_value());
    CHECK(session.undoDescription() == "Remove the key");
    session.undo();
    CHECK(keyOf(session) == Key{7, "major"});
}

TEST_CASE("the song's key is saved with the project; older projects open with none", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    juce::TemporaryFile withKey(fd::engine::Session::fileExtension),
        without(fd::engine::Session::fileExtension), newer(fd::engine::Session::fileExtension);
    {
        fd::engine::Session session(engine);
        REQUIRE(session.saveAs(without.getFile())); // as a project from before this step: no key
        REQUIRE(session.setKey(Key{6, "phrygian-dominant"}));
        REQUIRE(session.saveAs(withKey.getFile()));
        // A scale from a newer build.
        session.impl().edit->pitchSequence.getPitch(0)->state.setProperty("segnoScale", "some-future-scale",
                                                                          nullptr);
        REQUIRE(session.saveAs(newer.getFile()));
    }
    {
        fd::engine::Session session(engine);
        REQUIRE(session.open(withKey.getFile()));
        CHECK(keyOf(session) == Key{6, "phrygian-dominant"});
    }
    {
        fd::engine::Session session(engine);
        REQUIRE(session.open(without.getFile()));
        CHECK_FALSE(keyOf(session).has_value());
    }
    {
        fd::engine::Session session(engine);
        REQUIRE(session.open(newer.getFile()));
        CHECK_FALSE(keyOf(session).has_value());
        // The project keeps what it holds.
        CHECK(session.impl().edit->pitchSequence.getPitch(0)->state["segnoScale"].toString() ==
              "some-future-scale");
    }
}

TEST_CASE("modules hear the song's key through TransportInfo::key (ADR 0056)", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevice(engine, probeFactory);
    fd::engine::Session session(engine);
    const auto id = session.addDevice(session.snapshot()->master.id, "fd:key-probe");
    REQUIRE(id != 0);
    auto* plugin = dynamic_cast<fd::engine::DevicePlugin*>(session.impl().plugin(id));
    REQUIRE(plugin != nullptr);

    const fd::KeyInfo none{};
    const fd::KeyInfo aMinor{9, fd::music::notes(Key{9, "minor"})};

    // Live: the module's copy follows every change, undo included.
    CHECK(plugin->songKey() == none);
    REQUIRE(session.setKey(Key{9, "minor"}));
    CHECK(plugin->songKey() == aMinor);
    CHECK(plugin->songKey().contains(69 + 12));
    CHECK_FALSE(plugin->songKey().contains(68));
    session.undo();
    CHECK(plugin->songKey() == none);
    session.redo();

    // In a render, on the project's copy, the module processes with it.
    heard = 0xffffffffu;
    renderOnce(session);
    CHECK(fd::engine::unpackKey(heard.load()) == aMinor);
    REQUIRE(session.setKey(std::nullopt));
    heard = 0xffffffffu;
    renderOnce(session);
    CHECK(fd::engine::unpackKey(heard.load()) == none);
}

TEST_CASE("keys are found from the song's MIDI notes", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::Session session(engine);
    const auto track = session.snapshot()->tracks[0].id;
    const auto clip = session.createMidiClip(track, 0.0, 4.0);
    REQUIRE(clip != 0);

    CHECK(session.detectKeys().empty()); // no notes yet

    // An A minor phrase with its raised seventh, then i, V7, i.
    std::vector<fd::model::Note> notes;
    double at = 0.0;
    for (int p : {69, 71, 72, 74, 76, 77, 80, 81})
    {
        notes.push_back({p, at, 0.5});
        at += 0.5;
    }
    for (int p : {57, 69, 72, 76})
        notes.push_back({p, 4.0, 1.0});
    for (int p : {64, 68, 71, 74})
        notes.push_back({p, 5.0, 1.0});
    for (int p : {57, 69, 72, 76})
        notes.push_back({p, 6.0, 2.0});
    session.setNotes(clip, notes);
    session.updateSnapshotNow();

    const auto keys = session.detectKeys();
    REQUIRE(!keys.empty());
    CHECK(keys.front().key == Key{9, "minor"});
    CHECK(session.detectKeys({clip}).front().key == Key{9, "minor"});
    CHECK(session.detectKeys({clip + 1000}).empty()); // no such clip: no notes

    // A muted clip isn't judged.
    session.setClipsMuted({clip}, true);
    session.updateSnapshotNow();
    CHECK(session.detectKeys().empty());
}
