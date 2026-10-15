// Modulators through engine::Session (docs/design/modulators.md §5-§8, §13, ADR 0061): where they
// sit in a track's chain as modules move around them, their routes (refused off the track or in
// a loop), their copies, Keep and fresh passes, their panels' bindings, a module's knob under
// them, undo, and a project read back.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"
#include "model/Modulators.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;
using Kind = fd::model::ModulatorKind;
using namespace fd::model::modulator;
namespace model = fd::model;

namespace
{
constexpr double kSeconds = 1.0;

struct Modulators
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-modulator-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-modulators-" + juce::Uuid().toString());
    AudioBuffer input = fd::testkit::dc(48000, 0.5f, 2); // the output over 0.5 is the gain
    std::uint64_t t = 0;

    Modulators()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        (void)root.createDirectory();
        const auto file = root.getChildFile("dc.wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), input, 48000.0));
        t = track(0);
        REQUIRE(session->insertAudioFile(t, file, 0.0) != 0);
    }

    ~Modulators()
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

    std::vector<std::uint64_t> chainOf(std::uint64_t channel)
    {
        std::vector<std::uint64_t> out;
        for (const auto& d : info(channel).devices)
            out.push_back(d.id);
        return out;
    }

    model::DeviceSlot slot(std::uint64_t id)
    {
        for (const auto& d : info(t).devices)
            if (d.id == id)
                return d;
        FAIL("no module " << id);
        return {};
    }

    Session::ParamTarget target(std::uint64_t module, const std::string& param) const
    {
        return {t, "m" + std::to_string(module) + "/" + param};
    }

    /// A module's parameter by its text, through its host binding.
    void setParam(std::uint64_t device, const std::string& paramId, const std::string& text)
    {
        auto handle = session->openDevice(device);
        REQUIRE(handle != nullptr);
        auto& context = handle->context();
        const auto& params = context.info.params;
        const auto it =
            std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.id == paramId; });
        REQUIRE(it != params.end());
        context.params.setPlain(static_cast<size_t>(it - params.begin()), *it->fromText(text));
    }

    /// A modulator's parameter (plain), through its panel's binding.
    void setModulator(std::uint64_t modulator, size_t param, float plain)
    {
        auto handle = session->openModulator(modulator);
        REQUIRE(handle != nullptr);
        handle->context().params.setPlain(param, plain);
    }

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

    /// How far the gain swings (dB) after the first 50 ms.
    double swingDb()
    {
        const auto out = render();
        double lo = 1e9, hi = -1e9;
        for (int i = 2400; i < out.numSamples(); ++i)
        {
            const double db =
                20.0 * std::log10(std::max(1.0e-9, std::abs(static_cast<double>(out.at(0, i))) / 0.5));
            lo = std::min(lo, db);
            hi = std::max(hi, db);
        }
        return hi - lo;
    }
};
} // namespace

TEST_CASE("modulators (session): they sit in the chain where they're put, and stay as modules move",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    const auto filter = s.addDevice(f.t, "fd:filter");
    const auto lfo = s.addModulator(f.t, Kind::Lfo, 1); // between them
    REQUIRE(lfo != 0);
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{utility, lfo, filter});
    const auto slot = f.slot(lfo);
    CHECK(slot.kind == model::DeviceSlot::Kind::Modulator);
    CHECK(slot.deviceId == "mod-lfo");
    REQUIRE(slot.modulator != nullptr);
    CHECK(slot.modulator->kind == Kind::Lfo);
    CHECK(s.undoDescription() == "Add LFO");

    // A module added among them counts the modulator's place.
    const auto delay = s.addDevice(f.t, "fd:delay", 2);
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{utility, lfo, delay, filter});
    // A module moved past it: the LFO stays between the Utility and what came after it.
    s.moveDevice(utility, 3);
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{lfo, delay, filter, utility});
    // The modulator moved.
    s.moveDevice(lfo, 3);
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{delay, filter, utility, lfo});
    CHECK(s.undoDescription() == "Move LFO");
    // A module removed before it: it stays at the end.
    s.removeDevice(filter);
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{delay, utility, lfo});
    s.undo();
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{delay, filter, utility, lfo});
    // Its own removal, and undone.
    s.removeDevice(lfo);
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{delay, filter, utility});
    s.undo();
    CHECK(f.chainOf(f.t) == std::vector<std::uint64_t>{delay, filter, utility, lfo});
}

TEST_CASE("modulators (session): routes move what's on their track, and are refused elsewhere or in a loop",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    f.setParam(utility, "gain", "-12 dB");
    const auto lfo = s.addModulator(f.t, Kind::Lfo);
    CHECK(f.swingDb() < 0.01);
    REQUIRE(s.routeModulator(lfo, f.target(utility, "gain"), 0.2f));
    CHECK(s.undoDescription() == "Route LFO");
    CHECK(f.swingDb() > 6.0);
    auto routes = f.slot(lfo).modulator->routes;
    REQUIRE(routes.size() == 1);
    CHECK(routes[0].module == "Utility");
    CHECK(routes[0].param == "Gain");
    CHECK(routes[0].amount == Approx(0.2f));

    // The amount, then the route's removal; each one step, undone.
    REQUIRE(s.setRouteAmount(lfo, f.target(utility, "gain"), -0.4f));
    CHECK(f.slot(lfo).modulator->routes[0].amount == Approx(-0.4f));
    REQUIRE(s.removeRoute(lfo, f.target(utility, "gain")));
    CHECK(f.slot(lfo).modulator->routes.empty());
    CHECK(f.swingDb() < 0.01);
    s.undo();
    CHECK(f.slot(lfo).modulator->routes.size() == 1);

    // The track's own controls: its volume.
    CHECK(s.routeModulator(lfo, {f.t, "volume"}, 0.1f));
    // Another track's: refused.
    s.addTrack();
    const auto other = f.track(1);
    const auto elsewhere = s.addDevice(other, "fd:utility");
    CHECK_FALSE(s.routeModulator(lfo, {other, "m" + std::to_string(elsewhere) + "/gain"}, 0.5f));
    CHECK_FALSE(s.canRoute(lfo, {other, "m" + std::to_string(elsewhere) + "/gain"}));
    // Its own parameter, and a loop through another modulator: refused.
    CHECK_FALSE(s.routeModulator(lfo, f.target(lfo, "rate"), 0.5f));
    const auto random = s.addModulator(f.t, Kind::Random);
    REQUIRE(s.routeModulator(random, f.target(lfo, "rate"), 0.5f));     // Random moves the LFO's Rate
    CHECK_FALSE(s.routeModulator(lfo, f.target(random, "rate"), 0.5f)); // then the LFO can't move Random's
    CHECK(s.canRoute(random, f.target(utility, "width")));
}

TEST_CASE("modulators (session): each route its own copy; removing a modulator takes its routes",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    const auto a = s.addDevice(f.t, "fd:utility");
    const auto b = s.addDevice(f.t, "fd:utility");
    const auto random = s.addModulator(f.t, Kind::Random);
    REQUIRE(s.routeModulator(random, f.target(a, "gain"), 0.2f));
    REQUIRE(s.routeModulator(random, f.target(b, "gain"), 0.2f));
    REQUIRE(s.routeModulator(random, f.target(b, "width"), 0.2f));
    auto routes = f.slot(random).modulator->routes;
    REQUIRE(routes.size() == 3);
    std::set<int> copies;
    for (const auto& r : routes)
        copies.insert(r.copy);
    CHECK(copies == std::set<int>{0, 1, 2});
    // The panel's host counts them.
    auto handle = s.openModulator(random);
    REQUIRE(handle != nullptr);
    CHECK(handle->host().routeCount() == 3);
    // Removing it takes them, in one step.
    s.removeDevice(random);
    CHECK(f.swingDb() < 0.01);
    s.undo();
    CHECK(f.slot(random).modulator->routes.size() == 3);
}

TEST_CASE("modulators (session): Keep holds the roll; playback rolls again otherwise (ADR 0062)",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    const auto random = s.addModulator(f.t, Kind::Random);
    REQUIRE(s.routeModulator(random, f.target(utility, "gain"), 0.3f));
    auto handle = s.openModulator(random);
    REQUIRE(handle != nullptr);
    auto& host = handle->host();
    CHECK_FALSE(host.kept());
    // A new pass, fresh dice.
    const auto before = host.roll();
    s.play();
    s.stop();
    CHECK(host.roll() != before);
    // Kept: the same roll, pass after pass; one step, undone.
    REQUIRE(s.keepRoll(random, true));
    CHECK(s.undoDescription() == "Keep as played");
    CHECK(f.slot(random).modulator->kept);
    const auto kept = host.roll();
    s.play();
    s.stop();
    CHECK(host.roll() == kept);
    s.undo();
    CHECK_FALSE(host.kept());
    // The LFO rolls no dice: nothing to keep.
    const auto lfo = s.addModulator(f.t, Kind::Lfo);
    CHECK_FALSE(s.keepRoll(lfo, true));
    // Keep all.
    s.keepAllRolls(true);
    CHECK(host.kept());
    CHECK(s.undoDescription() == "Keep all as played");
}

TEST_CASE(
    "modulators (session): a module's knob under them shows its own value, their ring and where it is now",
    "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    f.setParam(utility, "gain", "-12 dB");
    const auto lfo = s.addModulator(f.t, Kind::Lfo);
    REQUIRE(s.routeModulator(lfo, f.target(utility, "gain"), 0.2f));
    auto handle = s.openDevice(utility);
    REQUIRE(handle != nullptr);
    auto& binding = handle->context().params;
    CHECK(binding.getPlain(0) == Approx(-12.0f).margin(0.01f)); // its own value
    const auto moved = binding.modulated(0);
    REQUIRE(moved.has_value());
    const float v = fd::findDevice("utility")->info().params[0].toNormalized(-12.0f);
    CHECK(moved->from == Approx(v - 0.1f).margin(1e-4));
    CHECK(moved->to == Approx(v + 0.1f).margin(1e-4));
    CHECK(moved->by == "LFO");
    CHECK_FALSE(moved->dimmed);
    // Another modulator's handle hovered: its ring dims.
    const auto random = s.addModulator(f.t, Kind::Random);
    s.setModulatorPointer({.modulator = random});
    CHECK(binding.modulated(0)->dimmed);
    // A handle dragged: every control a candidate; the one under the pointer lit.
    s.setModulatorPointer({.modulator = random, .dragging = true, .over = f.target(utility, "width")});
    CHECK(binding.modulatorDrag(0).candidate);
    CHECK_FALSE(binding.modulatorDrag(0).target);
    CHECK(binding.modulatorDrag(2).target); // width
    s.setModulatorPointer({});
    CHECK_FALSE(binding.modulatorDrag(0).candidate);
}

TEST_CASE("modulators (session): their panels edit them in undo steps; a project read back keeps them",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    const auto steps = s.addModulator(f.t, Kind::Steps);
    REQUIRE(s.routeModulator(steps, f.target(utility, "gain"), 0.3f));
    f.setModulator(steps, Steps::Length, 8.0f);
    {
        // A panel's handle lives no longer than its project (as a module's).
        auto handle = s.openModulator(steps);
        REQUIRE(handle != nullptr);
        CHECK(handle->context().params.getPlain(Steps::Length) == Approx(8.0f));
        handle->host().setStep(3, -0.5f, 0.25f);
        CHECK(handle->host().stepValues()[3] == Approx(-0.5f));
        CHECK(s.undoDescription() == "Edit step");
    }
    REQUIRE(s.keepRoll(steps, true));

    const auto file = f.root.getChildFile("project/project.segno");
    REQUIRE(s.saveAs(file));
    REQUIRE(s.open(file));
    const auto t = f.track(0);
    const auto chain = f.info(t).devices;
    REQUIRE(chain.size() == 2);
    CHECK(chain[1].kind == model::DeviceSlot::Kind::Modulator);
    const auto id = chain[1].id;
    REQUIRE(chain[1].modulator != nullptr);
    CHECK(chain[1].modulator->kept);
    CHECK(chain[1].modulator->routes.size() == 1);
    auto reopened = s.openModulator(id);
    REQUIRE(reopened != nullptr);
    CHECK(reopened->context().params.getPlain(Steps::Length) == Approx(8.0f));
    CHECK(reopened->host().stepValues()[3] == Approx(-0.5f));
    CHECK(reopened->host().stepChances()[3] == Approx(0.25f));
}

TEST_CASE("modulators (session): a Follower listens where it sits or to another track; loops are refused",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    s.addTrack();
    const auto kick = f.track(1);
    const auto follower = s.addModulator(f.t, Kind::Follower);
    const auto lfo = s.addModulator(f.t, Kind::Lfo);
    REQUIRE(follower != 0);

    // As a sidechain: where it sits by default; any track that doesn't hear its own.
    CHECK(f.slot(follower).takesSidechain);
    CHECK_FALSE(f.slot(follower).sidechainSource.has_value());
    CHECK_FALSE(f.slot(lfo).takesSidechain);
    auto sources = s.sidechainSources(follower);
    CHECK(std::find(sources.begin(), sources.end(), kick) != sources.end());
    CHECK(std::find(sources.begin(), sources.end(), f.t) == sources.end()); // its own: "Here"
    CHECK(s.sidechainSources(lfo).empty());
    CHECK_FALSE(s.setSidechainSource(lfo, kick));

    // Listening to the kick: one undo step.
    REQUIRE(s.setSidechainSource(follower, kick));
    CHECK(s.undoDescription() == "Set Listen");
    CHECK(f.slot(follower).sidechainSource == kick);
    {
        // The panel's Listen: Here, then the tracks; the kick chosen.
        auto handle = s.openModulator(follower);
        REQUIRE(handle != nullptr);
        auto* listen = handle->context().sidechain;
        REQUIRE(listen != nullptr);
        const auto names = listen->sources();
        REQUIRE(names.size() >= 2);
        CHECK(names.front() == "Here");
        CHECK(listen->selected() > 0);
        listen->select(0); // Here
        CHECK_FALSE(f.slot(follower).sidechainSource.has_value());
        s.undo();
        CHECK(f.slot(follower).sidechainSource == kick);
    }

    // A Follower on the kick listening back to the first track would make a loop: refused.
    const auto back = s.addModulator(kick, Kind::Follower);
    sources = s.sidechainSources(back);
    CHECK(std::find(sources.begin(), sources.end(), f.t) == sources.end());
    CHECK_FALSE(s.setSidechainSource(back, f.t));
}

TEST_CASE("modulators (session): the master has them too, placed among its modules, moving them",
          "[integration][modulators]")
{
    Modulators f;
    auto& s = *f.session;
    s.updateSnapshotNow();
    const auto master = s.snapshot()->master.id;
    const auto utility = s.addDevice(master, "fd:utility");
    const auto lfo = s.addModulator(master, Kind::Lfo, 0); // before the Utility
    REQUIRE(lfo != 0);
    s.updateSnapshotNow();
    const auto& devices = s.snapshot()->master.devices;
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].id == lfo);
    CHECK(devices[0].kind == model::DeviceSlot::Kind::Modulator);
    CHECK(devices[1].id == utility);
    CHECK(s.placeOf(lfo)->index == 0);
    CHECK(s.placeOf(utility)->index == 1);

    // Moving the master's Utility: heard.
    CHECK(f.swingDb() < 0.1);
    const Session::ParamTarget gain{master, "m" + std::to_string(utility) + "/gain"};
    REQUIRE(s.canRoute(lfo, gain));
    REQUIRE(s.routeModulator(lfo, gain, 0.5f));
    f.setModulator(lfo, Lfo::Rate, 8.0f);
    CHECK(f.swingDb() > 3.0);
    // Its parameters are the master's to automate and route.
    const auto choices = s.automatableParameters(master);
    CHECK(std::any_of(choices.begin(), choices.end(),
                      [&](const auto& c) { return c.key == "m" + std::to_string(lfo) + "/rate"; }));
    // Moved after the Utility, and back.
    s.moveDevice(lfo, 1);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->master.devices[1].id == lfo);
}
