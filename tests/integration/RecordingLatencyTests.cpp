// Hearing yourself sooner and recording in time (recording.md §22, M3 C7).
// Reduced latency when monitoring (§22.1): how late a monitored track's input is heard, with a
// module that adds delay on another track and on the track itself. tracktion delays a monitored
// track to line up with the song's compensation (the RL5 spike); with reduced latency on, it
// doesn't, and the track's own modules delaying 3 ms or more are left out, their power as set.
// Measuring the round trip (§22.2): the test signal through a cable the rig plays, found back.
#include "LoopDevice.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/MonitoringLatency.h"
#include "engine/Session.h"
#include "mcp/McpServer.h"
#include "mcp/protocol/Encoding.h"
#include "mcp/protocol/Schema.h"
#include "ui/RoundTripPanel.h"
#include "ui/SettingsView.h"
#include "ui/Shell.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <future>
#include <optional>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;

namespace
{
/// The engine playing through a hosted device: an impulse in, how many samples later it comes out.
struct Rig
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-monitoring-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    std::unique_ptr<tracktion::test_utilities::EnginePlayer> player;
    static constexpr int blockSize = 480;

    Rig()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        tracktion::HostedAudioDeviceInterface::Parameters io;
        io.sampleRate = 48000.0;
        io.blockSize = blockSize;
        io.inputChannels = 2;
        io.outputChannels = 2;
        player = std::make_unique<tracktion::test_utilities::EnginePlayer>(engine, io);
        silence(0.2);
    }
    ~Rig()
    {
        player.reset();
        session.reset();
    }

    void silence(double seconds)
    {
        juce::AudioBuffer<float> in(2, blockSize);
        for (int b = 0; b < static_cast<int>(seconds * 48000.0 / blockSize); ++b)
        {
            in.clear();
            player->process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    }

    /// A linear-phase EQ on `track` (a module that adds delay): its id.
    std::uint64_t addDelayingEq(std::uint64_t track)
    {
        const auto eq = session->addDevice(track, "fd:eq");
        auto handle = session->openDevice(eq);
        REQUIRE(handle != nullptr);
        auto& params = handle->context().params;
        const auto& specs = handle->context().info.params;
        const auto phase = static_cast<size_t>(
            std::distance(specs.begin(), std::find_if(specs.begin(), specs.end(),
                                                      [](const auto& p) { return p.id == "phase"; })));
        REQUIRE(phase < specs.size());
        params.beginGesture(phase);
        params.setPlain(phase, 1.0f); // linear phase
        params.endGesture(phase);
        silence(0.5);
        REQUIRE(session->moduleLatencyMs(eq) > 10.0);
        return eq;
    }

    /// The samples a module's delay is, at the rig's rate.
    [[nodiscard]] int samplesOf(std::uint64_t module) const
    {
        return static_cast<int>(std::lround(session->moduleLatencyMs(module) * 48.0));
    }

    /// An impulse into the input: the samples until the loudest output (-1: nothing came out).
    int delayOfImpulse()
    {
        juce::AudioBuffer<float> in(2, blockSize);
        int at = -1, sample = 0;
        float loudest = 0.05f;
        for (int b = 0; b < 100; ++b, sample += blockSize)
        {
            in.clear();
            if (b == 0)
                for (int c = 0; c < 2; ++c)
                    in.setSample(c, 0, 0.8f);
            const auto out = player->process(in);
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (const float v = std::abs(out.getSample(0, i)); v > loudest)
                {
                    loudest = v;
                    at = sample + i;
                }
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
        return at;
    }
};
} // namespace

namespace
{
bool enabledInSnapshot(Session& s, std::uint64_t module)
{
    s.updateSnapshotNow();
    for (const auto& t : s.snapshot()->tracks)
        for (const auto& d : t.devices)
            if (d.id == module)
                return d.enabled;
    return false;
}
} // namespace

TEST_CASE("reduced latency: a monitored track doesn't wait for another track's delay", "[integration]")
{
    Rig r;
    auto& s = *r.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id, t1 = s.snapshot()->tracks[1].id;
    REQUIRE_FALSE(s.audioInputs().empty());
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::In);
    r.silence(0.3);
    const int alone = r.delayOfImpulse();
    REQUIRE(alone >= 0);

    const auto eq = r.addDelayingEq(t1);
    const int compensated = r.delayOfImpulse();
    CHECK(compensated == alone + r.samplesOf(eq)); // as always: lined up with the EQ's track
    CHECK_FALSE(s.reducingLatency());

    s.setReducedLatency(true);
    r.silence(0.3);
    CHECK(s.reducedLatency());
    CHECK(s.reducingLatency());
    CHECK(r.delayOfImpulse() == alone);
    CHECK_FALSE(s.bypassedWhileMonitoring(eq)); // not on a monitored track

    // Monitoring stops: the track lines up again, and nothing's affected.
    s.setMonitor(t0, fd::model::Monitor::Off);
    r.silence(0.3);
    CHECK_FALSE(s.reducingLatency());
    s.setReducedLatency(false);
    s.setMonitor(t0, fd::model::Monitor::In);
    r.silence(0.3);
    CHECK(r.delayOfImpulse() == compensated);
}

TEST_CASE("reduced latency: a monitored track's module that adds delay is bypassed while it's monitored, its "
          "power as set",
          "[integration]")
{
    Rig r;
    auto& s = *r.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::Auto);
    s.setArmed(t0, true, true); // Auto: heard while armed
    r.silence(0.3);
    const int alone = r.delayOfImpulse();
    REQUIRE(alone >= 0);

    const auto eq = r.addDelayingEq(t0);
    CHECK(r.delayOfImpulse() == alone + r.samplesOf(eq));
    CHECK_FALSE(s.bypassedWhileMonitoring(eq));

    s.setReducedLatency(true);
    r.silence(0.3);
    CHECK(s.bypassedWhileMonitoring(eq));
    CHECK(r.delayOfImpulse() == alone);
    CHECK(enabledInSnapshot(s, eq)); // its power stays as set

    // Disarmed (Monitor on Auto): not monitored, so the EQ is back.
    s.setArmed(t0, false, false);
    r.silence(0.3);
    CHECK_FALSE(s.bypassedWhileMonitoring(eq));
    CHECK_FALSE(s.reducingLatency());

    // Switched off, a module isn't bypassed for it either (it's off anyway).
    s.setArmed(t0, true, true);
    s.setDeviceEnabled(eq, false);
    r.silence(0.3);
    CHECK_FALSE(s.bypassedWhileMonitoring(eq));
}

TEST_CASE("reduced latency is kept with the project, and bypasses as soon as it's open", "[integration]")
{
    Rig r;
    auto& s = *r.session;
    CHECK_FALSE(s.reducedLatency());
    s.setReducedLatency(true);
    CHECK_FALSE(s.canUndo()); // a setting, not an edit
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::In);
    r.silence(0.3);
    const int alone = r.delayOfImpulse();
    const auto eq = r.addDelayingEq(t0);
    juce::TemporaryFile saved(Session::fileExtension);
    REQUIRE(s.saveAs(saved.getFile()));
    s.setReducedLatency(false);

    REQUIRE(s.open(saved.getFile()));
    r.silence(0.3);
    CHECK(s.reducedLatency());
    CHECK(s.bypassedWhileMonitoring(eq));
    CHECK(r.delayOfImpulse() == alone);
}

TEST_CASE("reduced latency: a module added to a monitored track that adds delay at once is bypassed at once",
          "[integration]")
{
    Rig r;
    auto& s = *r.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::In);
    s.setReducedLatency(true);
    r.silence(0.3);
    const int alone = r.delayOfImpulse();

    // The Limiter looks 3 ms ahead from the start.
    const auto limiter = s.addDevice(t0, "fd:limiter");
    r.silence(0.3);
    REQUIRE(s.moduleLatencyMs(limiter) > 1.0);
    CHECK(s.bypassedWhileMonitoring(limiter));
    CHECK(r.delayOfImpulse() == alone);
}

TEST_CASE("reduced latency: a module delaying its track less than 3 ms stays in (a Saturator's oversampling)",
          "[integration]")
{
    Rig r;
    auto& s = *r.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::In);
    s.setReducedLatency(true);
    r.silence(0.3);
    const int alone = r.delayOfImpulse();
    const auto saturator = s.addDevice(t0, "fd:saturator");
    r.silence(0.3);
    const double ms = s.moduleLatencyMs(saturator);
    REQUIRE(ms > 0.0);
    CHECK(ms < fd::engine::kBypassFromMs);
    CHECK_FALSE(s.bypassedWhileMonitoring(saturator));
    CHECK(r.delayOfImpulse() == alone + r.samplesOf(saturator)); // heard through it, its delay kept
}

TEST_CASE("reduced latency: a rack holding a module that delays 3 ms or more is bypassed whole",
          "[integration]")
{
    Rig r;
    auto& s = *r.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::In);
    s.setReducedLatency(true);
    r.silence(0.3);
    const int alone = r.delayOfImpulse();
    const auto eq = r.addDelayingEq(t0);
    const auto rack = s.groupIntoRack({eq});
    REQUIRE(rack != 0);
    r.silence(0.3);
    CHECK(s.bypassedWhileMonitoring(rack));
    CHECK_FALSE(s.bypassedWhileMonitoring(eq)); // the rack is, as one module of the chain
    CHECK(r.delayOfImpulse() == alone);
}

namespace
{
/// The engine on an interface with a cable from its first output to its first input (LoopDevice).
struct LoopRig
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-roundtrip-tests", nullptr, fd::engine::makeEngineBehaviour()};
    fd::testing::LoopDevice* device = nullptr;
    std::unique_ptr<Session> session;

    LoopRig()
    {
        fd::engine::registerDevices(engine);
        device = &fd::testing::useLoopDevice(engine);
        session = std::make_unique<Session>(engine);
        runUntil([] { return false; }, 0.1);
    }
    ~LoopRig() { session.reset(); }

    /// Blocks until `done`, for at most `seconds` of audio.
    bool runUntil(const std::function<bool()>& done, double seconds)
    {
        for (int b = 0;
             b < static_cast<int>(seconds * fd::testing::LoopDevice::rate / fd::testing::LoopDevice::block) &&
             !done();
             ++b)
        {
            device->step();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
        for (int i = 0; i < 200 && !done(); ++i) // a try's search, on its own thread
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        return done();
    }
};

/// Measures the rig's round trip from its first output to its first input.
struct Measured
{
    std::optional<fd::model::RoundTripResult> result;
    std::vector<int> tries;
};
Measured measure(LoopRig& r, const std::function<void(int)>& onTry = {})
{
    Measured m;
    r.session->measureRoundTrip(
        0, 0,
        [&](int n)
        {
            m.tries.push_back(n);
            if (onTry)
                onTry(n);
        },
        [&](const fd::model::RoundTripResult& result) { m.result = result; });
    CHECK(r.session->measuringRoundTrip());
    REQUIRE(r.runUntil([&] { return m.result.has_value(); }, 10.0));
    CHECK_FALSE(r.session->measuringRoundTrip());
    return m;
}
} // namespace

TEST_CASE("the round trip: the test signal found back through a cable three times; the offset beyond what's "
          "reported, set by no one",
          "[integration]")
{
    LoopRig r;
    const auto channels = r.session->activeChannels();
    CHECK(channels.outputs == std::vector<std::string>{"Out 1", "Out 2"});
    CHECK(channels.inputs == std::vector<std::string>{"In 1", "In 2"});
    r.device->cable = 700;      // 14.6 ms
    r.device->reportedIn = 200; // the interface owns up to 10.4 ms of it
    r.device->reportedOut = 300;
    const auto m = measure(r);
    REQUIRE(m.result->ok());
    CHECK(m.tries == std::vector<int>{1, 2, 3});
    CHECK(m.result->roundTripMs == Approx(700.0 / 48.0).margin(0.05));
    CHECK(m.result->reportedMs == Approx(500.0 / 48.0));
    CHECK(m.result->offsetMs == Approx(200.0 / 48.0).margin(0.05));
    CHECK(m.result->spreadMs < 0.05);
    CHECK(r.session->recordingOffsets().first == 0.0); // Use sets it, not measuring
}

TEST_CASE("the round trip: its failures, named: nothing back, too loud, tries that disagree", "[integration]")
{
    using P = fd::model::RoundTripResult::Problem;
    {
        LoopRig r; // no cable
        const auto m = measure(r);
        CHECK(m.result->problem == P::NothingCameBack);
        CHECK(m.tries.size() == 1); // no use trying again
    }
    {
        LoopRig r;
        r.device->cable = 600;
        r.device->gain = 4.0f; // -12 dBFS comes back at 0 dBFS
        CHECK(measure(r).result->problem == P::TooLoud);
    }
    {
        LoopRig r;
        r.device->cable = 600;
        // The cable gets longer each try: 0.5 ms more.
        const auto m = measure(r, [&r](int n) { r.device->cable = 600 + 24 * (n - 1); });
        CHECK(m.result->problem == P::Disagree);
        CHECK(m.result->spreadMs == Approx(1.0).margin(0.05));
    }
    {
        // Cancelled: nothing more comes.
        LoopRig r;
        r.device->cable = 600;
        bool done = false;
        r.session->measureRoundTrip(0, 0, {}, [&](const auto&) { done = true; });
        r.runUntil([] { return false; }, 0.5);
        r.session->cancelRoundTrip();
        CHECK_FALSE(r.session->measuringRoundTrip());
        r.runUntil([&] { return done; }, 3.0);
        CHECK_FALSE(done);
    }
}

namespace
{
void dumpPng(const juce::String& name, juce::Component& c, juce::Rectangle<int> area)
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        const auto file = juce::File(dir).getChildFile(name);
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream(c.createComponentSnapshot(area, true, 2.0f), *out);
    }
}
} // namespace

TEST_CASE("the round trip in Settings: Measure… opens its panel under Audio offset; a failure named, then "
          "measured, Use sets the offset (RL6, RL7)",
          "[integration]")
{
    LoopRig r;
    r.device->reportedIn = 200;
    r.device->reportedOut = 300; // 10.4 ms
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        fd::app::Shell shell(*r.session);
        shell.setSize(1600, 960);
        shell.showSettings("recording.audioOffset");
        const auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); };
        settle();
        auto& view = shell.settingsView();
        const auto controls = view.controlsOf("recording.audioOffset");
        REQUIRE(controls.size() == 2);
        auto* measure = dynamic_cast<fd::ui::Toggle*>(controls[1]);
        REQUIRE(measure != nullptr);
        CHECK(measure->getButtonText() == juce::String::fromUTF8("Measure\xe2\x80\xa6"));
        CHECK(view.controlsOf("recording.midiOffset").size() == 1); // MIDI's stays by hand (RL8)
        CHECK(view.openPanel("recording.audioOffset") == nullptr);
        measure->triggerClick();
        settle();
        auto* panel = dynamic_cast<fd::app::RoundTripPanel*>(view.openPanel("recording.audioOffset"));
        REQUIRE(panel != nullptr);
        CHECK(panel->isVisible());

        // Under the row's description, the row's width, the next row below it; its parts apart.
        const auto row = view.rowBounds("recording.audioOffset");
        CHECK(row.contains(panel->getBounds()));
        CHECK(panel->getWidth() == row.getWidth());
        CHECK(panel->getHeight() == fd::app::RoundTripPanel::preferredHeight());
        CHECK(view.rowBounds("recording.midiOffset").getY() > panel->getBottom());
        std::vector<juce::Rectangle<int>> parts;
        for (auto* c : panel->getChildren())
            if (c->isVisible())
            {
                CHECK(panel->getLocalBounds()
                          .reduced(fd::ui::Theme::current().shell.groupGap)
                          .contains(c->getBounds()));
                for (const auto& other : parts)
                    CHECK_FALSE(other.intersects(c->getBounds()));
                parts.push_back(c->getBounds());
            }
        CHECK(panel->outputs().getText() == "Out 1");
        CHECK(panel->inputs().getText() == "In 1");
        CHECK(panel->resultText() == "The interface reports 10.4 ms");
        CHECK_FALSE(panel->useButton().isEnabled());
        auto& page = view.page();
        // Shot from the view, which paints the page's background.
        const auto shot = [&](const juce::String& name)
        {
            dumpPng("round-trip-" + name + "@2x.png", view,
                    view.getLocalArea(&page, panel->getBounds().expanded(0, 60)));
        };
        shot("open");

        // No cable: named, Use stays off.
        panel->measureButton().triggerClick();
        settle();
        CHECK(panel->resultText().startsWith(juce::String::fromUTF8("Measuring\xe2\x80\xa6 1 of 3")));
        CHECK_FALSE(panel->measureButton().isEnabled());
        shot("measuring");
        REQUIRE(r.runUntil([&] { return !r.session->measuringRoundTrip(); }, 10.0));
        CHECK(panel->resultText() == "Nothing came back: check the cable and the channels");
        CHECK_FALSE(panel->useButton().isEnabled());
        shot("failed");

        // A cable: the round trip, the offset; Use sets it and closes the panel.
        r.device->cable = 700;
        panel->measureButton().triggerClick();
        settle();
        REQUIRE(r.runUntil([&] { return !r.session->measuringRoundTrip(); }, 10.0));
        CHECK(panel->resultText() ==
              juce::String::fromUTF8("Round trip 14.6 ms \xc2\xb7 the interface reports 10.4 ms"));
        CHECK(panel->offsetLine() == "Offset +4.2 ms");
        CHECK(panel->useButton().isEnabled());
        shot("measured");
        panel->useButton().triggerClick();
        settle();
        CHECK(r.session->recordingOffsets().first == Approx(200.0 / 48.0).margin(0.05));
        CHECK(view.openPanel("recording.audioOffset") == nullptr);
        dumpPng("round-trip-used@2x.png", view,
                view.getLocalArea(&page, view.rowBounds("recording.audioOffset").expanded(0, 40)));

        // Cancel leaves the offset as it was (the row's controls fetched again: the page may have
        // made its rows again).
        r.session->setRecordingOffsets(0.0, 0.0);
        measure = dynamic_cast<fd::ui::Toggle*>(view.controlsOf("recording.audioOffset").at(1));
        REQUIRE(measure != nullptr);
        measure->triggerClick();
        settle();
        panel = dynamic_cast<fd::app::RoundTripPanel*>(view.openPanel("recording.audioOffset"));
        REQUIRE(panel != nullptr);
        CHECK(panel->resultText() == "The interface reports 10.4 ms"); // the last result gone
        panel->cancelButton().triggerClick();
        settle();
        CHECK(view.openPanel("recording.audioOffset") == nullptr);
        CHECK(r.session->recordingOffsets().first == 0.0);
        shell.closeSettings();
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE(
    "mcp tools: measure_latency measures the round trip through the cable and sets nothing; get_settings "
    "names the channels",
    "[integration][mcp]")
{
    using fd::mcp::Json;
    LoopRig r;
    r.device->cable = 700;
    r.device->reportedIn = 200;
    r.device->reportedOut = 300;
    juce::TemporaryFile folderName;
    const auto folder = folderName.getFile();
    REQUIRE(folder.createDirectory());
    fd::mcp::McpServer server(*r.session, folder, "test");
    REQUIRE(server.start(0));
    // A call from another thread, as transports make them, while this one runs the device.
    const auto call = [&](const std::string& tool, Json args = Json::object())
    {
        static int id = 1;
        Json params{{"name", tool},
                    {"arguments", std::move(args)},
                    {"_meta",
                     {{"io.modelcontextprotocol/protocolVersion", fd::mcp::modernVersion},
                      {"io.modelcontextprotocol/clientInfo", {{"name", "latency-tests"}, {"version", "1"}}},
                      {"io.modelcontextprotocol/clientCapabilities", Json::object()}}}};
        const Json request{
            {"jsonrpc", "2.0"}, {"id", id++}, {"method", "tools/call"}, {"params", std::move(params)}};
        auto future =
            std::async(std::launch::async, [&] { return server.dispatcher()->handle(request, {}); });
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            r.device->step();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
        const auto out = future.get();
        REQUIRE(out.message);
        const auto result = (*out.message)["result"];
        INFO(tool << ": " << result.dump());
        if (result["isError"] == false)
            if (const auto* t = server.registry().findTool(tool); t != nullptr && !t->outputSchema.is_null())
                CHECK(fd::mcp::validate(t->outputSchema, result["structuredContent"]) == std::nullopt);
        return result;
    };

    const auto settings = call("get_settings")["structuredContent"]["audio"];
    CHECK(settings["outputChannels"] == Json::array({"Out 1", "Out 2"}));
    CHECK(settings["inputChannels"] == Json::array({"In 1", "In 2"}));
    CHECK(settings["reportedLatencyMs"] == 10.4);

    const auto measured = call("measure_latency");
    REQUIRE(measured["isError"] == false);
    const auto& m = measured["structuredContent"];
    CHECK(m["problem"].is_null());
    CHECK(m["roundTripMs"] == 14.6);
    CHECK(m["reportedMs"] == 10.4);
    CHECK(m["offsetMs"] == 4.2);
    CHECK(r.session->recordingOffsets().first == 0.0); // set_settings sets it

    // The cable only joins Out 1 to In 1.
    const auto other = call("measure_latency", {{"output", "Out 2"}});
    REQUIRE(other["isError"] == false);
    CHECK(other["structuredContent"]["problem"] == "nothing came back");
    CHECK_FALSE(other["structuredContent"].contains("offsetMs"));
    const auto wrong = call("measure_latency", {{"input", "In 9"}});
    CHECK(wrong["isError"] == true);
    CHECK(wrong["content"][0]["text"].get<std::string>().find("In 1, In 2") != std::string::npos);
}
