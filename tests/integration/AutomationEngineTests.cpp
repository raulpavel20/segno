// Automation in the engine (M2 step C4a, ADR 0028, docs/design/automation.md): the curve plays
// as drawn, on the control's scale; lanes, undo, override, recording and projects.

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
using fd::model::AutomationPoint;
using fd::model::AutomationPoints;
using fd::testkit::AudioBuffer;

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

    tracktion::AutomatableParameter& param(const Session::LaneTarget& target)
    {
        auto lane = session->impl().lane(target);
        REQUIRE(lane);
        REQUIRE(lane->param != nullptr);
        return *lane->param;
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

    /// The value the engine plays at `seconds` (its playback iterator), in lane units.
    float played(const Session::LaneTarget& target, double seconds)
    {
        auto lane = session->impl().lane(target);
        REQUIRE(lane);
        tracktion::AutomationIterator it(*lane->param);
        it.setPosition(tracktion::TimePosition::fromSeconds(seconds));
        return lane->toLane(it.getCurrentValue());
    }
};

/// The channel's module key for a parameter of the first module of its chain.
std::string moduleLane(const fd::model::TrackInfo& t, size_t module, std::string_view param)
{
    return "m" + std::to_string(t.devices.at(module).id) + "/" + std::string(param);
}
} // namespace

TEST_CASE("automation: the lane's curve is what the engine draws and plays", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Session::LaneTarget volume{t, "volume"};
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> value(0.0f, 1.0f), bend(-1.0f, 1.0f);
    AutomationPoints points;
    double time = 0.25;
    for (int i = 0; i < 12; ++i, time += 0.3 + 0.1 * i)
        points.push_back({time, value(rng), i % 4 == 3 ? 0.0f : fd::model::curveForBend(bend(rng))});
    REQUIRE(a.session->setAutomation(volume, points));
    const auto& lane = *a.channel(t).lane("volume");
    REQUIRE(lane.points.size() == points.size());

    auto& curve = a.param(volume).getCurve();
    auto l = *a.session->impl().lane(volume);
    for (double s = 0.0; s < time + 1.0; s += 0.013)
    {
        const float model = fd::model::automationValueAt(lane.points, s);
        INFO("at " << s << " s");
        CHECK(l.toLane(curve.getValueAt(tracktion::TimePosition::fromSeconds(s), 0.0f)) == Approx(model).margin(1e-5));
        CHECK(a.played(volume, s) == Approx(model).margin(1e-5)); // patched: playback bends as drawn
    }
}

TEST_CASE("automation: a straight segment is a straight move of the control", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    REQUIRE(a.session->addDevice(t, "fd:eq") != 0);
    const auto& info = a.channel(t);
    // A frequency on a skewed knob: halfway along the segment is the knob's centre value.
    const Session::LaneTarget freq{t, moduleLane(info, 0, "band1.freq")};
    REQUIRE(a.session->setAutomation(freq, {{0.0, 0.0f, 0.0f}, {2.0, 1.0f, 0.0f}}));
    CHECK(a.played(freq, 1.0) == Approx(0.5f));
    const auto plain = a.session->lanePlain(freq, a.played(freq, 1.0));
    REQUIRE(plain);
    auto* eq = dynamic_cast<fd::engine::DevicePlugin*>(a.session->impl().plugin(info.devices[0].id));
    REQUIRE(eq != nullptr);
    const auto index = *eq->device().params().indexOf("band1.freq");
    CHECK(*plain == Approx(eq->device().info().params[index].fromNormalized(0.5f)));
    CHECK(*plain < 2000.0); // not the linear middle of 20 Hz .. 20 kHz

    // Volume: 0 dB to -24 dB passes the taper's halfway position halfway.
    const Session::LaneTarget volume{t, "volume"};
    const float top = *a.session->laneFromPlain(volume, 0.0), bottom = *a.session->laneFromPlain(volume, -24.0);
    REQUIRE(a.session->setAutomation(volume, {{0.0, top, 0.0f}, {2.0, bottom, 0.0f}}));
    CHECK(a.played(volume, 1.0) == Approx((top + bottom) / 2.0f));
    CHECK(*a.session->lanePlain(volume, top) == Approx(0.0).margin(1e-4));
}

TEST_CASE("automation: a rendered volume lane follows the drawn curve", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    a.place(t, fd::testkit::dc(static_cast<int>(12.0 * kRate), 0.5f));
    const Session::LaneTarget volume{t, "volume"};
    const float zero = *a.session->laneFromPlain(volume, 0.0), low = *a.session->laneFromPlain(volume, -12.0);
    // Holds, a slow ramp, a bend and a step down.
    const AutomationPoints points{{1.0, zero, 0.0f}, {9.0, low, fd::model::curveForBend(0.5f)}, {10.0, zero, 0.0f}};
    REQUIRE(a.session->setAutomation(volume, points));
    const auto& lane = a.channel(t).lane("volume")->points;
    const auto y = a.render(0.0, 11.0);
    auto renderedDb = [&](double s) { return 20.0 * std::log10(std::abs(y.at(0, static_cast<int>(s * kRate))) / 0.5); };
    auto drawnDb = [&](double s) { return *a.session->lanePlain(volume, fd::model::automationValueAt(lane, s)); };
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

TEST_CASE("automation: one point holds; steps step; settings that change latency have no lane", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Session::LaneTarget pan{t, "pan"};
    REQUIRE(a.session->setAutomation(pan, {{4.0, 0.25f, 0.0f}}));
    CHECK(a.param(pan).isAutomationActive()); // patched: a single point plays
    CHECK(a.played(pan, 0.0) == Approx(0.25f));
    CHECK(a.played(pan, 9.0) == Approx(0.25f));

    REQUIRE(a.session->addDevice(t, "fd:compressor") != 0);
    const auto& info = a.channel(t);
    // A choice: values snap to its steps and hold until the next point.
    const Session::LaneTarget character{t, moduleLane(info, 0, "character")};
    REQUIRE(a.session->setAutomation(character, {{0.0, 0.1f, 0.0f}, {2.0, 0.9f, 0.0f}}));
    const auto* lane = a.channel(t).lane(character.lane);
    REQUIRE(lane != nullptr);
    CHECK(lane->steps == 2); // two characters
    CHECK(lane->points[0].value == 0.0f);
    CHECK(lane->points[1].value == 1.0f);
    CHECK(lane->points[0].curve == fd::model::kStepCurve);
    CHECK(a.played(character, 1.9) == 0.0f);
    CHECK(a.played(character, 2.01) == 1.0f);

    const Session::LaneTarget lookahead{t, moduleLane(info, 0, "lookahead")};
    CHECK_FALSE(a.session->setAutomation(lookahead, {{0.0, 1.0f, 0.0f}}));
    const auto choices = a.session->automatableParameters(t);
    CHECK(std::none_of(choices.begin(), choices.end(), [&](const auto& c) { return c.lane == lookahead.lane; }));
    CHECK(std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.lane == "volume"; }));
}

TEST_CASE("automation: edits are undo steps; lanes, on switches and folds are not", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Session::LaneTarget volume{t, "volume"};
    REQUIRE(a.session->setAutomation(volume, {{1.0, 0.5f, 0.0f}, {2.0, 0.7f, 0.0f}}, std::nullopt, "Add point"));
    CHECK(a.session->undoDescription() == "Add point");
    CHECK(a.channel(t).lane("volume")->points.size() == 2);

    // A range replaces only what's inside it.
    REQUIRE(a.session->setAutomation(volume, {{1.5, 0.1f, 0.0f}}, std::pair{1.2, 3.0}, "Draw automation"));
    auto points = a.channel(t).lane("volume")->points;
    REQUIRE(points.size() == 2);
    CHECK(points[0].time == Approx(1.0));
    CHECK(points[1].time == Approx(1.5));

    a.session->undo();
    CHECK(a.channel(t).lane("volume")->points.size() == 2);
    CHECK(a.channel(t).lane("volume")->points[1].time == Approx(2.0));

    // Shown empty lanes, folding and on switches are view or listening state.
    const auto before = a.session->undoDescription();
    REQUIRE(a.session->showLane({t, "pan"}));
    a.session->setLanesFolded(t, true);
    a.session->setLaneOn(volume, false);
    CHECK(a.session->undoDescription() == before);
    CHECK(a.channel(t).lanesFolded);
    CHECK(a.channel(t).lane("pan") != nullptr);
    CHECK_FALSE(a.channel(t).lane("volume")->on);
    CHECK(a.session->anyLaneOff());

    // Removing a lane takes its points with it, in one step.
    a.session->removeLane(volume);
    CHECK(a.channel(t).lane("volume") == nullptr);
    CHECK(a.session->undoDescription() == "Remove automation");
    a.session->undo();
    CHECK(a.channel(t).lane("volume") != nullptr);
}

TEST_CASE("automation: moving an automated control turns its lane off until it's back on", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Session::LaneTarget volume{t, "volume"};
    REQUIRE(a.session->setAutomation(volume, {{0.0, 0.2f, 0.0f}, {4.0, 0.9f, 0.0f}}));
    CHECK(a.channel(t).lane("volume")->on);

    a.session->setVolume(t, -3.0f); // a move of the fader (not recording)
    CHECK_FALSE(a.channel(t).lane("volume")->on);
    CHECK(a.channel(t).lane("volume")->points.size() == 2); // nothing lost
    CHECK(a.channel(t).mix.volumeDb == Approx(-3.0f).margin(0.01f)); // the value set by hand stays
    CHECK(a.session->anyLaneOff());
    CHECK(a.session->lastTouchedParameter() == Session::LaneTarget{t, "volume"});

    a.session->setAllLanesOn();
    CHECK(a.channel(t).lane("volume")->on);
    CHECK_FALSE(a.session->anyLaneOff());

    // Switching a send between pre and post isn't a move of its control.
    a.session->addReturnTrack();
    const auto r = a.channel(t).mix.sends.empty() ? 0 : a.session->snapshot()->returns.at(0).id;
    REQUIRE(r != 0);
    const Session::LaneTarget send{t, "send/" + std::to_string(r)};
    REQUIRE(a.session->setAutomation(send, {{0.0, 0.5f, 0.0f}, {1.0, 1.0f, 0.0f}})); // makes the send
    REQUIRE(a.session->setSend(t, r, std::nullopt, true));
    CHECK(a.channel(t).lane(send.lane)->on);
}

TEST_CASE("automation: played values never mark the project changed or rebuild snapshots", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    const Session::LaneTarget volume{t, "volume"};
    REQUIRE(a.session->setAutomation(volume, {{0.0, 0.2f, 0.0f}, {4.0, 0.9f, 0.0f}}));
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

TEST_CASE("automation: lanes are saved with the project; older projects keep their sound", "[integration]")
{
    Automation a;
    const auto t = a.track(0);
    REQUIRE(a.session->addDevice(t, "fd:eq") != 0);
    const auto& info = a.channel(t);
    const Session::LaneTarget freq{t, moduleLane(info, 0, "band1.freq")};
    REQUIRE(a.session->setAutomation(freq, {{0.5, 0.3f, fd::model::curveForBend(0.4f)}, {1.5, 0.8f, 0.0f}}));
    REQUIRE(a.session->showLane({t, "pan"}));
    a.session->setLaneOn(freq, false);
    a.session->setLanesFolded(t, true);
    a.session->setVolume(t, -6.0f);
    const auto saved = a.channel(t);

    juce::TemporaryFile project(Session::fileExtension);
    REQUIRE(a.session->saveAs(project.getFile()));
    REQUIRE(a.session->open(project.getFile()));
    const auto& loaded = a.channel(saved.id);
    REQUIRE(loaded.lanes.size() == saved.lanes.size());
    for (size_t i = 0; i < saved.lanes.size(); ++i)
    {
        CHECK(loaded.lanes[i].key == saved.lanes[i].key);
        CHECK(loaded.lanes[i].on == saved.lanes[i].on);
        REQUIRE(loaded.lanes[i].points.size() == saved.lanes[i].points.size());
        for (size_t p = 0; p < saved.lanes[i].points.size(); ++p)
        {
            CHECK(loaded.lanes[i].points[p].time == Approx(saved.lanes[i].points[p].time));
            CHECK(loaded.lanes[i].points[p].value == Approx(saved.lanes[i].points[p].value));
            CHECK(loaded.lanes[i].points[p].curve == Approx(saved.lanes[i].points[p].curve));
        }
    }
    CHECK(loaded.lanesFolded);
    CHECK(loaded.mix.volumeDb == Approx(-6.0f).margin(0.01f));

    // A project from before automation: levels on tracktion's own fader curve, module
    // parameters as plain values. It opens with the same levels and settings.
    auto xml = juce::XmlDocument::parse(project.getFile());
    REQUIRE(xml != nullptr);
    xml->removeAttribute("fdFormat");
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
        if (plugin->getStringAttribute("type") == "volume")
            plugin->setAttribute("volume", std::exp((-6.0 - 6.0) / 20.0)); // -6 dB, old curve
        if (plugin->getStringAttribute("type") == "fd.eq")
        {
            plugin->removeAttribute("fdNormalised");
            plugin->setAttribute("band1.gain", 4.5); // plain dB
            gainDb = 4.5f;
            plugin->deleteAllChildElementsWithTagName("AUTOMATIONCURVE");
        }
    }
    REQUIRE(gainDb != 0.0f);
    REQUIRE(xml->writeTo(project.getFile()));
    REQUIRE(a.session->open(project.getFile()));
    const auto& old = a.channel(saved.id);
    CHECK(old.mix.volumeDb == Approx(-6.0f).margin(0.01f));
    auto handle = a.session->openDevice(old.devices.at(0).id);
    REQUIRE(handle != nullptr);
    auto& ctx = handle->context();
    size_t gainIndex = 0;
    for (size_t i = 0; i < ctx.info.params.size(); ++i)
        if (ctx.info.params[i].id == "band1.gain")
            gainIndex = i;
    CHECK(ctx.params.getPlain(gainIndex) == Approx(4.5f).margin(1e-4));
    CHECK_FALSE(a.session->hasUnsavedChanges());
}

TEST_CASE("automation: armed, a held control writes its lane while playing, as one undo step", "[integration]")
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
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    const auto t = a.track(0);
    const Session::LaneTarget volume{t, "volume"};
    a.session->setAutomationArmed(true);
    CHECK(a.session->isAutomationArmed());
    const auto before = a.session->undoDescription();
    a.session->play();
    pump(0.2);
    REQUIRE(a.session->isPlaying());
    {
        Session::LaneGesture touch(*a.session, volume, "Volume");
        for (float db : {-2.0f, -4.0f, -8.0f, -12.0f})
        {
            a.session->setVolume(t, db);
            pump(0.25);
        }
    }
    pump(0.3);
    a.session->stop();
    pump(0.3);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
    const auto* lane = a.channel(t).lane("volume");
    REQUIRE(lane != nullptr); // shown, since it has points
    CHECK(lane->points.size() >= 2);
    CHECK(lane->on); // recording isn't an override
    CHECK(*a.session->lanePlain(volume, lane->points.back().value) == Approx(-12.0).margin(0.1));
    CHECK(lane->points.back().time > lane->points.front().time);
    CHECK(a.session->undoDescription() == "Record automation");
    a.session->undo();
    CHECK(a.channel(t).lane("volume") == nullptr);
    a.session->setAutomationArmed(false);
}
