// Automation in the engine (M2 step C4, ADRs 0028 and 0029, docs/design/automation.md): lanes of
// their own drive their parameters through ranges, exactly as drawn, on the control's scale;
// assignments, undo, override, recording and projects.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using Catch::Approx;
using fd::engine::Session;
using fd::model::AssignmentRange;
using fd::model::AutomationPoint;
using fd::model::AutomationPoints;
using fd::testkit::AudioBuffer;
using Target = Session::ParamTarget;

namespace
{
constexpr double kRate = 48000.0;

struct Automation
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-automation-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    std::vector<std::unique_ptr<juce::TemporaryFile>> files;

    Automation()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    const fd::model::TrackInfo& channel(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }

    /// A copy: snapshots are replaced.
    fd::model::AutomationLaneInfo lane(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* l = session->snapshot()->automationLane(id);
        REQUIRE(l != nullptr);
        return *l;
    }

    bool exists(std::uint64_t lane)
    {
        session->updateSnapshotNow();
        return session->snapshot()->automationLane(lane) != nullptr;
    }

    tracktion::AutomatableParameter& param(const Target& target)
    {
        auto p = session->impl().param(target);
        REQUIRE(p);
        REQUIRE(p->param != nullptr);
        return *p->param;
    }

    void place(std::uint64_t trackId, const AudioBuffer& audio, double seconds = 0.0)
    {
        auto& file = *files.emplace_back(std::make_unique<juce::TemporaryFile>(".wav"));
        REQUIRE(fd::testkit::writeWav(file.getFile().getFullPathName().toStdString(), audio, kRate));
        REQUIRE(session->insertAudioFile(trackId, file.getFile(), seconds) != 0);
    }

    AudioBuffer render(double start, double end)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, kRate}, out.getFile(), [&](const std::string& e) {
            error = e;
            done = true;
        });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 600 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }

    /// The parameter's curve as tracktion evaluates it at `seconds`, in its control's travel.
    float curve(const Target& target, double seconds)
    {
        auto p = session->impl().param(target);
        REQUIRE(p);
        return p->toTravel(p->param->getCurve().getValueAt(tracktion::TimePosition::fromSeconds(seconds), 0.0f));
    }

    /// The value the engine plays at `seconds` (its playback iterator), in the control's travel.
    float played(const Target& target, double seconds)
    {
        auto p = session->impl().param(target);
        REQUIRE(p);
        tracktion::AutomationIterator it(*p->param);
        it.setPosition(tracktion::TimePosition::fromSeconds(seconds));
        return p->toTravel(it.getCurrentValue());
    }

    int curvePoints(const Target& target) { return param(target).getCurve().getNumPoints(); }
};

/// The key of a parameter of a channel's module.
std::string moduleKey(const fd::model::TrackInfo& t, size_t module, std::string_view param)
{
    return "m" + std::to_string(t.devices.at(module).id) + "/" + std::string(param);
}

AutomationPoints randomPoints(unsigned seed, double& end)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> value(0.0f, 1.0f), bend(-1.0f, 1.0f);
    AutomationPoints points;
    double time = 0.25;
    for (int i = 0; i < 12; ++i, time += 0.3 + 0.1 * i)
        points.push_back({time, value(rng), i % 4 == 3 ? 0.0f : fd::model::curveForBend(bend(rng))});
    end = time;
    return points;
}
} // namespace

TEST_CASE("automation: a lane's curve is what its parameters play, through full, partial and inverted ranges",
          "[integration]")
{
    Automation a;
    const auto t0 = a.track(0), t1 = a.track(1);
    REQUIRE(a.session->addDevice(t1, "fd:eq") != 0);
    const Target volume{t0, "volume"}, pan{t1, "pan"}, freq{t1, moduleKey(a.channel(t1), 0, "band1.freq")};
    const auto lane = a.session->addAutomationLane(t0, volume);
    REQUIRE(lane != 0);
    const AssignmentRange part{0.2f, 0.7f}, inverted{0.9f, 0.1f};
    REQUIRE(a.session->assign(lane, pan, part));
    REQUIRE(a.session->assign(lane, freq, inverted));
    double end = 0.0;
    REQUIRE(a.session->setLanePoints(lane, randomPoints(7, end)));
    const auto info = a.lane(lane);
    REQUIRE(info.points.size() == 12);
    REQUIRE(info.assignments.size() == 3);
    CHECK(info.after == t0);
    CHECK(info.displayName() == "Volume + 2");

    for (const auto& [target, range] : {std::pair{volume, AssignmentRange{}}, std::pair{pan, part}, std::pair{freq, inverted}})
        for (double s = 0.0; s < end + 1.0; s += 0.013)
        {
            const float drawn = range.toTravel(fd::model::automationValueAt(info.points, s));
            INFO(target.key << " at " << s << " s");
            CHECK(a.curve(target, s) == Approx(drawn).margin(1e-5));
            CHECK(a.played(target, s) == Approx(drawn).margin(1e-5)); // patched: playback bends as drawn
        }
}

TEST_CASE("automation: a straight segment is a straight move of the control", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    REQUIRE(a.session->addDevice(t, "fd:eq") != 0);
    const auto info = a.channel(t);
    // A frequency on a skewed knob: halfway along the segment is the knob's centre value.
    const Target freq{t, moduleKey(info, 0, "band1.freq")};
    const auto lane = a.session->addAutomationLane(t, freq);
    REQUIRE(a.session->setLanePoints(lane, {{0.0, 0.0f, 0.0f}, {2.0, 1.0f, 0.0f}}));
    CHECK(a.played(freq, 1.0) == Approx(0.5f));
    const auto plain = a.session->paramPlain(freq, a.played(freq, 1.0));
    REQUIRE(plain);
    auto* eq = dynamic_cast<fd::engine::DevicePlugin*>(a.session->impl().plugin(info.devices[0].id));
    REQUIRE(eq != nullptr);
    const auto index = *eq->device().params().indexOf("band1.freq");
    CHECK(*plain == Approx(eq->device().info().params[index].fromNormalized(0.5f)));
    CHECK(*plain < 2000.0); // not the linear middle of 20 Hz .. 20 kHz

    // Volume: 0 dB to -24 dB passes the taper's halfway position halfway.
    const Target volume{t, "volume"};
    const auto volumeLane = a.session->automateInNewLane(volume);
    const float top = *a.session->paramFromPlain(volume, 0.0), bottom = *a.session->paramFromPlain(volume, -24.0);
    REQUIRE(a.session->setLanePoints(volumeLane, {{0.0, top, 0.0f}, {2.0, bottom, 0.0f}}));
    CHECK(a.played(volume, 1.0) == Approx((top + bottom) / 2.0f));
    CHECK(*a.session->paramPlain(volume, top) == Approx(0.0).margin(1e-4));
}

TEST_CASE("automation: a rendered volume lane follows the drawn curve", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    a.place(t, fd::testkit::dc(static_cast<int>(12.0 * kRate), 0.5f));
    const Target volume{t, "volume"};
    // Through a partial range: the lane's 0..1 is -24 dB .. +6 dB.
    const AssignmentRange range{*a.session->paramFromPlain(volume, -24.0), 1.0f};
    const auto lane = a.session->addAutomationLane(t);
    REQUIRE(a.session->assign(lane, volume, range));
    const float zero = *range.toLane(*a.session->paramFromPlain(volume, 0.0));
    const float low = *range.toLane(*a.session->paramFromPlain(volume, -12.0));
    // Holds, a slow ramp, a bend and a step down.
    REQUIRE(a.session->setLanePoints(lane, {{1.0, zero, 0.0f}, {9.0, low, fd::model::curveForBend(0.5f)}, {10.0, zero, 0.0f}}));
    const auto points = a.lane(lane).points;
    const auto y = a.render(0.0, 11.0);
    auto renderedDb = [&](double s) { return 20.0 * std::log10(std::abs(y.at(0, static_cast<int>(s * kRate))) / 0.5); };
    auto drawnDb = [&](double s) {
        return *a.session->paramPlain(volume, range.toTravel(fd::model::automationValueAt(points, s)));
    };
    CHECK(renderedDb(0.5) == Approx(0.0).margin(0.01));
    CHECK(renderedDb(10.5) == Approx(0.0).margin(0.01));
    // Along the ramp: within 0.05 dB (the engine updates every 128 samples and smooths over
    // 15 ms, a few milliseconds behind a moving value).
    for (double s = 1.5; s < 9.0; s += 0.5)
    {
        INFO("at " << s << " s");
        CHECK(renderedDb(s) == Approx(drawnDb(s)).margin(0.05));
    }
}

TEST_CASE("automation: one point holds; one stepped parameter steps; latency settings can't be assigned",
          "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Target pan{t, "pan"};
    const auto panLane = a.session->addAutomationLane(t, pan);
    REQUIRE(a.session->setLanePoints(panLane, {{4.0, 0.25f, 0.0f}}));
    CHECK(a.param(pan).isAutomationActive()); // patched: a single point plays
    CHECK(a.played(pan, 0.0) == Approx(0.25f));
    CHECK(a.played(pan, 9.0) == Approx(0.25f));

    REQUIRE(a.session->addDevice(t, "fd:compressor") != 0);
    const auto info = a.channel(t);
    // A choice alone on its lane over its full range: values snap to its steps and hold.
    const Target character{t, moduleKey(info, 0, "character")};
    const auto lane = a.session->addAutomationLane(t, character);
    REQUIRE(a.session->setLanePoints(lane, {{0.0, 0.1f, 0.0f}, {2.0, 0.9f, 0.0f}}));
    auto withSteps = a.lane(lane);
    CHECK(withSteps.rules().steps == 2); // two characters
    CHECK(withSteps.points[0].value == 0.0f);
    CHECK(withSteps.points[1].value == 1.0f);
    CHECK(withSteps.points[0].curve == fd::model::kStepCurve);
    CHECK(a.played(character, 1.9) == 0.0f);
    CHECK(a.played(character, 2.01) == 1.0f);
    // With another parameter the lane ramps; the choice plays the ramp as its steps.
    REQUIRE(a.session->assign(lane, pan));
    REQUIRE(a.session->setLanePoints(lane, {{0.0, 0.0f, 0.0f}, {2.0, 1.0f, 0.0f}}));
    CHECK_FALSE(a.lane(lane).rules().stepped());
    CHECK(a.lane(lane).points[0].curve == 0.0f);
    CHECK(a.played(pan, 1.0) == Approx(0.5f));

    const Target lookahead{t, moduleKey(info, 0, "lookahead")};
    CHECK(a.session->addAutomationLane(t, lookahead) == 0);
    CHECK_FALSE(a.session->assign(lane, lookahead));
    const auto choices = a.session->automatableParameters(t);
    CHECK(std::none_of(choices.begin(), choices.end(), [&](const auto& c) { return c.key == lookahead.key; }));
    const auto vol = std::find_if(choices.begin(), choices.end(), [](const auto& c) { return c.key == "volume"; });
    REQUIRE(vol != choices.end());
    CHECK(vol->lane == 0);
    CHECK(std::find_if(choices.begin(), choices.end(), [](const auto& c) { return c.key == "pan"; })->lane == lane);
}

TEST_CASE("automation: one lane drives parameters on two tracks; assigning moves; each edit is one undo step",
          "[integration]")
{
    Automation a;
    const auto t0 = a.track(0), t1 = a.track(1);
    const Target v0{t0, "volume"}, v1{t1, "volume"};
    const auto lane = a.session->addAutomationLane(t0, v0);
    CHECK(a.session->undoDescription() == "Add automation lane");
    REQUIRE(a.session->assign(lane, v1, {0.0f, 0.5f}));
    CHECK(a.session->undoDescription() == "Assign parameter");
    REQUIRE(a.session->setLanePoints(lane, {{1.0, 0.5f, 0.0f}, {2.0, 1.0f, 0.0f}}, std::nullopt, "Add point"));
    CHECK(a.session->undoDescription() == "Add point");
    CHECK(a.curve(v0, 2.0) == Approx(1.0f));
    CHECK(a.curve(v1, 2.0) == Approx(0.5f)); // through its range

    // A time range replaces only what's inside it; undo restores the lane and both curves.
    REQUIRE(a.session->setLanePoints(lane, {{1.5, 0.1f, 0.0f}}, std::pair{1.2, 3.0}, "Draw automation"));
    CHECK(a.lane(lane).points.size() == 2);
    CHECK(a.curvePoints(v1) == 2);
    CHECK(a.curve(v1, 1.5) == Approx(0.05f));
    a.session->undo();
    CHECK(a.lane(lane).points[1].time == Approx(2.0));
    CHECK(a.curve(v0, 2.0) == Approx(1.0f));
    CHECK(a.curve(v1, 2.0) == Approx(0.5f));

    // A range is one step; its curve follows.
    a.session->setAssignmentRange(v1, {1.0f, 0.0f});
    CHECK(a.session->undoDescription() == "Assignment range");
    CHECK(a.curve(v1, 2.0) == Approx(0.0f));
    a.session->undo();
    CHECK(a.curve(v1, 2.0) == Approx(0.5f));

    // One lane per parameter: a new lane for it takes it, bringing its curve (full range there).
    const auto other = a.session->addAutomationLane(t1, v1);
    REQUIRE(other != 0);
    CHECK(a.session->laneOf(v1) == other);
    CHECK(a.lane(lane).assignments.size() == 1);
    CHECK(a.lane(other).points.size() == 2);
    CHECK(a.curve(v1, 2.0) == Approx(0.5f)); // it sounds the same
    a.session->undo();
    CHECK(a.session->laneOf(v1) == lane);
    CHECK_FALSE(a.exists(other));
    // Assigning it elsewhere moves it in one step.
    const auto empty = a.session->addAutomationLane(0);
    REQUIRE(a.session->assign(empty, v1));
    CHECK(a.session->laneOf(v1) == empty);
    CHECK(a.curvePoints(v1) == 0);
    a.session->undo();
    CHECK(a.session->laneOf(v1) == lane);
    CHECK(a.curvePoints(v1) == 2);

    // Unassigning leaves the parameter as it is; removing a lane frees them all, in one step.
    a.session->unassign(v1);
    CHECK(a.session->undoDescription() == "Remove from automation lane");
    CHECK_FALSE(a.session->laneOf(v1));
    CHECK(a.curvePoints(v1) == 0);
    a.session->undo();
    a.session->removeAutomationLane(lane);
    CHECK_FALSE(a.exists(lane));
    CHECK(a.curvePoints(v0) == 0);
    CHECK(a.curvePoints(v1) == 0);
    a.session->undo();
    REQUIRE(a.exists(lane));
    CHECK(a.lane(lane).assignments.size() == 2);
    CHECK(a.curvePoints(v0) == 2);

    // Names are steps; on switches are listening state, never steps.
    a.session->renameAutomationLane(lane, "Swell");
    CHECK(a.lane(lane).displayName() == "Swell");
    const auto before = a.session->undoDescription();
    a.session->setLaneOn(lane, false);
    a.session->setAssignmentOn(v0, true);
    CHECK(a.session->undoDescription() == before);
    CHECK(a.lane(lane).assignment(t0, "volume")->on);
    CHECK_FALSE(a.lane(lane).assignment(t1, "volume")->on);
    CHECK(a.session->anyLaneOff());
}

TEST_CASE("automation: moving an assigned control turns off its assignment, and only it", "[integration]")
{
    Automation a;
    const auto t0 = a.track(0), t1 = a.track(1);
    const Target v0{t0, "volume"}, v1{t1, "volume"};
    const auto lane = a.session->addAutomationLane(t0, v0);
    REQUIRE(a.session->assign(lane, v1));
    REQUIRE(a.session->setLanePoints(lane, {{0.0, 0.2f, 0.0f}, {4.0, 0.9f, 0.0f}}));
    CHECK(a.lane(lane).allOn());

    a.session->setVolume(t0, -3.0f); // a move of the fader (not recording)
    auto info = a.lane(lane);
    CHECK_FALSE(info.assignment(t0, "volume")->on);
    CHECK(info.assignment(t1, "volume")->on); // the lane's other parameter keeps playing
    CHECK(info.points.size() == 2);           // nothing lost
    CHECK(a.channel(t0).mix.volumeDb == Approx(-3.0f).margin(0.01f)); // the value set by hand stays
    CHECK(a.session->anyLaneOff());
    CHECK(a.session->lastTouchedParameter() == v0);

    a.session->setAllLanesOn();
    CHECK(a.lane(lane).allOn());
    CHECK_FALSE(a.session->anyLaneOff());

    // Switching a send between pre and post isn't a move of its control.
    a.session->addReturnTrack();
    a.session->updateSnapshotNow();
    const auto r = a.session->snapshot()->returns.at(0).id;
    const Target send{t0, "send/" + std::to_string(r)};
    REQUIRE(a.session->assign(lane, send)); // makes the send
    REQUIRE(a.session->setSend(t0, r, std::nullopt, true));
    CHECK(a.lane(lane).assignment(t0, send.key)->on);
}

TEST_CASE("automation: played values never mark the project changed or rebuild snapshots", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Target volume{t, "volume"};
    const auto lane = a.session->addAutomationLane(t, volume);
    REQUIRE(a.session->setLanePoints(lane, {{0.0, 0.2f, 0.0f}, {4.0, 0.9f, 0.0f}}));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500); // tracktion's undo timer settles
    juce::TemporaryFile project(Session::fileExtension);
    REQUIRE(a.session->saveAs(project.getFile()));
    a.session->flushSnapshot();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    const auto version = a.session->snapshot()->version;

    // The engine follows the lane: tracktion writes each played value back into the project.
    auto& param = a.param(volume);
    for (double s : {0.5, 1.0, 2.0, 3.0})
    {
        param.updateFromAutomationSources(tracktion::TimePosition::fromSeconds(s));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    CHECK_FALSE(a.session->hasUnsavedChanges());
    CHECK(a.session->snapshot()->version == version);
}

TEST_CASE("automation: editing a lane updates its curves in place, never rebuilding playback", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Target volume{t, "volume"}, pan{t, "pan"};
    const auto lane = a.session->addAutomationLane(t, volume);
    REQUIRE(a.session->assign(lane, pan));
    REQUIRE(a.session->setLanePoints(lane, {{0.0, 0.2f, 0.0f}, {4.0, 0.9f, 0.0f}}));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // automation came: the graph was rebuilt
    REQUIRE_FALSE(a.session->impl().restartPending);
    for (const auto& points : {AutomationPoints{{0.0, 0.3f, 0.0f}, {4.0, 0.9f, 0.0f}},
                               AutomationPoints{{0.0, 0.3f, 0.0f}, {2.0, 0.6f, 0.2f}, {4.0, 0.9f, 0.0f}},
                               AutomationPoints{{1.0, 0.5f, 0.0f}}})
    {
        REQUIRE(a.session->setLanePoints(lane, points, std::nullopt, "Move points"));
        CHECK_FALSE(a.session->impl().restartPending);
        a.session->undo();
        CHECK_FALSE(a.session->impl().restartPending);
    }
    // Only when automation goes (or comes).
    REQUIRE(a.session->setLanePoints(lane, {}));
    CHECK(a.session->impl().restartPending);
}

TEST_CASE("automation: lanes are saved with the project; older projects convert and keep their sound", "[integration]")
{
    Automation a;
    const auto t0 = a.track(0), t1 = a.track(1);
    REQUIRE(a.session->addDevice(t0, "fd:eq") != 0);
    const auto info = a.channel(t0);
    const Target freq{t0, moduleKey(info, 0, "band1.freq")}, pan{t1, "pan"}, volume{t0, "volume"};
    const auto first = a.session->addAutomationLane(t1, pan);
    const auto second = a.session->addAutomationLane(0, freq);
    REQUIRE(a.session->assign(second, volume, {0.8f, 0.3f}));
    REQUIRE(a.session->setLanePoints(second, {{0.5, 0.3f, fd::model::curveForBend(0.4f)}, {1.5, 0.8f, 0.0f}}));
    a.session->renameAutomationLane(first, "Pan sweep");
    a.session->setAssignmentOn(freq, false);
    a.session->updateSnapshotNow();
    const auto lanes = a.session->snapshot()->automationLanes;
    REQUIRE(lanes.size() == 2);
    CHECK(lanes[0].id == second);
    CHECK(lanes[1].id == first);

    juce::TemporaryFile project(Session::fileExtension);
    REQUIRE(a.session->saveAs(project.getFile()));
    REQUIRE(a.session->open(project.getFile()));
    a.session->updateSnapshotNow();
    const auto loaded = a.session->snapshot()->automationLanes;
    REQUIRE(loaded.size() == lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i)
    {
        CHECK(loaded[i].id == lanes[i].id);
        CHECK(loaded[i].name == lanes[i].name);
        CHECK(loaded[i].after == lanes[i].after);
        CHECK(loaded[i].colour == lanes[i].colour);
        REQUIRE(loaded[i].points.size() == lanes[i].points.size());
        for (size_t p = 0; p < lanes[i].points.size(); ++p)
        {
            CHECK(loaded[i].points[p].time == Approx(lanes[i].points[p].time));
            CHECK(loaded[i].points[p].value == Approx(lanes[i].points[p].value));
            CHECK(loaded[i].points[p].curve == Approx(lanes[i].points[p].curve));
        }
        REQUIRE(loaded[i].assignments.size() == lanes[i].assignments.size());
        for (size_t j = 0; j < lanes[i].assignments.size(); ++j)
        {
            CHECK(loaded[i].assignments[j].key == lanes[i].assignments[j].key);
            CHECK(loaded[i].assignments[j].range == lanes[i].assignments[j].range);
            CHECK(loaded[i].assignments[j].on == lanes[i].assignments[j].on);
        }
    }
    CHECK_FALSE(a.session->hasUnsavedChanges());
    CHECK_FALSE(a.session->canUndo());
    CHECK(a.curve(volume, 1.5) == Approx(AssignmentRange{0.8f, 0.3f}.toTravel(0.8f)));
    // New lanes get new ids.
    CHECK(a.session->addAutomationLane(0) > std::max(first, second));

    // A format-2 project (ADR 0028): curves on the parameters, the shown lanes listed per track.
    // Each becomes a lane of its own after its track, sounding the same.
    auto xml = juce::XmlDocument::parse(project.getFile());
    REQUIRE(xml != nullptr);
    xml->setAttribute("fdFormat", 2);
    xml->deleteAllChildElementsWithTagName("FDAUTOMATION");
    std::function<void(juce::XmlElement&)> showLanes = [&](juce::XmlElement& e) {
        for (auto* child : e.getChildIterator())
        {
            if (child->hasTagName("TRACK") && child->getStringAttribute("id") == juce::String(std::to_string(t1)))
                child->setAttribute("fdLanes", "volume");
            showLanes(*child);
        }
    };
    showLanes(*xml);
    REQUIRE(xml->writeTo(project.getFile()));
    std::vector<float> before;
    for (double s = 0.0; s < 2.0; s += 0.1)
        before.push_back(a.curve(volume, s));
    REQUIRE(a.session->open(project.getFile()));
    a.session->updateSnapshotNow();
    const auto converted = a.session->snapshot()->automationLanes;
    // Track 0: volume and band1.freq (with points, in parameter order); track 1: its shown volume
    // lane (pan had no points and wasn't shown).
    REQUIRE(converted.size() == 3);
    CHECK(converted[0].after == t0);
    CHECK(converted[0].assignments.at(0).key == "volume");
    CHECK(converted[1].assignments.at(0).key == freq.key);
    CHECK(converted[2].after == t1);
    CHECK(converted[2].assignments.at(0).key == "volume");
    CHECK(converted[2].points.empty());
    for (const auto& l : converted)
    {
        REQUIRE(l.assignments.size() == 1);
        CHECK(l.assignments[0].range.full());
    }
    CHECK_FALSE(converted[1].assignments[0].on); // off stays off
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(a.curve(volume, 0.1 * static_cast<double>(i)) == Approx(before[i]).margin(1e-5));
    CHECK_FALSE(a.session->hasUnsavedChanges());

    // A project from before automation: levels on tracktion's own fader curve, module
    // parameters as plain values. It opens with the same levels and settings.
    REQUIRE(a.session->saveAs(project.getFile()));
    xml = juce::XmlDocument::parse(project.getFile());
    REQUIRE(xml != nullptr);
    xml->removeAttribute("fdFormat");
    xml->deleteAllChildElementsWithTagName("FDAUTOMATION");
    float gainDb = 0.0f;
    std::vector<juce::XmlElement*> plugins;
    std::function<void(juce::XmlElement&)> collect = [&](juce::XmlElement& e) {
        for (auto* child : e.getChildIterator())
        {
            if (child->hasTagName("PLUGIN"))
                plugins.push_back(child);
            collect(*child);
        }
    };
    collect(*xml);
    for (auto* plugin : plugins)
    {
        plugin->deleteAllChildElementsWithTagName("AUTOMATIONCURVE");
        plugin->removeAttribute("base64:parameters"); // tracktion's record of automated values
        if (plugin->getStringAttribute("type") == "volume")
            plugin->setAttribute("volume", std::exp((-6.0 - 6.0) / 20.0)); // -6 dB, old curve
        if (plugin->getStringAttribute("type") == "fd.eq")
        {
            plugin->removeAttribute("fdNormalised");
            plugin->setAttribute("band1.gain", 4.5); // plain dB
            gainDb = 4.5f;
        }
    }
    REQUIRE(gainDb != 0.0f);
    REQUIRE(xml->writeTo(project.getFile()));
    REQUIRE(a.session->open(project.getFile()));
    const auto old = a.channel(t0);
    CHECK(old.mix.volumeDb == Approx(-6.0f).margin(0.01f));
    auto handle = a.session->openDevice(old.devices.at(0).id);
    REQUIRE(handle != nullptr);
    auto& ctx = handle->context();
    size_t gainIndex = 0;
    for (size_t i = 0; i < ctx.info.params.size(); ++i)
        if (ctx.info.params[i].id == "band1.gain")
            gainIndex = i;
    CHECK(ctx.params.getPlain(gainIndex) == Approx(4.5f).margin(1e-4));
    CHECK(a.session->snapshot()->automationLanes.empty());
    CHECK_FALSE(a.session->hasUnsavedChanges());
}

TEST_CASE("automation: a removed module takes its assignments; dangling ones are pruned on opening", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const auto eq = a.session->addDevice(t, "fd:eq");
    REQUIRE(eq != 0);
    const Target freq{t, moduleKey(a.channel(t), 0, "band1.freq")}, volume{t, "volume"};
    const auto lane = a.session->addAutomationLane(t, freq);
    REQUIRE(a.session->assign(lane, volume));
    a.session->removeDevice(eq);
    CHECK(a.lane(lane).assignments.size() == 1);
    CHECK_FALSE(a.session->laneOf(freq));
    a.session->undo();
    CHECK(a.lane(lane).assignments.size() == 2);

    // An assignment to a module that isn't there (an edit made elsewhere) goes when opening.
    juce::TemporaryFile project(Session::fileExtension);
    REQUIRE(a.session->saveAs(project.getFile()));
    auto xml = juce::XmlDocument::parse(project.getFile());
    REQUIRE(xml != nullptr);
    auto* laneXml = xml->getChildByName("FDAUTOMATION")->getChildByName("LANE");
    REQUIRE(laneXml != nullptr);
    auto* dangling = laneXml->createNewChildElement("ASSIGN");
    dangling->setAttribute("channel", juce::String(std::to_string(t)));
    dangling->setAttribute("key", "m99999/band1.freq");
    REQUIRE(xml->writeTo(project.getFile()));
    REQUIRE(a.session->open(project.getFile()));
    auto root = a.session->impl().lanesTree().getChild(0);
    int assigns = 0;
    for (auto c : root)
        assigns += c.hasType(Session::Impl::assignId) ? 1 : 0;
    CHECK(assigns == 2);
}

TEST_CASE("automation: armed, held controls write their lanes while playing, as one undo step", "[integration]")
{
    Automation a;
    // Playback driven by hand through tracktion's hosted device (no sound card in tests).
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = kRate;
    io.blockSize = 480;
    io.inputChannels = 0;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(a.engine, io);
    auto pump = [&](double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 100.0); ++i)
        {
            player.process(480); // 10 ms
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    auto record = [&](const Target& target, std::uint64_t channel, std::vector<float> dbs, double start) {
        a.session->setPosition(start);
        a.session->play();
        pump(0.2);
        REQUIRE(a.session->isPlaying());
        {
            Session::ParamGesture touch(*a.session, target, "Volume");
            for (float db : dbs)
            {
                a.session->setVolume(channel, db);
                pump(0.25);
            }
        }
        pump(0.3);
        a.session->stop();
        pump(0.3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
    };
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    const auto t0 = a.track(0), t1 = a.track(1);
    const Target v0{t0, "volume"}, v1{t1, "volume"};
    a.session->setAutomationArmed(true);
    CHECK(a.session->isAutomationArmed());

    SECTION("a control without a lane gets one under its track")
    {
        record(v0, t0, {-2.0f, -4.0f, -8.0f, -12.0f}, 0.0);
        const auto lane = a.session->laneOf(v0);
        REQUIRE(lane);
        const auto info = a.lane(*lane);
        CHECK(info.after == t0);
        CHECK(info.points.size() >= 2);
        CHECK(info.assignments.at(0).on); // recording isn't an override
        CHECK(*a.session->paramPlain(v0, info.points.back().value) == Approx(-12.0).margin(0.1));
        CHECK(info.points.back().time > info.points.front().time);
        CHECK(a.session->undoDescription() == "Record automation");
        a.session->undo();
        CHECK_FALSE(a.session->laneOf(v0));
    }
    SECTION("a pass into a lane with two parameters: spliced through the range, the other follows")
    {
        const auto lane = a.session->addAutomationLane(t0, v0);
        REQUIRE(a.session->assign(lane, v1, {1.0f, 0.0f})); // inverted
        const AutomationPoints points{{0.0, 0.5f, 0.0f}, {0.1, 0.55f, 0.0f}, {5.0, 0.6f, 0.3f}, {6.0, 0.7f, 0.0f}};
        REQUIRE(a.session->setLanePoints(lane, points));
        const auto before = a.session->undoDescription();
        record(v0, t0, {-3.0f, -9.0f}, 0.5);
        const auto info = a.lane(lane);
        // Before and after the pass the lane's points are its own, exactly.
        CHECK(info.points.front() == points[0]);
        CHECK(info.points[1] == points[1]);
        CHECK(info.points[info.points.size() - 2] == points[2]);
        CHECK(info.points.back() == points[3]);
        bool wrote = false;
        for (const auto& p : info.points)
            wrote = wrote || std::abs(*a.session->paramPlain(v0, p.value) + 9.0) < 0.1;
        CHECK(wrote);
        // The other parameter follows the new lane, inverted.
        for (double s = 0.0; s < 6.5; s += 0.05)
            CHECK(a.curve(v1, s) == Approx(1.0f - fd::model::automationValueAt(info.points, s)).margin(1e-5));
        CHECK(a.session->undoDescription() == "Record automation");
        a.session->undo();
        CHECK(a.lane(lane).points == points);
        CHECK(a.session->undoDescription() == before);
    }
    a.session->setAutomationArmed(false);
}
