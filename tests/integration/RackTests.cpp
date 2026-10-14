// Racks (M3 P2, docs/design/racks.md §15, ADR 0056), through engine::Session: grouping sounds as
// the modules did and undoes whole; chains side by side, each with its mixer row; modules into,
// along and out of a rack; macros mapped From to To along their Curve, automated like any
// parameter; racks saved with the project, copied with their track, and instrument racks.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"
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
constexpr double kSeconds = 1.0;

struct Racks
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-rack-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-racks-" + juce::Uuid().toString());
    AudioBuffer input = fd::testkit::whiteNoise(48000, 3, 0.2f, 2);
    std::uint64_t t = 0;

    Racks()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        (void)root.createDirectory();
        const auto file = root.getChildFile("noise.wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), input, 48000.0));
        t = track(0);
        REQUIRE(session->insertAudioFile(t, file, 0.0) != 0);
    }

    ~Racks()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// A copy (snapshots are replaced).
    model::TrackInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* c = session->snapshot()->channel(id);
        REQUIRE(c != nullptr);
        return *c;
    }

    /// A module as the snapshot has it, in a rack too.
    model::DeviceSlot slot(std::uint64_t id)
    {
        session->updateSnapshotNow();
        for (const auto& c : session->snapshot()->tracks)
            if (const auto* d = model::findDevice(c.devices, id))
                return *d;
        FAIL("no module " << id);
        return {};
    }

    model::RackInfo rack(std::uint64_t id)
    {
        const auto s = slot(id);
        REQUIRE(s.rack != nullptr);
        return *s.rack;
    }

    std::vector<std::uint64_t> chainOf(std::uint64_t channel)
    {
        std::vector<std::uint64_t> out;
        for (const auto& d : info(channel).devices)
            out.push_back(d.id);
        return out;
    }

    static std::vector<std::uint64_t> ids(const model::RackChain& chain)
    {
        std::vector<std::uint64_t> out;
        for (const auto& d : chain.devices)
            out.push_back(d.id);
        return out;
    }

    /// Sets one of our module's parameters by its text ("-6 dB"), through its host binding.
    void setParam(std::uint64_t device, const std::string& paramId, const std::string& text)
    {
        auto handle = session->openDevice(device);
        REQUIRE(handle != nullptr);
        auto& context = handle->context();
        const auto& params = context.info.params;
        const auto it =
            std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.id == paramId; });
        REQUIRE(it != params.end());
        const auto plain = it->fromText(text);
        REQUIRE(plain);
        context.params.setPlain(static_cast<size_t>(it - params.begin()), *plain);
    }

    float normalised(const std::string& device, const std::string& paramId, float plain)
    {
        const auto& info = fd::findDevice(device)->info();
        for (const auto& p : info.params)
            if (p.id == paramId)
                return p.toNormalized(plain);
        FAIL("no parameter " << paramId);
        return 0.0f;
    }

    /// The mix, rendered offline.
    AudioBuffer render()
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({0.0, kSeconds, std::nullopt, 48000.0}, out.getFile(),
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

    /// The input's level change through the mix, in dB.
    double gainDb() { return fd::testkit::rmsDb(render()) - fd::testkit::rmsDb(input); }
};
} // namespace

TEST_CASE("racks: grouping modules into a rack sounds as they did, and ungrouping and undo put them back",
          "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto saturator = s.addDevice(r.t, "fd:saturator");
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto limiter = s.addDevice(r.t, "fd:limiter");
    r.setParam(saturator, "drive", "18 dB");
    r.setParam(utility, "gain", "-6 dB");
    const auto before = r.render();

    // The first two, in their order, where the first was; one undo step.
    const auto rack = s.groupIntoRack({utility, saturator});
    REQUIRE(rack != 0);
    CHECK(s.undoDescription() == "Group into rack");
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{rack, limiter});
    {
        const auto info = r.rack(rack);
        REQUIRE(info.chains.size() == 1);
        CHECK(Racks::ids(info.chains[0]) == std::vector<std::uint64_t>{saturator, utility});
        CHECK(info.chains[0].name == "Chain 1");
        CHECK(info.macros[0].name == "Macro 1");
        CHECK(info.macros[7].name == "Macro 8");
        CHECK(info.macros[0].value == 0.0f); // a knob mapped From where it is stays there
        CHECK(r.slot(rack).name == "Rack");
        CHECK(r.slot(rack).deviceId == "rack");
        CHECK_FALSE(r.slot(rack).instrument);
    }
    CHECK(r.info(r.t).kind == model::TrackInfo::Kind::Audio); // an effect rack: still an audio track
    INFO("grouped against before: " << fd::testkit::nullResidualDb(r.render(), before) << " dB");
    CHECK(fd::testkit::nullResidualDb(r.render(), before) < -100.0);
    // Its modules keep their ids: they're reached as before.
    CHECK(s.placeOf(utility)->rack == rack);
    CHECK(s.placeOf(utility)->index == 1);

    s.undo();
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{saturator, utility, limiter});
    CHECK(fd::testkit::nullResidualDb(r.render(), before) < -100.0);
    s.redo();
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{rack, limiter});

    // Ungrouped: the shown chain's modules where the rack was; one undo step puts the rack back whole.
    CHECK(s.ungroupRack(rack) == std::vector<std::uint64_t>{saturator, utility});
    CHECK(s.undoDescription() == "Ungroup rack");
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{saturator, utility, limiter});
    CHECK(fd::testkit::nullResidualDb(r.render(), before) < -100.0);
    s.undo();
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{rack, limiter});
    CHECK(Racks::ids(r.rack(rack).chains[0]) == std::vector<std::uint64_t>{saturator, utility});
    CHECK(fd::testkit::nullResidualDb(r.render(), before) < -100.0);

    // Not a rack in a rack, nor modules of two chains.
    CHECK(s.groupIntoRack({rack}) == 0);
    CHECK(s.groupIntoRack({}) == 0);
}

TEST_CASE("racks: chains side by side sum; an empty one passes the input; on, solo, volume and pan are heard",
          "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto rack = s.addRack(r.t);
    REQUIRE(rack != 0);
    CHECK(s.undoDescription() == "Add rack");
    CHECK(r.rack(rack).chains.size() == 1);
    CHECK(r.gainDb() == Approx(0.0).margin(0.05)); // an empty chain passes its input

    CHECK(s.addRackChain(rack) == 1);
    CHECK(r.rack(rack).shownChain == 1); // the new chain shows
    CHECK(r.rack(rack).chains[1].name == "Chain 2");
    CHECK(r.gainDb() == Approx(6.02).margin(0.05)); // two of it

    // Off: heard no more; an undo step.
    REQUIRE(s.setRackChain(rack, 1, {.on = false}));
    CHECK(s.undoDescription() == "Chain on/off");
    CHECK_FALSE(r.rack(rack).chains[1].on);
    CHECK(r.gainDb() == Approx(0.0).margin(0.05));
    s.undo();
    CHECK(r.gainDb() == Approx(6.02).margin(0.05));

    // Solo: only the soloed chain (an undo step: it rewires the rack).
    REQUIRE(s.setRackChain(rack, 0, {.solo = true}));
    CHECK(s.undoDescription() == "Solo chain");
    CHECK(r.rack(rack).chains[0].solo);
    CHECK(r.gainDb() == Approx(0.0).margin(0.05));
    REQUIRE(s.setRackChain(rack, 0, {.solo = false}));

    // A chain's volume and name.
    REQUIRE(s.setRackChain(rack, 1, {.name = "Dry", .volumeDb = model::minusInfinityDb}));
    CHECK(r.rack(rack).chains[1].name == "Dry");
    CHECK(r.gainDb() == Approx(0.0).margin(0.05));
    REQUIRE(s.setRackChain(rack, 1, {.volumeDb = 0.0f}));
    // Pan: a balance, as a track's.
    REQUIRE(s.setRackChain(rack, 0, {.pan = -1.0f}));
    REQUIRE(s.setRackChain(rack, 1, {.pan = -1.0f}));
    const auto out = r.render();
    CHECK(fd::testkit::rmsDb(out, 1) < -60.0); // nothing on the right

    // At most four; a rack keeps one.
    CHECK(s.addRackChain(rack) == 2);
    CHECK(s.addRackChain(rack) == 3);
    CHECK(s.addRackChain(rack) == -1);
    CHECK(s.removeRackChain(rack, 3));
    CHECK(s.removeRackChain(rack, 2));
    CHECK(s.removeRackChain(rack, 1));
    CHECK_FALSE(s.removeRackChain(rack, 0));
    CHECK(r.rack(rack).chains.size() == 1);
}

TEST_CASE("racks: modules go into, along and out of a rack's chains, never a rack into a rack",
          "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto eq = s.addDevice(r.t, "fd:eq");
    const auto rack = s.addRack(r.t);
    const auto delay = s.addDevice(r.t, "fd:delay");
    REQUIRE(s.addRackChain(rack) == 1);

    // Into the second chain, then into the first, ahead of nothing.
    REQUIRE(s.moveDevice(delay, {.rack = rack, .chain = 1}));
    CHECK(s.undoDescription() == "Move module");
    REQUIRE(s.moveDevice(eq, {.rack = rack, .chain = 0}));
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{rack});
    CHECK(Racks::ids(r.rack(rack).chains[0]) == std::vector<std::uint64_t>{eq});
    CHECK(Racks::ids(r.rack(rack).chains[1]) == std::vector<std::uint64_t>{delay});

    // Added straight into a chain, before the Delay; then the Delay before it (along the chain).
    const auto chorus = s.addDevice(Session::ChainPlace{.rack = rack, .chain = 1, .index = 0}, "fd:chorus");
    REQUIRE(chorus != 0);
    CHECK(Racks::ids(r.rack(rack).chains[1]) == std::vector<std::uint64_t>{chorus, delay});
    REQUIRE(s.moveDevice(delay, {.rack = rack, .chain = 1, .index = 0}));
    CHECK(Racks::ids(r.rack(rack).chains[1]) == std::vector<std::uint64_t>{delay, chorus});
    s.moveDevice(delay, 1); // the plain move, to where it ends up, works in a rack too
    CHECK(Racks::ids(r.rack(rack).chains[1]) == std::vector<std::uint64_t>{chorus, delay});

    // Out, after the rack in the track's chain.
    REQUIRE(s.moveDevice(chorus, {.channel = r.t, .index = 1}));
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{rack, chorus});
    CHECK(s.placeOf(chorus)->rack == 0);

    // A rack never goes into a rack, its own or another.
    const auto other = s.addRack(r.t);
    CHECK_FALSE(s.moveDevice(other, {.rack = rack, .chain = 0}));
    CHECK(s.addDevice(Session::ChainPlace{.rack = rack, .chain = 5}, "fd:eq") == 0);

    // Removing a module closes its chain up; removing the rack takes its modules with it.
    s.removeDevice(delay);
    CHECK(r.rack(rack).chains[1].devices.empty());
    CHECK(r.gainDb() > -100.0); // still wired: sound comes through
    s.removeDevice(rack);
    CHECK(r.chainOf(r.t) == std::vector<std::uint64_t>{chorus, other});
    s.undo();
    CHECK(Racks::ids(r.rack(rack).chains[0]) == std::vector<std::uint64_t>{eq});
}

TEST_CASE("racks: a macro sets what it's mapped to, From to To along its Curve, and is automated like any "
          "parameter",
          "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(rack != 0);
    const float from = r.normalised("utility", "gain", -24.0f), to = r.normalised("utility", "gain", 0.0f);

    REQUIRE(s.mapMacro(rack, 1, utility, "gain", from, to));
    CHECK(s.undoDescription() == "Map to macro");
    {
        const auto info = r.rack(rack);
        REQUIRE(info.macros[1].mappings.size() == 1);
        const auto& m = info.macros[1].mappings[0];
        CHECK(m.module == utility);
        CHECK(m.param == "gain");
        CHECK(m.paramName == "Gain");
        CHECK(m.from == Approx(from).margin(1e-4));
        CHECK(m.to == Approx(to).margin(1e-4));
        CHECK(m.curve == Approx(0.0f).margin(1e-4));
    }
    REQUIRE(s.setMacro(rack, 1, 0.0f));
    CHECK(r.gainDb() == Approx(-24.0).margin(0.2));
    REQUIRE(s.setMacro(rack, 1, 1.0f));
    CHECK(r.gainDb() == Approx(0.0).margin(0.2));
    REQUIRE(s.setMacro(rack, 1, 0.5f));
    const double straight = r.gainDb();
    CHECK(straight ==
          Approx(fd::findDevice("utility")->info().params[0].fromNormalized(from + 0.5f * (to - from)))
              .margin(0.3));

    // Bent all the way, the curve survives (the engine leaves out its own default when adding).
    REQUIRE(s.mapMacro(rack, 1, utility, "gain", from, to, 1.0f));
    CHECK(r.rack(rack).macros[1].mappings.at(0).curve == Approx(1.0f).margin(1e-4));
    const double late = r.gainDb();
    REQUIRE(s.mapMacro(rack, 1, utility, "gain", from, to, -1.0f));
    const double early = r.gainDb();
    CHECK(late < straight - 1.0);
    CHECK(early > straight + 1.0);
    // The editor's curve is the engine's.
    CHECK(fd::findDevice("utility")->info().params[0].fromNormalized(
              model::macroMapped(0.5f, from, to, 1.0f)) == Approx(late).margin(0.3));

    // Another macro takes the parameter: one macro a parameter.
    REQUIRE(s.mapMacro(rack, 2, utility, "gain", from, to));
    CHECK(r.rack(rack).macros[1].mappings.empty());
    CHECK(r.rack(rack).macros[2].mappings.size() == 1);
    // A macro's name.
    REQUIRE(s.setMacro(rack, 2, std::nullopt, "Cut"));
    CHECK(r.rack(rack).macros[2].name == "Cut");

    // Automated: its lane moves what it's mapped to.
    const auto choices = s.automatableParameters(r.t);
    const auto key = "m" + std::to_string(rack) + "/macro3";
    CHECK(std::any_of(choices.begin(), choices.end(),
                      [&](const auto& c) { return c.key == key && c.name == "Cut"; }));
    CHECK(std::any_of(choices.begin(), choices.end(),
                      [&](const auto& c) { return c.key == "m" + std::to_string(utility) + "/gain"; }));
    s.setMacro(rack, 2, 1.0f);
    const auto lane = s.addAutomationLane(Session::ParamTarget{r.t, key});
    REQUIRE(lane != 0);
    REQUIRE(s.setLanePoints(lane, {{0.0, 0.0f, 0.0f}, {kSeconds, 0.0f, 0.0f}}));
    {
        auto h = s.openRack(rack);
        CHECK(h->context().params.automationState(2) == fd::ui::AutomationState::On);
    }
    CHECK(r.gainDb() == Approx(-24.0).margin(0.3)); // the lane, not where the macro was set

    // Unmapped: set by hand again.
    s.removeAutomationLane(lane);
    REQUIRE(s.unmapMacro(utility, "gain"));
    CHECK(r.rack(rack).macros[2].mappings.empty());
    CHECK_FALSE(s.mapMacro(rack, 0, utility, "no-such-parameter", 0.0f, 1.0f));
}

TEST_CASE("racks: a rack's modules leave its macros behind; its own panel reads its macros", "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(s.mapMacro(rack, 0, utility, "gain", 0.2f, 0.8f));
    REQUIRE(s.setMacro(rack, 0, 0.5f));
    // Its panel: the macros as parameters, 0 .. 100 %, then Morph (racks.md §18.3).
    auto handle = s.openRack(rack);
    REQUIRE(handle != nullptr);
    CHECK(handle->deviceId() == "rack");
    REQUIRE(handle->context().info.params.size() == 9);
    CHECK(handle->context().info.params[8].id == "morph");
    CHECK(handle->context().params.getPlain(0) == Approx(50.0f).margin(0.01f));
    handle->context().params.setPlain(0, 25.0f);
    CHECK(r.rack(rack).macros[0].value == Approx(0.25f).margin(1e-3));
    CHECK(s.openRack(utility) == nullptr);

    // Out of the rack: unmapped, its gain where the macro had it.
    REQUIRE(s.moveDevice(utility, {.channel = r.t, .index = 1}));
    CHECK(r.rack(rack).macros[0].mappings.empty());
    auto u = s.openDevice(utility);
    CHECK(
        u->context().params.getPlain(0) ==
        Approx(fd::findDevice("utility")->info().params[0].fromNormalized(0.2f + 0.25f * 0.6f)).margin(0.05));
}

TEST_CASE("racks: a project's racks are saved and opened, and copied with their track", "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto saturator = s.addDevice(r.t, "fd:saturator");
    r.setParam(saturator, "drive", "12 dB");
    const auto rack = s.groupIntoRack({saturator});
    REQUIRE(s.addRackChain(rack) == 1);
    REQUIRE(s.setRackChain(rack, 1, {.name = "Dry", .volumeDb = -6.0f}));
    REQUIRE(s.mapMacro(rack, 3, saturator, "drive", 0.1f, 0.9f, 0.5f));
    REQUIRE(s.setMacro(rack, 3, 0.4f, "Heat"));
    REQUIRE(s.renameRack(rack, "Parallel heat"));
    const auto rendered = r.render();
    const auto before = r.rack(rack);

    const auto file = r.root.getChildFile("Racks").getChildFile("Racks.tracktionedit");
    REQUIRE(s.saveAs(file));
    REQUIRE(s.open(file));
    const auto t = r.track(0);
    const auto chain = r.info(t).devices;
    REQUIRE(chain.size() == 1);
    REQUIRE(chain[0].rack != nullptr);
    const auto& after = *chain[0].rack;
    CHECK(chain[0].name == "Parallel heat");
    REQUIRE(after.chains.size() == 2);
    CHECK(after.chains[1].name == "Dry");
    CHECK(after.chains[1].volumeDb == Approx(-6.0f).margin(0.01f));
    CHECK(after.macros[3].name == "Heat");
    CHECK(after.macros[3].value == Approx(0.4f).margin(1e-4));
    REQUIRE(after.macros[3].mappings.size() == 1);
    CHECK(after.macros[3].mappings[0].curve == Approx(before.macros[3].mappings[0].curve).margin(1e-4));
    CHECK(fd::testkit::nullResidualDb(r.render(), rendered) < -100.0);

    // A copy of the track has a rack of its own: new ids, and its macros move only it.
    const auto copy = s.duplicateTrack(t);
    REQUIRE(copy != 0);
    const auto copied = r.info(copy).devices;
    REQUIRE(copied.size() == 1);
    REQUIRE(copied[0].rack != nullptr);
    CHECK(copied[0].id != chain[0].id);
    CHECK(copied[0].rack->chains[0].devices.at(0).id != chain[0].rack->chains[0].devices.at(0).id);
    REQUIRE(s.setMacro(copied[0].id, 3, 1.0f));
    CHECK(r.rack(chain[0].id).macros[3].value == Approx(0.4f).margin(1e-4));
    CHECK(r.rack(copied[0].id).macros[3].mappings.size() == 1);
}

TEST_CASE("racks: an instrument rack plays notes and makes its track an instrument track", "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto t = r.track(1);
    const auto synth = s.addDevice(t, "fd:subtractive-synth");
    const auto rack = s.groupIntoRack({synth});
    REQUIRE(rack != 0);
    CHECK(r.slot(rack).instrument);
    CHECK(r.info(t).kind == model::TrackInfo::Kind::Instrument);
    CHECK(r.info(t).instrument == "Rack");
    const auto clip = s.createMidiClip(t, 0.0, kSeconds);
    s.setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 1.5, .velocity = 0.8f}});
    s.setMute(r.t, true);
    CHECK(fd::testkit::rmsDb(r.render()) > -40.0);
}

TEST_CASE("racks: a rack file loads as it was saved, the same sound; a newer module is refused, a plugin not "
          "installed said",
          "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto saturator = s.addDevice(r.t, "fd:saturator");
    const auto utility = s.addDevice(r.t, "fd:utility");
    r.setParam(saturator, "drive", "15 dB");
    const auto rack = s.groupIntoRack({saturator, utility});
    REQUIRE(s.addRackChain(rack) == 1);
    REQUIRE(s.setRackChain(rack, 1, {.name = "Dry", .volumeDb = -3.0f, .pan = 0.25f}));
    REQUIRE(s.setRackChain(rack, 1, {.solo = true})); // never kept in the file
    REQUIRE(s.setRackChain(rack, 1, {.solo = false}));
    REQUIRE(s.mapMacro(rack, 2, utility, "gain", 0.3f, 0.7f, -0.5f));
    REQUIRE(s.setMacro(rack, 2, 0.6f, "Push"));
    REQUIRE(s.renameRack(rack, "Heat"));
    const auto first = r.render();

    const auto file = r.root.getChildFile("Heat.segnorack");
    std::string error;
    REQUIRE(s.saveRack(rack, file, &error));
    CHECK(error.empty());
    const auto info = Session::readRackFile(file);
    REQUIRE(info);
    CHECK(info->name == "Heat");
    CHECK_FALSE(info->instrument);

    // Loaded in place of the original: the same rack, the same sound.
    s.removeDevice(rack);
    const auto loaded = s.loadRack(file, {.channel = r.t});
    REQUIRE(loaded.rack != 0);
    CHECK(loaded.missing.empty());
    CHECK(s.undoDescription() == "Add rack Heat");
    const auto again = r.rack(loaded.rack);
    CHECK(r.slot(loaded.rack).name == "Heat");
    REQUIRE(again.chains.size() == 2);
    CHECK(again.chains[1].name == "Dry");
    CHECK(again.chains[1].volumeDb == Approx(-3.0f).margin(0.01f));
    CHECK_FALSE(again.chains[1].solo);
    CHECK(again.macros[2].name == "Push");
    REQUIRE(again.macros[2].mappings.size() == 1);
    CHECK(again.macros[2].mappings[0].from == Approx(0.3f).margin(1e-4));
    CHECK(again.macros[2].mappings[0].curve == Approx(-0.5f).margin(1e-4));
    INFO("loaded against saved: " << fd::testkit::nullResidualDb(r.render(), first) << " dB");
    CHECK(fd::testkit::nullResidualDb(r.render(), first) < -100.0);
    // Saved again: the same file, byte for byte.
    const auto second = r.root.getChildFile("Heat 2.segnorack");
    REQUIRE(s.saveRack(loaded.rack, second));
    CHECK(second.loadFileAsString() == file.loadFileAsString());

    // A module newer than this Segno: refused whole, by name.
    auto json = nlohmann::json::parse(file.loadFileAsString().toStdString());
    json["chains"][0]["modules"][0]["state"]["stateVersion"] = 999;
    const auto newer = r.root.getChildFile("Newer.segnorack");
    REQUIRE(newer.replaceWithText(juce::String(json.dump())));
    const auto refused = s.loadRack(newer, {.channel = r.t});
    CHECK(refused.rack == 0);
    CHECK(refused.error.find("newer than this Segno") != std::string::npos);
    // A plugin that isn't installed: the rest loads, and it's named.
    json = nlohmann::json::parse(file.loadFileAsString().toStdString());
    json["chains"][0]["modules"].push_back(
        {{"name", "Shimmer Verb"}, {"plugin", "VST3-Shimmer-0-0"}, {"state", "<PLUGIN/>"}});
    const auto missing = r.root.getChildFile("Missing.segnorack");
    REQUIRE(missing.replaceWithText(juce::String(json.dump())));
    const auto partial = s.loadRack(missing, {.channel = r.t});
    REQUIRE(partial.rack != 0);
    CHECK(partial.missing == std::vector<std::string>{"Shimmer Verb"});
    CHECK(r.rack(partial.rack).chains[0].devices.size() == 2);
}

// ---- step 2: snapshots, Morph, locks and Vary (racks.md §18) -----------------------------------

TEST_CASE("racks: snapshots store and recall the macros, the current one marked changed; locked macros and "
          "those following a lane stay; each edit one undo step",
          "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(rack != 0);
    REQUIRE(s.mapMacro(rack, 0, utility, "gain", 0.2f, 0.8f));
    REQUIRE(s.setMacro(rack, 0, 0.25f));
    REQUIRE(s.setMacro(rack, 1, 0.5f));

    // Stored: where the macros are, named, the current one; the first two become A and B.
    CHECK(s.storeSnapshot(rack) == 0);
    CHECK(s.undoDescription() == "Store snapshot");
    REQUIRE(s.setMacro(rack, 0, 0.75f));
    REQUIRE(s.setMacro(rack, 1, 0.1f));
    CHECK(model::snapshotChanged(r.rack(rack)));
    CHECK(s.storeSnapshot(rack) == 1);
    {
        const auto info = r.rack(rack);
        REQUIRE(info.snapshots.size() == 2);
        CHECK(info.snapshots[0].name == "Snapshot 1");
        CHECK(info.snapshots[1].name == "Snapshot 2");
        CHECK(info.snapshots[0].macros[0] == Approx(0.25f).margin(1e-4));
        CHECK(info.snapshots[1].macros[1] == Approx(0.1f).margin(1e-4));
        CHECK(info.currentSnapshot == 1);
        CHECK(info.morph.a == 0);
        CHECK(info.morph.b == 1);
        CHECK_FALSE(info.morph.on);
        CHECK_FALSE(model::snapshotChanged(info));
    }

    // Recalled: the macros where it has them, and what they map with them.
    REQUIRE(s.recallSnapshot(rack, 0));
    CHECK(s.undoDescription() == "Recall snapshot");
    {
        const auto info = r.rack(rack);
        CHECK(info.currentSnapshot == 0);
        CHECK(info.macros[0].value == Approx(0.25f).margin(1e-4));
        CHECK(info.macros[1].value == Approx(0.5f).margin(1e-4));
    }
    CHECK(
        r.gainDb() ==
        Approx(fd::findDevice("utility")->info().params[0].fromNormalized(0.2f + 0.25f * 0.6f)).margin(0.3));
    s.undo(); // the recall
    CHECK(r.rack(rack).macros[0].value == Approx(0.75f).margin(1e-4));

    // A locked macro stays; so does one following its lane.
    REQUIRE(s.lockMacro(rack, 1, true));
    CHECK(r.rack(rack).macros[1].locked);
    const auto lane = s.addAutomationLane(Session::ParamTarget{r.t, "m" + std::to_string(rack) + "/macro1"});
    REQUIRE(lane != 0);
    REQUIRE(s.setLanePoints(lane, {{0.0, 0.9f, 0.0f}, {kSeconds, 0.9f, 0.0f}}));
    s.updateSnapshotNow();
    CHECK(r.rack(rack).macros[0].automated);
    REQUIRE(s.recallSnapshot(rack, 0));
    {
        const auto info = r.rack(rack);
        CHECK(info.macros[1].value == Approx(0.1f).margin(1e-4)); // locked: as it was
        CHECK(info.macros[0].automated);
    }
    CHECK(r.gainDb() == Approx(fd::findDevice("utility")->info().params[0].fromNormalized(0.2f + 0.9f * 0.6f))
                            .margin(0.3)); // the lane's, not the snapshot's
    s.removeAutomationLane(lane);

    // Updated, renamed, set directly, removed.
    REQUIRE(s.setMacro(rack, 0, 0.6f));
    CHECK(s.storeSnapshot(rack, 0) == 0);
    CHECK(s.undoDescription() == "Update snapshot");
    CHECK(r.rack(rack).snapshots[0].macros[0] == Approx(0.6f).margin(1e-4));
    REQUIRE(s.setSnapshot(rack, 0, std::string("Verse")));
    CHECK(r.rack(rack).snapshots[0].name == "Verse");
    REQUIRE(s.setSnapshot(rack, 1, std::nullopt, std::array<float, 8>{1.0f, 2.0f}));
    CHECK(r.rack(rack).snapshots[1].macros[1] == Approx(1.0f)); // kept within its travel
    REQUIRE(s.removeSnapshot(rack, 0));
    {
        const auto info = r.rack(rack);
        REQUIRE(info.snapshots.size() == 1);
        CHECK(info.snapshots[0].name == "Snapshot 2");
        CHECK(info.morph.a == -1); // it was A
        CHECK(info.morph.b == 0);  // B moved up with it
        CHECK(info.currentSnapshot == -1);
    }
    s.undo();
    CHECK(r.rack(rack).snapshots.size() == 2);

    // Eight at most.
    for (int i = 2; i < model::RackInfo::maxSnapshots; ++i)
        CHECK(s.storeSnapshot(rack) == i);
    CHECK(s.storeSnapshot(rack) == -1);
    CHECK_FALSE(s.recallSnapshot(rack, 8));
}

TEST_CASE(
    "racks: Morph takes the macros from A to B in the sound, follows its lane, leaves locked and automated "
    "macros alone, and lets go where it is; a recall turns it off",
    "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(rack != 0);
    const auto& gain = fd::findDevice("utility")->info().params[0];
    const float from = r.normalised("utility", "gain", -24.0f), to = r.normalised("utility", "gain", 0.0f);
    REQUIRE(s.mapMacro(rack, 0, utility, "gain", from, to));
    const auto levelAt = [&](float macro) { return gain.fromNormalized(from + macro * (to - from)); };

    REQUIRE(s.setMacro(rack, 0, 0.2f));
    REQUIRE(s.storeSnapshot(rack) == 0); // A: 0.2
    REQUIRE(s.setMacro(rack, 0, 0.8f));
    REQUIRE(s.storeSnapshot(rack) == 1);                         // B: 0.8
    CHECK_FALSE(s.setMorph(rack, {.on = true, .a = 0, .b = 5})); // no such snapshot

    REQUIRE(s.setMorph(rack, {.on = true, .position = 0.0f}));
    CHECK(s.undoDescription() == "Morph on");
    CHECK(r.rack(rack).morph.on);
    CHECK(r.gainDb() == Approx(levelAt(0.2f)).margin(0.3));
    REQUIRE(s.setMorph(rack, {.position = 1.0f}));
    CHECK(r.gainDb() == Approx(levelAt(0.8f)).margin(0.3));
    REQUIRE(s.setMorph(rack, {.position = 0.5f}));
    CHECK(r.gainDb() == Approx(levelAt(0.5f)).margin(0.3));
    CHECK(r.rack(rack).macros[0].value == Approx(0.5f).margin(1e-3));
    // Its macros are set by it: the panel shows their range, and a drag can't move them.
    {
        auto h = s.openRack(rack);
        REQUIRE(h != nullptr);
        const auto mapping = h->context().params.macroMapping(0);
        REQUIRE(mapping);
        CHECK(mapping->macro == juce::String::fromUTF8("Morph: Snapshot 1 \xe2\x86\x92 Snapshot 2"));
        CHECK(mapping->from == Approx(0.2f).margin(1e-4));
        CHECK(mapping->to == Approx(0.8f).margin(1e-4));
        CHECK(h->context().params.getPlain(0) == Approx(50.0f).margin(0.1f));
        CHECK(h->context().params.getPlain(static_cast<size_t>(model::RackInfo::morphParam)) ==
              Approx(50.0f).margin(0.1f));
    }

    // Its lane: Morph is automated as any parameter.
    const auto morphKey = "m" + std::to_string(rack) + "/morph";
    const auto choices = s.automatableParameters(r.t);
    CHECK(std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.key == morphKey; }));
    const auto morphLane = s.addAutomationLane(Session::ParamTarget{r.t, morphKey});
    REQUIRE(morphLane != 0);
    REQUIRE(s.setLanePoints(morphLane, {{0.0, 1.0f, 0.0f}, {kSeconds, 1.0f, 0.0f}}));
    CHECK(r.gainDb() == Approx(levelAt(0.8f)).margin(0.3));
    s.removeAutomationLane(morphLane);
    REQUIRE(s.setMorph(rack, {.position = 0.5f}));

    // A macro following its lane is left alone by Morph: the lane plays.
    const auto macroLane =
        s.addAutomationLane(Session::ParamTarget{r.t, "m" + std::to_string(rack) + "/macro1"});
    REQUIRE(macroLane != 0);
    REQUIRE(s.setLanePoints(macroLane, {{0.0, 0.0f, 0.0f}, {kSeconds, 0.0f, 0.0f}}));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the session sees the lane
    CHECK(r.rack(rack).macros[0].automated);
    {
        // Morph letting go of it never turned its lane off.
        auto h = s.openRack(rack);
        CHECK(h->context().params.automationState(0) == fd::ui::AutomationState::On);
    }
    {
        auto h = s.openRack(rack);
        CHECK_FALSE(h->context().params.macroMapping(0).has_value());
    }
    CHECK(r.gainDb() == Approx(-24.0).margin(0.3));
    s.removeAutomationLane(macroLane);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(r.gainDb() == Approx(levelAt(0.5f)).margin(0.3)); // Morph takes it again

    // Locked, it's left where it is.
    REQUIRE(s.lockMacro(rack, 0, true));
    REQUIRE(s.setMorph(rack, {.position = 1.0f}));
    CHECK(r.gainDb() == Approx(levelAt(0.5f)).margin(0.3));
    REQUIRE(s.lockMacro(rack, 0, false));
    CHECK(r.gainDb() == Approx(levelAt(0.8f)).margin(0.3));

    // Off: the macros stay where Morph had them, free again.
    REQUIRE(s.setMorph(rack, {.position = 0.25f}));
    REQUIRE(s.setMorph(rack, {.on = false}));
    CHECK_FALSE(r.rack(rack).morph.on);
    CHECK(r.rack(rack).macros[0].value == Approx(0.35f).margin(1e-3));
    CHECK(r.gainDb() == Approx(levelAt(0.35f)).margin(0.3));
    {
        auto h = s.openRack(rack);
        CHECK_FALSE(h->context().params.macroMapping(0).has_value());
    }
    s.undo(); // on again
    CHECK(r.rack(rack).morph.on);

    // A click on a snapshot while it's on turns it off and puts the macros there.
    REQUIRE(s.recallSnapshot(rack, 1));
    CHECK_FALSE(r.rack(rack).morph.on);
    CHECK(r.rack(rack).macros[0].value == Approx(0.8f).margin(1e-4));
    CHECK(r.gainDb() == Approx(levelAt(0.8f)).margin(0.3));
}

TEST_CASE(
    "racks: Vary moves the mapped macros a recall would move, each within its amount, as one undo step; "
    "never while Morph is on",
    "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(rack != 0);
    REQUIRE(s.mapMacro(rack, 0, utility, "gain", 0.2f, 0.8f));
    REQUIRE(s.mapMacro(rack, 1, utility, "width", 0.0f, 1.0f));
    REQUIRE(s.setMacro(rack, 0, 0.5f));
    REQUIRE(s.setMacro(rack, 1, 0.5f));
    REQUIRE(s.setMacro(rack, 2, 0.5f)); // maps nothing: Vary leaves it
    REQUIRE(s.lockMacro(rack, 1, true));

    bool moved = false;
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(s.varyRack(rack, Session::varyAmount));
        CHECK(s.undoDescription() == "Vary");
        const auto info = r.rack(rack);
        CHECK(std::abs(info.macros[0].value - 0.5f) <= Session::varyAmount + 1e-4f);
        moved = moved || std::abs(info.macros[0].value - 0.5f) > 1e-4f;
        CHECK(info.macros[1].value == Approx(0.5f)); // locked
        CHECK(info.macros[2].value == Approx(0.5f)); // maps nothing
        s.undo();
        CHECK(r.rack(rack).macros[0].value == Approx(0.5f).margin(1e-5));
    }
    CHECK(moved);

    // Nothing to move: refused.
    REQUIRE(s.lockMacro(rack, 0, true));
    CHECK_FALSE(s.varyRack(rack, Session::varyAmount));
    REQUIRE(s.lockMacro(rack, 0, false));
    // Morph on: refused, it sets the macros.
    REQUIRE(s.storeSnapshot(rack) == 0);
    REQUIRE(s.storeSnapshot(rack) == 1);
    REQUIRE(s.setMorph(rack, {.on = true}));
    CHECK_FALSE(s.varyRack(rack, Session::varyMoreAmount));
}

TEST_CASE(
    "racks: a rack file keeps its snapshots, Morph and locks, the same sound; a file from before them loads "
    "as it is",
    "[integration]")
{
    Racks r;
    auto& s = *r.session;
    const auto utility = s.addDevice(r.t, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(rack != 0);
    REQUIRE(s.renameRack(rack, "Verse to drop"));

    // Before any snapshot: the file has none, and loads with none.
    const auto plain = r.root.getChildFile("Plain.segnorack");
    REQUIRE(s.saveRack(rack, plain));
    {
        const auto json = nlohmann::json::parse(plain.loadFileAsString().toStdString());
        CHECK_FALSE(json.contains("snapshots"));
        CHECK_FALSE(json.contains("morph"));
        const auto loaded = s.loadRack(plain, {.channel = r.t});
        REQUIRE(loaded.rack != 0);
        CHECK(r.rack(loaded.rack).snapshots.empty());
        const auto again = r.root.getChildFile("Plain 2.segnorack");
        REQUIRE(s.saveRack(loaded.rack, again));
        CHECK(again.loadFileAsString() == plain.loadFileAsString());
        s.removeDevice(loaded.rack);
    }

    REQUIRE(s.mapMacro(rack, 0, utility, "gain", 0.2f, 0.8f));
    REQUIRE(s.setMacro(rack, 0, 0.1f, "Cut"));
    REQUIRE(s.storeSnapshot(rack) == 0);
    REQUIRE(s.setSnapshot(rack, 0, std::string("Verse")));
    REQUIRE(s.setMacro(rack, 0, 0.9f));
    REQUIRE(s.storeSnapshot(rack) == 1);
    REQUIRE(s.setSnapshot(rack, 1, std::string("Drop")));
    REQUIRE(s.lockMacro(rack, 3, true));
    REQUIRE(s.setMorph(rack, {.on = true, .position = 0.3f}));
    const auto first = r.render();

    const auto file = r.root.getChildFile("Verse to drop.segnorack");
    REQUIRE(s.saveRack(rack, file));
    s.removeDevice(rack);
    const auto loaded = s.loadRack(file, {.channel = r.t});
    REQUIRE(loaded.rack != 0);
    const auto info = r.rack(loaded.rack);
    REQUIRE(info.snapshots.size() == 2);
    CHECK(info.snapshots[0].name == "Verse");
    CHECK(info.snapshots[1].name == "Drop");
    CHECK(info.snapshots[1].macros[0] == Approx(0.9f).margin(1e-4));
    CHECK(info.morph.on);
    CHECK(info.morph.a == 0);
    CHECK(info.morph.b == 1);
    CHECK(info.morph.position == Approx(0.3f).margin(1e-4));
    CHECK(info.macros[3].locked);
    CHECK(info.macros[0].value == Approx(0.1f + 0.3f * 0.8f).margin(1e-3));
    INFO("loaded against saved: " << fd::testkit::nullResidualDb(r.render(), first) << " dB");
    CHECK(fd::testkit::nullResidualDb(r.render(), first) < -100.0);
    // Saved again: the same file, byte for byte.
    const auto second = r.root.getChildFile("Verse to drop 2.segnorack");
    REQUIRE(s.saveRack(loaded.rack, second));
    CHECK(second.loadFileAsString() == file.loadFileAsString());
}
