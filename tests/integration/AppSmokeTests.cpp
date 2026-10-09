// Smoke tests of the DAW shell (docs/design/daw-shell.md, C1): it builds offscreen around a
// Session, every action has a unique id and shortcut, and keys reach the registry.

#include "engine/EngineBehaviour.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "mcp/McpServer.h"
#include "model/Editing.h"
#include "model/ChannelStripLayout.h"
#include "model/Selection.h"
#include "ui/AutomationMenu.h"
#include "ui/ChannelControls.h"
#include "ui/ChannelInspector.h"
#include "ui/DeviceChain.h"
#include "ui/MixerView.h"
#include "ui/Shell.h"
#include "ui/TrackInputs.h"

#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

using fd::mcp::Json;

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
struct ShellFixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<fd::engine::Session> session;
    std::unique_ptr<fd::app::Shell> shell;

    ShellFixture()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
        shell = std::make_unique<fd::app::Shell>(*session);
    }

    ~ShellFixture()
    {
        shell.reset();
        session.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
};
} // namespace

namespace
{
juce::MouseEvent mouseEvent(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged,
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
{
    return {juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, dragged};
}

/// A click (or, with clicks = 2, the second click of a double-click) at `pos`.
void click(juce::Component& c, juce::Point<float> pos, int clicks = 1)
{
    c.mouseDown(mouseEvent(c, pos, pos, false, juce::ModifierKeys::leftButtonModifier, clicks));
    c.mouseUp(mouseEvent(c, pos, pos, false, juce::ModifierKeys::leftButtonModifier, clicks));
}

/// Press at `from`, drag to `to` in a few steps, release.
void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown(mouseEvent(c, from, from, false));
    for (float k : {0.25f, 0.5f, 1.0f})
        c.mouseDrag(mouseEvent(c, from + (to - from) * k, from, true));
    c.mouseUp(mouseEvent(c, to, from, true));
}
} // namespace

TEST_CASE("DAW shell builds and paints offscreen at standard and compact widths", "[integration]")
{
    ShellFixture f;
    // Some content: two clips of a generated loop.
    juce::TemporaryFile wav(".wav");
    {
        juce::AudioBuffer<float> buffer(2, 96000);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                buffer.setSample(c, i, 0.4f * std::sin(0.03f * static_cast<float>(i)) * std::exp(-static_cast<float>(i % 24000) / 6000.0f));
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> out = wav.getFile().createOutputStream();
        auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(48000.0).withNumChannels(2).withBitsPerSample(24));
        REQUIRE(writer != nullptr);
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
    const auto snapshot = f.session->snapshot();
    f.session->insertAudioFile(snapshot->tracks[0].id, wav.getFile(), 0.0);
    f.session->insertAudioFile(snapshot->tracks[2].id, wav.getFile(), 4.0);
    f.session->addDevice(snapshot->tracks[0].id, "fd:eq");
    f.session->addDevice(snapshot->tracks[0].id, "fd:saturator");
    f.session->updateSnapshotNow();
    f.session->setPosition(3.0);

    for (int width : {1800, 1200})
    {
        f.shell->setSize(width, 900);
        // Select the first track (its chain shows in the detail panel).
        for (auto* child : f.shell->getChildren())
            if (auto* view = dynamic_cast<fd::app::ArrangementView*>(child))
            {
                const juce::Point<float> header(40.0f, static_cast<float>(fd::app::ArrangementView::rulerHeight()) + 20.0f);
                view->mouseDown(mouseEvent(*view, header, header, false));
                view->mouseUp(mouseEvent(*view, header, header, false));
            }
        const auto image = f.shell->createComponentSnapshot(f.shell->getLocalBounds());
        CHECK(image.isValid());
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
        {
            juce::PNGImageFormat png;
            auto out = juce::File(dump).getChildFile("shell-" + juce::String(width) + ".png").createOutputStream();
            if (out != nullptr)
                png.writeImageToStream(image, *out);
            // The transport bar at 2x, as on the reference displays.
            auto top = juce::File(dump).getChildFile("transport-" + juce::String(width) + "@2x.png").createOutputStream();
            if (top != nullptr)
                png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds().removeFromTop(40), true, 2.0f), *top);
        }
    }
    CHECK_FALSE(f.session->isPlaying()); // never starts making sound by itself
}

namespace
{
/// Every child of `root` (depth first) of type T.
/// Runs a menu item by its text, in submenus too ("Assign to automation lane" > "Swell"); false
/// if there's none.
bool invoke(const juce::PopupMenu& menu, const juce::String& text, const juce::String& in = {})
{
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        auto& item = it.getItem();
        if (item.subMenu != nullptr)
        {
            if ((in.isEmpty() || item.text == in) && invoke(*item.subMenu, text))
                return true;
            continue;
        }
        if (in.isEmpty() && item.text == text && item.isEnabled && item.action)
        {
            item.action();
            return true;
        }
    }
    return false;
}

template <typename T>
std::vector<T*> findAll(juce::Component& root)
{
    std::vector<T*> found;
    for (auto* child : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(child))
            found.push_back(t);
        auto deeper = findAll<T>(*child);
        found.insert(found.end(), deeper.begin(), deeper.end());
    }
    return found;
}
} // namespace

TEST_CASE("the mixer in the arrangement: header controls, returns and master, the inspector (C3b)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 900);
    auto& view = f.shell->arrangement();
    view.setAllTrackHeights(96.0); // tall enough for the volume and pan row
    auto& actions = f.shell->actions();
    REQUIRE(actions.perform("track.addReturn"));
    f.session->updateSnapshotNow();
    auto snap = f.session->snapshot();
    REQUIRE(snap->returns.size() == 1);
    CHECK(view.selectedTrack() == snap->returns[0].id); // the new return is selected
    view.setSnapshot(snap);
    view.resized();
    view.frame(1.0 / 60.0, 0.0);

    // Every row has its controls: mute, solo and (tracks) arm; none on the master.
    const auto toggles = findAll<fd::app::ChannelToggle>(view);
    CHECK(toggles.size() == 3 * snap->tracks.size() + 2 * snap->returns.size());
    CHECK(findAll<fd::app::VolumeBar>(view).size() == snap->tracks.size() + snap->returns.size() + 1);
    CHECK(findAll<fd::app::ChannelMeter>(view).size() == snap->tracks.size() + snap->returns.size() + 1);

    // The first track's mute toggle mutes it (one undo step).
    const auto first = snap->tracks[0].id;
    fd::app::ChannelToggle* mute = nullptr;
    for (auto* t : toggles)
        if (t->getButtonText() == "M" && t->isVisible() && t->getY() < 40)
            mute = t;
    REQUIRE(mute != nullptr);
    mute->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.mute);
    CHECK(f.session->undoDescription() == "Mute");

    // Alt+S solos the selected channel; Ctrl+Alt+R hides and shows the returns and master.
    view.selectTrack(first);
    REQUIRE(actions.perform("track.solo"));
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.solo);
    REQUIRE(actions.perform("view.returns"));
    CHECK_FALSE(view.mixRowsVisible());
    view.setSnapshot(f.session->snapshot());
    CHECK(findAll<fd::app::VolumeBar>(view).size() == f.session->snapshot()->tracks.size());
    REQUIRE(actions.perform("view.returns"));
    CHECK(view.mixRowsVisible());

    // The inspector shows the selected track's channel: its volume bar edits the volume, and
    // there is a send to the return.
    view.selectTrack(std::nullopt);
    view.selectTrack(first);
    auto mixes = findAll<fd::app::ChannelMix>(f.shell->inspector());
    REQUIRE(mixes.size() == 1);
    auto bars = findAll<fd::app::VolumeBar>(*mixes[0]);
    REQUIRE(bars.size() == 1);
    bars[0]->setValue(0.64f, juce::sendNotification); // -6 dB on the taper
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.volumeDb == Catch::Approx(-6.0f).margin(0.01f));
    CHECK(findAll<fd::app::SendBar>(f.shell->inspector()).size() == 1);

    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review
    {
        view.setSnapshot(f.session->snapshot());
        view.frame(1.0 / 60.0, 0.0);
        auto file = juce::File(dump).getChildFile("mixer-arrangement.png");
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
    }
}

TEST_CASE("the mixer view: strips side by side, shared selection, a fader drag is one step (C3c)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 900);
    auto& actions = f.shell->actions();
    REQUIRE(actions.perform("track.addReturn"));
    const auto t0 = f.session->snapshot()->tracks[0].id;
    REQUIRE(f.session->addDevice(t0, "fd:eq") != 0);
    REQUIRE(f.session->addDevice(t0, "fd:compressor") != 0);
    f.session->updateSnapshotNow();
    auto snap = f.session->snapshot();
    REQUIRE(f.session->addDevice(snap->master.id, "fd:limiter") != 0);
    f.session->updateSnapshotNow();
    snap = f.session->snapshot();

    auto& arrangement = f.shell->arrangement();
    arrangement.setView(2.0, 80.0);
    REQUIRE(actions.perform("view.mixer"));
    CHECK(actions.isToggled("view.mixer"));
    auto mixers = findAll<fd::app::MixerView>(*f.shell);
    REQUIRE(mixers.size() == 1);
    auto& mixer = *mixers[0];
    CHECK(mixer.isVisible());
    CHECK_FALSE(arrangement.isVisible());
    mixer.setSnapshot(snap);

    // A strip per channel: the tracks, the return, and the master.
    const auto strips = findAll<fd::app::ChannelStrip>(mixer);
    CHECK(strips.size() == snap->tracks.size() + snap->returns.size() + 1);

    // A click on a strip selects its channel everywhere.
    fd::app::ChannelStrip* second = nullptr;
    for (auto* s : strips)
        if (s->channel() == snap->tracks[1].id)
            second = s;
    REQUIRE(second != nullptr);
    click(*second, {20.0f, 12.0f});
    CHECK(arrangement.selectedTrack() == snap->tracks[1].id);
    // Left and Right move the selection along the strips.
    CHECK(mixer.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(arrangement.selectedTrack() == snap->tracks[2].id);

    // A fader drag is one undo step, on the taper.
    auto faders = findAll<fd::app::VolumeBar>(*strips[0]);
    REQUIRE(faders.size() == 1);
    auto& fader = *faders[0];
    CHECK(fader.style() == fd::ui::Knob::Style::Fader);
    const auto before = f.session->undoDescription();
    const auto h = static_cast<float>(fader.getHeight());
    drag(fader, {12.0f, h * 0.5f}, {12.0f, h * 0.3f});
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.volumeDb > 0.0f);
    CHECK(f.session->undoDescription() == "Volume");
    f.session->undo();
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.volumeDb == Catch::Approx(0.0f).margin(0.01f)); // the whole drag
    CHECK(f.session->undoDescription() == before);

    // The header's view options change every strip.
    fd::ui::ViewOption* narrowOption = nullptr;
    for (auto* option : findAll<fd::ui::ViewOption>(mixer))
        if (option->getButtonText() == "Narrow")
            narrowOption = option;
    REQUIRE(narrowOption != nullptr);
    CHECK_FALSE(narrowOption->getToggleState());
    narrowOption->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(mixer.options().narrow);
    CHECK(findAll<fd::app::ChannelStrip>(mixer)[0]->getWidth() == 64);

    // The options are view state, saved with the project; switching back keeps the arrangement's view.
    mixer.setOptions(true, true, true);
    const auto state = nlohmann::json::parse(f.session->viewStateProvider());
    CHECK(state["view"] == "mix");
    CHECK(state["mixerNarrow"] == true);
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review, wide and narrow
        for (bool narrow : {false, true})
        {
            mixer.setOptions(true, true, narrow);
            auto file = juce::File(dump).getChildFile(narrow ? "mixer-view-narrow.png" : "mixer-view.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            if (auto out = file.createOutputStream())
                png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
        }
    REQUIRE(actions.perform("view.mixer"));
    CHECK(arrangement.isVisible());
    CHECK(arrangement.timeline().start() == Catch::Approx(2.0));

    // A new project while the headers, the inspector and the strips hold the engine's meters:
    // they are let go first, and come back for the new project.
    arrangement.selectTrack(snap->tracks[0].id);
    f.session->newProject();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(f.session->snapshot()->returns.empty());
    CHECK(findAll<fd::app::ChannelMeter>(arrangement).size() == f.session->snapshot()->tracks.size() + 1);
}

namespace
{
constexpr float kRowHeight = 64.0f;

/// Where a row's header is, and a time in its lane, once every row is kRowHeight tall.
juce::Point<float> headerOf(int row)
{
    return {40.0f, static_cast<float>(fd::app::ArrangementView::rulerHeight()) + kRowHeight * (static_cast<float>(row) + 0.5f)};
}

juce::Point<float> laneAt(const fd::app::ArrangementView& view, double seconds, int row)
{
    return {static_cast<float>(fd::app::ArrangementView::headerWidth()) + static_cast<float>(view.timeline().xForTime(seconds)),
            headerOf(row).y};
}
} // namespace

TEST_CASE("one selection: the context panels follow the last click in the arrangement (A5)", "[integration]")
{
    using S = fd::model::InspectorSection;
    using Mode = fd::app::DetailPanel::Mode;
    using Selection = fd::model::Selection;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& actions = f.shell->actions();
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    auto& inspector = f.shell->inspector();
    REQUIRE(actions.perform("track.addReturn"));
    f.session->updateSnapshotNow();
    const auto snap = f.session->snapshot();
    REQUIRE(snap->tracks.size() >= 2);
    const auto t0 = snap->tracks[0].id, t1 = snap->tracks[1].id;
    const auto rows = static_cast<int>(snap->tracks.size());
    view.setAllTrackHeights(kRowHeight);
    int told = 0;
    const auto shellHandler = view.onSelectionChanged;
    view.onSelectionChanged = [&] {
        ++told;
        shellHandler();
    };

    // A MIDI clip on the first track: the clip view, and the clip's sections in the inspector.
    const auto clip = view.createMidiClip(0, 0.0);
    REQUIRE(clip != 0);
    f.session->updateSnapshotNow();
    CHECK(view.selection() == Selection{.channel = t0, .clips = {clip}});
    CHECK(detail.mode() == Mode::Clip);
    CHECK(detail.showsPianoRoll());
    CHECK(inspector.sections() == std::vector{S::Clip, S::Track, S::Input, S::Mix, S::Sends});
    CHECK_FALSE(f.session->midiInputTarget()); // no instrument came with it (ADR 0033): nothing to play

    // Another track's header: that track alone, in both panels, told once.
    told = 0;
    click(view, headerOf(1));
    CHECK(told == 1);
    CHECK(view.selection() == Selection{.channel = t1});
    CHECK(detail.mode() == Mode::Devices);
    CHECK(detail.chain().track() == t1);
    CHECK(inspector.sections() == std::vector{S::Track, S::Input, S::Mix, S::Sends});
    CHECK(f.session->midiInputTarget() != t0);

    // The clip again, then its own track's header: the track alone.
    const auto onClip = laneAt(view, 0.5, 0);
    click(view, onClip);
    CHECK(view.selection() == Selection{.channel = t0, .clips = {clip}});
    CHECK(detail.mode() == Mode::Clip);
    click(view, headerOf(0));
    CHECK(view.selection() == Selection{.channel = t0});
    CHECK(detail.mode() == Mode::Devices);
    CHECK(detail.chain().track() == t0);

    // Tab picks the other view until the subject changes; a new snapshot doesn't change it.
    click(view, onClip);
    REQUIRE(actions.perform("view.detailMode"));
    CHECK(detail.mode() == Mode::Devices);
    f.session->updateSnapshotNow();
    CHECK(detail.mode() == Mode::Devices);

    // The return and master rows, by lane and by header: their channel alone.
    click(view, laneAt(view, 6.0, rows));
    CHECK(view.selection() == Selection{.channel = snap->returns[0].id});
    CHECK(detail.chain().track() == snap->returns[0].id);
    CHECK(inspector.sections() == std::vector{S::Return, S::Mix});
    click(view, onClip);
    click(view, headerOf(rows + 1));
    CHECK(view.selection() == Selection{.channel = snap->master.id});
    CHECK(inspector.sections() == std::vector{S::Master, S::Mix});

    // An empty spot in a lane: that track, no clips.
    click(view, onClip);
    click(view, laneAt(view, 6.0, 1));
    CHECK(view.selection() == Selection{.channel = t1});

    // Track actions act on what the panels show.
    click(view, onClip);
    REQUIRE(actions.perform("track.mute"));
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.mute);
    CHECK_FALSE(f.session->snapshot()->tracks[1].mix.mute);
}

TEST_CASE("one selection in the mixer view: modules only, no inspector, the chain follows (A5)", "[integration]")
{
    using Mode = fd::app::DetailPanel::Mode;
    using Selection = fd::model::Selection;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& actions = f.shell->actions();
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    auto& inspector = f.shell->inspector();
    auto& mixer = f.shell->mixer();
    REQUIRE(actions.perform("track.addReturn"));
    f.session->updateSnapshotNow();
    const auto snap = f.session->snapshot();
    REQUIRE(snap->tracks.size() >= 3);
    const auto t0 = snap->tracks[0].id, t1 = snap->tracks[1].id, t2 = snap->tracks[2].id;
    const auto eq = f.session->addDevice(t1, "fd:eq");
    REQUIRE(eq != 0);
    REQUIRE(f.session->addDevice(t1, "fd:compressor") != 0);
    view.setAllTrackHeights(kRowHeight);
    const auto clip = view.createMidiClip(0, 0.0);
    REQUIRE(clip != 0);
    f.session->updateSnapshotNow();
    REQUIRE(detail.mode() == Mode::Clip);
    REQUIRE(inspector.isVisible());
    const int arrangeWidth = view.getWidth();

    // Mix: the clip's track's modules under a plain title, and no inspector.
    REQUIRE(actions.perform("view.mixer"));
    CHECK_FALSE(detail.clipViewAvailable());
    CHECK(detail.mode() == Mode::Devices);
    CHECK(detail.chain().track() == t0);
    CHECK_FALSE(inspector.isVisible());
    CHECK(inspector.sections().empty()); // its meters are let go
    CHECK(mixer.getWidth() > arrangeWidth);
    CHECK_FALSE(actions.isToggled("view.inspector"));
    CHECK(nlohmann::json::parse(f.session->viewStateProvider())["inspector"] == true); // the arrangement's

    // Nothing acts on the arrangement's content you can't see; Tab keeps the focus on the strips.
    for (const auto* id : {"view.detailMode", "view.inspector", "edit.delete", "edit.duplicate", "edit.split",
                           "edit.selectAll", "clip.newMidi", "view.zoomIn", "view.zoomFit", "view.returns"})
    {
        INFO(id);
        CHECK_FALSE(actions.perform(id));
    }
    f.session->updateSnapshotNow();
    CHECK(fd::model::findClip(*f.session->snapshot(), clip));
    CHECK(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::tabKey)));
    CHECK(detail.mode() == Mode::Devices);

    // A module name on another strip, with the panel closed: it opens on that module.
    auto stripOf = [&](std::uint64_t id) -> fd::app::ChannelStrip& {
        for (auto* s : findAll<fd::app::ChannelStrip>(mixer))
            if (s->channel() == id)
                return *s;
        FAIL("no strip");
        throw;
    };
    REQUIRE(actions.perform("view.detail"));
    REQUIRE_FALSE(actions.isToggled("view.detail"));
    auto& strip = stripOf(t1);
    const auto slots = fd::model::layoutChannelStrip(strip.getHeight(), mixer.options());
    const auto& name = slots.modules.at(0);
    click(strip, {static_cast<float>(name.x + name.w / 2), static_cast<float>(name.y + name.h / 2)});
    CHECK(actions.isToggled("view.detail"));
    CHECK(view.selection() == Selection{.channel = t1}); // the clip is no longer selected
    CHECK(detail.chain().track() == t1);
    CHECK(detail.chain().selectedDevice() == eq);

    // Delete removes that module: the chain was used last.
    REQUIRE(actions.perform("edit.delete"));
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[1].devices.size() == 1);

    // The arrow keys and other strips: the chain follows every time.
    CHECK(mixer.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(detail.chain().track() == t2);
    click(stripOf(t0), {20.0f, 12.0f});
    CHECK(detail.chain().track() == t0);
    CHECK(view.selection() == Selection{.channel = t0});

    // Back in the arrangement: the inspector as it was, the clip view for a MIDI clip again.
    REQUIRE(actions.perform("view.mixer"));
    CHECK(inspector.isVisible());
    CHECK(detail.clipViewAvailable());
    CHECK(inspector.sections().size() == 4); // track, input, mix, sends
    click(view, laneAt(view, 0.5, 0));
    CHECK(detail.mode() == Mode::Clip);
    REQUIRE(actions.perform("view.mixer"));
    CHECK(detail.mode() == Mode::Devices);
    REQUIRE(actions.perform("view.mixer"));
    CHECK(detail.mode() == Mode::Clip);
    CHECK(detail.showsPianoRoll());
    // Tab's choice survives the trip too.
    REQUIRE(actions.perform("view.detailMode"));
    REQUIRE(actions.perform("view.mixer"));
    REQUIRE(actions.perform("view.mixer"));
    CHECK(detail.mode() == Mode::Devices);
}

TEST_CASE("the arrangement's selection: the channel follows the clicked clip, told once per gesture (A5)", "[integration]")
{
    using Selection = fd::model::Selection;
    ShellFixture f;
    const auto snap = f.session->snapshot();
    REQUIRE(snap->tracks.size() >= 3);
    const auto t0 = snap->tracks[0].id, t1 = snap->tracks[1].id, t2 = snap->tracks[2].id;
    const auto a = f.session->createMidiClip(t0, 0.0, 1.0);
    const auto b = f.session->createMidiClip(t2, 0.0, 1.0);
    const auto c = f.session->createMidiClip(t2, 4.0, 1.0);
    f.session->updateSnapshotNow();

    fd::app::ArrangementView view(*f.session);
    view.setSize(1200, 600);
    view.setSnapshot(f.session->snapshot());
    view.setAllTrackHeights(kRowHeight);
    int told = 0;
    view.onSelectionChanged = [&] { ++told; };
    const juce::ModifierKeys shift(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    auto shiftClick = [&](juce::Point<float> p) {
        view.mouseDown(mouseEvent(view, p, p, false, shift));
        view.mouseUp(mouseEvent(view, p, p, false, shift));
    };

    click(view, laneAt(view, 0.5, 2));
    CHECK(view.selection() == Selection{.channel = t2, .clips = {b}});
    CHECK(told == 1);
    shiftClick(laneAt(view, 0.5, 0)); // adds a: its track
    CHECK(view.selection() == Selection{.channel = t0, .clips = {a, b}});
    CHECK(told == 2);
    shiftClick(laneAt(view, 0.5, 0)); // removes a: the remaining clip's track
    CHECK(view.selection() == Selection{.channel = t2, .clips = {b}});
    CHECK(told == 3);

    // A box over a and b: the topmost clip's track, told once when it ends.
    told = 0;
    drag(view, laneAt(view, 1.5, 0), laneAt(view, 0.2, 2));
    CHECK(view.selection() == Selection{.channel = t0, .clips = {a, b}});
    CHECK(told == 1);

    // A clip dragged to another track takes the channel along.
    click(view, laneAt(view, 4.5, 2));
    told = 0;
    drag(view, laneAt(view, 4.5, 2), laneAt(view, 4.5, 1));
    CHECK(view.selection().channel == t1);
    CHECK(told == 1);
    f.session->updateSnapshotNow();
    view.setSnapshot(f.session->snapshot());
    CHECK(view.selection() == Selection{.channel = t1, .clips = {c}});
}

TEST_CASE("every action has a unique shortcut and the shell routes keys to it", "[integration]")
{
    ShellFixture f;
    const auto& actions = f.shell->actions();
    CHECK(actions.conflicts().empty());
    for (const auto& a : actions.all())
    {
        INFO(a.id);
        CHECK_FALSE(a.name.isEmpty());
        CHECK(a.perform != nullptr);
        CHECK_FALSE(a.keys.empty()); // ui-principles §1: one shortcut for every frequent action
    }

    // Undo is disabled with nothing to undo; adding a track enables it.
    CHECK_FALSE(actions.perform("edit.undo"));
    const auto tracks = f.session->snapshot()->tracks.size();
    CHECK(f.shell->keyPressed(juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0)));
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks.size() == tracks + 1);
    CHECK(actions.perform("edit.undo"));
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks.size() == tracks);

    // View toggles.
    CHECK(actions.isToggled("view.browser") == true);
    CHECK(actions.perform("view.browser"));
    CHECK_FALSE(actions.isToggled("view.browser"));
    CHECK(actions.tooltip("transport.playStop").contains("Play"));
}


TEST_CASE("dragging a clip moves it, snapped, onto another track in one undo step", "[integration]")
{
    ShellFixture f;
    juce::TemporaryFile wav(".wav");
    {
        juce::AudioBuffer<float> buffer(1, 48000);
        buffer.clear();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> out = wav.getFile().createOutputStream();
        auto writer = format.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
    const auto tracks = f.session->snapshot()->tracks;
    const auto clip = f.session->insertAudioFile(tracks[0].id, wav.getFile(), 0.0);
    f.session->updateSnapshotNow();

    fd::app::ArrangementView view(*f.session);
    view.setSize(1200, 600);
    view.setSnapshot(f.session->snapshot());
    const auto& tl = view.timeline();
    const auto laneX = static_cast<float>(fd::app::ArrangementView::headerWidth());
    const auto top = static_cast<float>(fd::app::ArrangementView::rulerHeight());

    // Grab the clip in the middle of its body (0.5 s), drag right ~1.3 s and down one track.
    const juce::Point<float> down(laneX + static_cast<float>(tl.xForTime(0.5)), top + 30.0f);
    const auto to = down + juce::Point<float>(static_cast<float>(1.3 * tl.pixelsPerSecond()), 64.0f);
    view.mouseDown(mouseEvent(view, down, down, false));
    CHECK(view.selectedClips() == std::set<std::uint64_t>{clip});
    view.mouseDrag(mouseEvent(view, (down + to) * 0.5f, down, true));
    view.mouseDrag(mouseEvent(view, to, down, true));
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // the drag preview, for review
    {
        juce::PNGImageFormat png;
        if (auto out = juce::File(dump).getChildFile("arrangement-drag.png").createOutputStream())
            png.writeImageToStream(view.createComponentSnapshot(view.getLocalBounds(), true, 2.0f), *out);
    }
    view.mouseUp(mouseEvent(view, to, down, true));
    f.session->updateSnapshotNow();

    const auto moved = fd::model::findClip(*f.session->snapshot(), clip);
    REQUIRE(moved);
    CHECK(moved->second == 1);
    const double snappedStart = tl.snap(1.3, f.session->snapshot()->tempo);
    CHECK(moved->first->start == Catch::Approx(snappedStart));
    f.session->undo();
    f.session->updateSnapshotNow();
    CHECK(fd::model::findClip(*f.session->snapshot(), clip)->second == 0);
}

TEST_CASE("the loop brace is drawn, toggled and resized with the mouse", "[integration]")
{
    ShellFixture f;
    fd::app::ArrangementView view(*f.session);
    view.setSize(1200, 600);
    const auto& tl = view.timeline();
    const auto tempo = f.session->snapshot()->tempo;
    const double bar = tempo.secondsForBeats(tempo.beatsPerBar());
    auto xAt = [&](double seconds) { return static_cast<float>(fd::app::ArrangementView::headerWidth()) + static_cast<float>(tl.xForTime(seconds)); };
    const float y = 6.0f; // inside the loop strip

    // Draw bars 2-4 (snapped).
    const juce::Point<float> a(xAt(bar * 1.05), y), b(xAt(bar * 2.97), y);
    view.mouseDown(mouseEvent(view, a, a, false));
    view.mouseDrag(mouseEvent(view, b, a, true));
    view.mouseUp(mouseEvent(view, b, a, true));
    CHECK(f.session->loopRange().first == Catch::Approx(bar));
    CHECK(f.session->loopRange().second == Catch::Approx(3.0 * bar));
    CHECK(f.session->isLooping());

    // A click on the brace toggles looping.
    const juce::Point<float> mid(xAt(bar * 2.0), y);
    view.frame(1.0 / 60.0, 0.0); // pick up the transport's loop range
    view.mouseDown(mouseEvent(view, mid, mid, false));
    view.mouseUp(mouseEvent(view, mid, mid, false));
    CHECK_FALSE(f.session->isLooping());

    // Drag the end edge to bar 5.
    const juce::Point<float> edge(xAt(3.0 * bar), y), to(xAt(4.0 * bar), y);
    view.mouseDown(mouseEvent(view, edge, edge, false));
    view.mouseDrag(mouseEvent(view, to, edge, true));
    view.mouseUp(mouseEvent(view, to, edge, true));
    CHECK(f.session->loopRange().first == Catch::Approx(bar));
    CHECK(f.session->loopRange().second == Catch::Approx(4.0 * bar));
}

TEST_CASE("the device chain shows panels and reorders by dragging a header", "[integration]")
{
    ShellFixture f;
    const auto track = f.session->snapshot()->tracks[0].id;
    const auto eq = f.session->addDevice(track, "fd:eq");
    const auto sat = f.session->addDevice(track, "fd:saturator");
    f.session->updateSnapshotNow();

    fd::app::ActionRegistry actions;
    actions.add({.id = "app.scanPlugins", .name = "Scan", .keys = {juce::KeyPress('p')}, .perform = [] {}});
    fd::app::DeviceChain chain(*f.session, actions);
    chain.setSize(1600, 200);
    chain.setTrack(f.session->snapshot()->tracks[0]);

    // Our devices appear as their panels.
    std::vector<fd::ui::DevicePanel*> panels;
    std::function<void(juce::Component&)> collect = [&](juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* p = dynamic_cast<fd::ui::DevicePanel*>(child))
                panels.push_back(p);
            collect(*child);
        }
    };
    collect(chain);
    REQUIRE(panels.size() == 2);
    CHECK(panels[0]->getX() < panels[1]->getX());

    // Drag the EQ's header past the saturator.
    auto* first = panels[0];
    const juce::Point<float> grab(60.0f, 10.0f);
    const auto drop = grab + juce::Point<float>(static_cast<float>(panels[1]->getRight() - first->getX()), 0.0f);
    chain.mouseDown(mouseEvent(*first, grab, grab, false));
    CHECK(chain.hasSelection());
    chain.mouseDrag(mouseEvent(*first, drop, grab, true));
    chain.mouseUp(mouseEvent(*first, drop, grab, true));
    f.session->updateSnapshotNow();
    const auto& devices = f.session->snapshot()->tracks[0].devices;
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].id == sat);
    CHECK(devices[1].id == eq);

    // The chain keeps the panels of devices that remain.
    chain.setTrack(f.session->snapshot()->tracks[0]);
    panels.clear();
    collect(chain);
    CHECK(std::find(panels.begin(), panels.end(), first) != panels.end());
}

TEST_CASE("the shell saves and restores its view with the project", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 900);
    fd::app::ArrangementView* view = nullptr;
    for (auto* child : f.shell->getChildren())
        if (auto* v = dynamic_cast<fd::app::ArrangementView*>(child))
            view = v;
    REQUIRE(view != nullptr);
    juce::String title;
    f.shell->onTitleChanged = [&](const juce::String& t) { title = t; };

    view->setView(5.0, 77.0);
    REQUIRE(f.shell->actions().perform("view.browser")); // close the browser
    juce::TemporaryFile project(fd::engine::Session::fileExtension);
    REQUIRE(f.session->saveAs(project.getFile()));

    f.session->newProject();
    view->setView(0.0, 20.0);
    REQUIRE(f.shell->actions().perform("view.browser")); // open again

    REQUIRE(f.session->open(project.getFile()));
    CHECK(view->timeline().start() == Catch::Approx(5.0));
    CHECK(view->timeline().pixelsPerSecond() == Catch::Approx(77.0));
    CHECK_FALSE(f.shell->actions().isToggled("view.browser"));
    CHECK(title.contains(project.getFile().getFileNameWithoutExtension()));
}

TEST_CASE("the command palette finds and runs any action", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 900);
    CHECK(f.shell->keyPressed(juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0)));
    auto& palette = f.shell->palette();
    REQUIRE(palette.isOpen());

    // Every action except the palette itself is listed.
    palette.setQuery("");
    CHECK(palette.shownIds().size() == std::min<size_t>(fd::app::CommandPalette::maxRows, f.shell->actions().all().size() - 1));

    palette.setQuery("to");
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review
    {
        juce::PNGImageFormat png;
        if (auto out = juce::File(dump).getChildFile("palette.png").createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot({500, 0, 800, 520}, true, 2.0f), *out);
    }
    palette.setQuery("brows");
    REQUIRE_FALSE(palette.shownIds().empty());
    CHECK(palette.shownIds().front() == "view.browser");
    const bool before = f.shell->actions().isToggled("view.browser");
    palette.performSelected();
    CHECK_FALSE(palette.isOpen());
    CHECK(f.shell->actions().isToggled("view.browser") != before);
    CHECK(palette.recent().front() == "view.browser");
}

TEST_CASE("piano roll: a double-click makes a clip; notes are drawn, moved, set and deleted", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    auto& roll = detail.pianoRoll();
    const auto track = f.session->snapshot()->tracks.front().id;
    REQUIRE(f.session->addDevice(track, "fd:subtractive-synth") != 0); // previews need something to play them
    auto clipInfo = [&]() -> std::optional<fd::model::ClipInfo> {
        f.session->updateSnapshotNow();
        const auto& clips = f.session->snapshot()->tracks.front().clips;
        return clips.empty() ? std::nullopt : std::optional(clips.front());
    };

    // Double-click an empty spot of the first track at 2.1 s: a one-bar MIDI clip from the grid
    // line before it, and the clip view showing it.
    const auto& tl = view.timeline();
    const juce::Point<float> spot(static_cast<float>(fd::app::ArrangementView::headerWidth()) + static_cast<float>(tl.xForTime(2.1)),
                                  static_cast<float>(fd::app::ArrangementView::rulerHeight()) + 20.0f);
    click(view, spot);
    click(view, spot, 2);
    auto clip = clipInfo();
    REQUIRE(clip);
    CHECK(clip->kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(clip->start <= 2.1);
    CHECK(clip->start == Catch::Approx(tl.snap(clip->start, f.session->snapshot()->tempo)));
    CHECK(clip->length == Catch::Approx(2.0)); // a bar at 120 BPM
    CHECK(f.session->snapshot()->tracks.front().kind == fd::model::TrackInfo::Kind::Instrument);
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Clip);
    REQUIRE(detail.showsPianoRoll());
    CHECK(f.session->midiInputTarget() == track); // previews reach the clip's instrument
    f.shell->finishAnimations();
    REQUIRE(roll.gridArea().getHeight() > 150);

    // Draw: a double-click in the grid makes a note one grid step long in the step under it.
    const double step = roll.timeline().gridStepBeats(f.session->snapshot()->tempo);
    click(roll, roll.pointFor(1.0 + step * 0.4, 64));
    click(roll, roll.pointFor(1.0 + step * 0.4, 64), 2);
    clip = clipInfo();
    REQUIRE(clip->notes.size() == 1);
    CHECK(clip->notes[0].pitch == 64);
    CHECK(clip->notes[0].start == Catch::Approx(1.0));
    CHECK(clip->notes[0].length == Catch::Approx(step));
    CHECK(roll.selection() == std::set<std::size_t>{0});

    // Move it a beat later and two semitones up (its body, dragged).
    const auto body = roll.pointFor(1.0 + step * 0.5, 64);
    const auto beatPixels = roll.pointFor(2.0, 64).x - roll.pointFor(1.0, 64).x;
    drag(roll, body, body + juce::Point<float>(beatPixels, roll.pointFor(1.0, 66).y - body.y));
    clip = clipInfo();
    REQUIRE(clip->notes.size() == 1);
    CHECK(clip->notes[0].pitch == 66);
    CHECK(clip->notes[0].start == Catch::Approx(2.0));

    // Longer, by its right edge (snapped to the grid).
    const auto edge = roll.pointFor(2.0 + step, 66) - juce::Point<float>(2.0f, 0.0f);
    drag(roll, edge, edge + juce::Point<float>(beatPixels, 0.0f));
    clip = clipInfo();
    CHECK(clip->notes[0].length == Catch::Approx(1.0 + step));

    // Velocity: the selected note's stem dragged down most of the lane.
    const auto lane = roll.velocityArea();
    const juce::Point<float> head(roll.pointFor(2.0, 66).x, static_cast<float>(lane.getY()) + 12.0f);
    drag(roll, head, head + juce::Point<float>(0.0f, static_cast<float>(lane.getHeight()) - 20.0f));
    clip = clipInfo();
    CHECK(clip->notes[0].velocity < 0.3f);

    // One undo step per gesture: velocity, length, move, draw.
    for (int i = 0; i < 4; ++i)
        f.session->undo();
    clip = clipInfo();
    REQUIRE(clip);
    CHECK(clip->notes.empty());
    for (int i = 0; i < 4; ++i)
        f.session->redo();
    clip = clipInfo();
    REQUIRE(clip->notes.size() == 1);
    CHECK(clip->notes[0].pitch == 66);
    detail.setClip(clip, f.session->snapshot()->tempo); // as the snapshot callback does

    // Keys act on the notes while the piano roll was used last: up a semitone, then delete.
    click(roll, roll.pointFor(2.0 + step * 0.5, 66));
    CHECK(roll.selection().size() == 1);
    CHECK(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    clip = clipInfo();
    CHECK(clip->notes[0].pitch == 67);
    detail.setClip(clip, f.session->snapshot()->tempo);
    CHECK(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    clip = clipInfo();
    CHECK(clip->notes.empty());
    CHECK(clipInfo()); // the clip itself stays

    if (const char* dump = std::getenv("FD_DUMP_DIR")) // a phrase in the clip view, for review
    {
        std::vector<fd::model::Note> phrase;
        const int pitches[] = {60, 64, 67, 72, 67, 64, 62, 65, 69, 74, 69, 65, 59, 62, 67, 71};
        for (int i = 0; i < 16; ++i)
            phrase.push_back({pitches[i], i * 0.25, i % 4 == 3 ? 0.5 : 0.25, static_cast<float>(60 + (i * 37) % 67) / 127.0f});
        f.session->setNotes(clip->id, phrase);
        clip = clipInfo();
        detail.setClip(clip, f.session->snapshot()->tempo);
        roll.selectAll();
        click(roll, roll.pointFor(3.0 + 0.1, 72), 1); // one selected
        juce::PNGImageFormat png;
        if (auto out = juce::File(dump).getChildFile("piano-roll.png").createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
        if (auto out = juce::File(dump).getChildFile("piano-roll@2x.png").createOutputStream())
            png.writeImageToStream(detail.createComponentSnapshot(detail.getLocalBounds().removeFromLeft(700), true, 2.0f), *out);
    }
}

TEST_CASE("modules collapse sections and fold in the chain; header controls never drag", "[integration]")
{
    ShellFixture f;
    const auto track = f.session->snapshot()->tracks[0].id;
    const auto sat = f.session->addDevice(track, "fd:saturator");
    f.session->addDevice(track, "fd:utility");
    f.session->updateSnapshotNow();

    fd::app::ActionRegistry actions;
    actions.add({.id = "app.scanPlugins", .name = "Scan", .keys = {juce::KeyPress('p')}, .perform = [] {}});
    fd::app::DeviceChain chain(*f.session, actions);
    chain.setSize(1600, 300);
    chain.setTrack(f.session->snapshot()->tracks[0]);
    std::vector<fd::ui::DevicePanel*> panels;
    std::function<void(juce::Component&)> collect = [&](juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* p = dynamic_cast<fd::ui::DevicePanel*>(child))
                panels.push_back(p);
            collect(*child);
        }
    };
    collect(chain);
    REQUIRE(panels.size() == 2);
    auto* saturator = panels[0];
    auto* utility = panels[1];
    CHECK(saturator->getHeight() == fd::ui::Theme::current().devicePanelHeight);

    // A click on a header setting (Quality) neither selects nor drags the module.
    REQUIRE_FALSE(saturator->headerControls().empty());
    auto* quality = saturator->headerControls().front();
    const juce::Point<float> inCombo(5.0f, 5.0f);
    chain.mouseDown(mouseEvent(*quality, inCombo, inCombo, false));
    CHECK_FALSE(chain.hasSelection());
    chain.mouseUp(mouseEvent(*quality, inCombo, inCombo, false));

    // Clicking a section's title collapses it; the next module moves left at once, and the
    // view is stored on the module.
    const int before = utility->getX();
    auto* shape = saturator->findSection("shape");
    REQUIRE(shape != nullptr);
    const auto& grid = fd::ui::Theme::current().deviceGrid;
    const juce::Point<float> title(static_cast<float>(grid.sectionPadding + 8), static_cast<float>(grid.bodyTop + 8));
    shape->mouseUp(mouseEvent(*shape, title, title, false));
    CHECK(shape->isCollapsed());
    CHECK(utility->getX() == before - (grid.sectionWidth(shape->columns()) - grid.collapsedWidth));
    CHECK(f.session->deviceViewState(sat).find("shape") != std::string::npos);

    // Select the module by its header, then fold it with the action's command.
    const juce::Point<float> header(80.0f, 10.0f);
    chain.mouseDown(mouseEvent(*saturator, header, header, false));
    chain.mouseUp(mouseEvent(*saturator, header, header, false));
    REQUIRE(chain.selectedPanel() == saturator);
    chain.foldSelected();
    CHECK(saturator->isFolded());
    CHECK(saturator->getWidth() == grid.foldedWidth);
    CHECK(utility->getX() == saturator->getRight() + fd::app::DeviceChain::gap);

    // Panels are rebuilt from the module's stored view (e.g. after switching tracks).
    chain.setTrack(std::nullopt);
    chain.setTrack(f.session->snapshot()->tracks[0]);
    panels.clear();
    collect(chain);
    REQUIRE(panels.size() == 2);
    CHECK(panels[0]->isFolded());
    CHECK(panels[0]->findSection("shape")->isCollapsed());

    // Expanding the selection brings everything back.
    chain.mouseDown(mouseEvent(*panels[0], {10.0f, 120.0f}, {10.0f, 120.0f}, false)); // the folded strip
    chain.mouseUp(mouseEvent(*panels[0], {10.0f, 120.0f}, {10.0f, 120.0f}, false));
    REQUIRE(chain.selectedPanel() == panels[0]);
    chain.expandSelected();
    CHECK_FALSE(panels[0]->isFolded());
    CHECK_FALSE(panels[0]->findSection("shape")->isCollapsed());
    CHECK(f.session->deviceViewState(sat).empty());
}

TEST_CASE("the Assistants popover turns the server on, shows setup lines and clients", "[integration][mcp]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
        fd::engine::registerDevices(engine);
        fd::engine::Session session(engine);
        juce::TemporaryFile folderName;
        const auto folder = folderName.getFile();
        REQUIRE(folder.createDirectory());
        {
            fd::mcp::McpServer server(session, folder, "test");
            fd::app::Shell shell(session, &server);
            shell.setBounds(0, 0, 1600, 900);
            shell.finishAnimations();
            REQUIRE(shell.actions().find("app.assistants") != nullptr);
            CHECK(shell.actions().conflicts().empty());

            shell.actions().perform("app.assistants");
            CHECK(shell.actions().isToggled("app.assistants"));
            auto* popover = dynamic_cast<fd::app::AssistantsPopover*>(shell.getChildComponent(shell.getNumChildComponents() - 1));
            REQUIRE(popover != nullptr);
            CHECK(popover->getRight() <= shell.getWidth());
            CHECK(popover->getY() >= fd::app::TransportBar::height);

            // On (the settings port may be taken on a developer machine: any free port will do).
            CHECK_FALSE(server.isRunning());
            server.setEnabled(true);
            if (!server.isRunning())
                REQUIRE(server.start(0));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            CHECK(Json::parse(folder.getChildFile("assistants.json").loadFileAsString().toStdString())["enabled"] == true);

            // A client calls: it's listed, and the transport's light is on.
            const auto out = server.dispatcher()->handle(
                {{"jsonrpc", "2.0"},
                 {"id", 1},
                 {"method", "tools/call"},
                 {"params",
                  {{"name", "get_project"},
                   {"arguments", Json::object()},
                   {"_meta",
                    {{"io.modelcontextprotocol/protocolVersion", fd::mcp::modernVersion},
                     {"io.modelcontextprotocol/clientInfo", {{"name", "Claude Code"}, {"version", "3.0"}}},
                     {"io.modelcontextprotocol/clientCapabilities", Json::object()}}}}}},
                {});
            REQUIRE(out.message);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            CHECK(server.clients().at(0).name == "Claude Code");
            CHECK(server.isActive());

            if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review
            {
                juce::PNGImageFormat png;
                const auto area = popover->getBounds().expanded(24).getUnion(juce::Rectangle<int>(shell.getWidth() - 640, 0, 640, 40));
                const auto file = juce::File(dump).getChildFile("assistants@2x.png");
                file.deleteFile(); // streams append
                if (auto outFile = file.createOutputStream())
                    png.writeImageToStream(shell.createComponentSnapshot(area.constrainedWithin(shell.getLocalBounds()), true, 2.0f), *outFile);
            }

            shell.actions().perform("app.assistants");
            CHECK_FALSE(shell.actions().isToggled("app.assistants"));
            server.setEnabled(false);
            CHECK_FALSE(server.isRunning());
        }
        folder.deleteRecursively();
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("automation lanes: rows among the tracks, added like tracks, edited and selected (C4d)", "[integration]")
{
    using S = fd::model::InspectorSection;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& view = f.shell->arrangement();
    auto& actions = f.shell->actions();
    auto& s = *f.session;
    const auto snap = s.snapshot();
    const auto t0 = snap->tracks[0].id, t1 = snap->tracks[1].id;
    const auto eq = s.addDevice(t0, "fd:eq");
    REQUIRE(eq != 0);
    s.updateSnapshotNow();

    // Ctrl+Shift+L adds a lane like a track: empty, at the end of the tracks, selected. The
    // picker then assigns the selected track's volume to it.
    view.selectTrack(t0);
    REQUIRE(actions.perform("automation.addLane"));
    CHECK(s.undoDescription() == "Add automation lane");
    s.updateSnapshotNow();
    REQUIRE(view.selection().lane);
    const auto volumeLane = view.selection().lane;
    const auto tracks = s.snapshot()->tracks.size();
    REQUIRE(view.rows().size() > tracks);
    CHECK(view.rows()[tracks].lane == *volumeLane);
    REQUIRE(actions.perform("automation.assign"));
    auto& picker = f.shell->lanePicker();
    REQUIRE(picker.isOpen());
    CHECK(picker.channel() == t0); // the track selected before
    picker.setQuery("volume");
    REQUIRE_FALSE(picker.shownLanes().empty());
    CHECK(picker.shownLanes().front() == "volume");
    picker.chooseSelected();
    s.updateSnapshotNow();
    REQUIRE(s.laneOf({t0, "volume"}) == volumeLane);
    CHECK(view.selection().lane == volumeLane);
    CHECK(view.selection().channel == t0); // its first parameter's channel

    // A module's parameter and another track's pan, each in a new lane at the end (an assistant
    // or a recording would do the same).
    const std::string freq = "m" + std::to_string(eq) + "/band1.freq";
    const auto freqLane = s.automateInNewLane({t0, freq});
    REQUIRE(s.setLanePoints(freqLane, {{0.0, 0.2f, 0.0f}, {2.0, 0.8f, fd::model::curveForBend(0.5f)}, {4.0, 0.4f, 0.0f}}));
    const auto panLane = s.automateInNewLane({t1, "pan"});
    REQUIRE(s.setLanePoints(panLane, {{1.0, 0.0f, 0.0f}, {3.0, 1.0f, 0.0f}}));
    s.updateSnapshotNow();
    CHECK(view.rows().size() == tracks + 3 + 1 + s.snapshot()->returns.size()); // 3 lanes, the master
    CHECK(view.rows()[tracks + 1].lane == freqLane);
    CHECK(view.rows()[tracks + 2].lane == panLane);
    view.setAllTrackHeights(64.0);

    // Double-click in the volume lane adds a point (one undo step).
    auto rowTop = [&](size_t row) {
        float y = static_cast<float>(fd::app::ArrangementView::rulerHeight());
        for (size_t i = 0; i < row; ++i)
            y += 64.0f;
        return y;
    };
    const auto laneX = [&](double seconds) {
        return static_cast<float>(fd::app::ArrangementView::headerWidth()) + static_cast<float>(view.timeline().xForTime(seconds));
    };
    auto volumePoints = [&] { return s.snapshot()->automationLane(*volumeLane)->points; };
    const auto volumeRow = tracks, freqRow = tracks + 1;
    const juce::Point<float> spot(laneX(1.0), rowTop(volumeRow) + 32.0f);
    click(view, spot);
    click(view, spot, 2);
    s.updateSnapshotNow();
    REQUIRE(volumePoints().size() == 1);
    CHECK(s.undoDescription() == "Add point");
    CHECK(view.selection().points == std::set<size_t>{0});
    auto& inspector = f.shell->inspector();
    CHECK(inspector.sections() == std::vector{S::Point, S::Lane, S::Assignments}); // a lane belongs to no track
    // The inspector's value field types a point's value (the others are the assignments' ranges).
    auto fields = findAll<fd::ui::NumberField>(inspector);
    fields.erase(std::remove_if(fields.begin(), fields.end(), [&](auto* field) { return field->getParentComponent() != &inspector; }),
                 fields.end());
    REQUIRE(fields.size() == 1);
    fields[0]->setValue(0.25, juce::sendNotification);
    s.updateSnapshotNow();
    CHECK(volumePoints()[0].value == Catch::Approx(0.25f));
    CHECK(s.undoDescription() == "Point value");

    // Dragging the point moves it (one step); Delete removes it.
    const auto pointAt = volumePoints()[0];
    const juce::Point<float> grab(laneX(pointAt.time),
                                  rowTop(volumeRow) + 64.0f - 1.0f - 5.0f - pointAt.value * (64.0f - 1.0f - 10.0f));
    drag(view, grab, grab + juce::Point<float>(0.0f, -10.0f));
    s.updateSnapshotNow();
    CHECK(volumePoints()[0].value > pointAt.value);
    CHECK(s.undoDescription() == "Move points");
    REQUIRE(actions.perform("edit.delete"));
    s.updateSnapshotNow();
    CHECK(volumePoints().empty());

    // The module lane: its header selects it, and its module shows below.
    click(view, {40.0f, rowTop(freqRow) + 20.0f});
    CHECK(view.selection().lane == freqLane);
    CHECK(f.shell->detail().chain().selectedDevice() == eq);
    CHECK(inspector.sections() == std::vector{S::Lane, S::Assignments});

    // A lane driving two parameters, one inverted over part of its range.
    REQUIRE(s.assign(freqLane, {t1, "volume"}, {0.8f, 0.2f}));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->automationLane(freqLane)->displayName().ends_with(" + 1"));

    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review
    {
        juce::PNGImageFormat png;
        auto file = juce::File(dump).getChildFile("automation-lanes.png");
        file.deleteFile();
        if (auto out = file.createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
    }

    // Ctrl+Alt+L hides every lane, and shows them again.
    const auto before = view.rows().size();
    CHECK(actions.isToggled("view.lanes"));
    REQUIRE(actions.perform("view.lanes"));
    CHECK(view.rows().size() == before - 3);
    CHECK_FALSE(view.selection().lane); // a hidden lane isn't selected
    CHECK_FALSE(actions.isToggled("view.lanes"));
    CHECK(nlohmann::json::parse(s.viewStateProvider())["lanes"] == false);
    REQUIRE(actions.perform("view.lanes"));
    CHECK(view.rows().size() == before);

    // Delete with a lane's header used last removes the lane, in one step.
    click(view, {40.0f, rowTop(volumeRow) + 20.0f});
    REQUIRE(view.selection().lane == volumeLane);
    REQUIRE(actions.perform("edit.delete"));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->automationLane(*volumeLane) == nullptr);
    CHECK(s.undoDescription() == "Remove automation lane");

    // Row heights are view state, saved with the project.
    view.setAllTrackHeights(80.0);
    const auto state = nlohmann::json::parse(s.viewStateProvider());
    CHECK(state["rowHeights"]["a" + std::to_string(freqLane)] == Catch::Approx(80.0));
}

TEST_CASE("automation lanes: the picker's channel row and assign mode, the inspector's assignments, menus (C4d)",
          "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& view = f.shell->arrangement();
    auto& actions = f.shell->actions();
    auto& s = *f.session;
    const auto snap = s.snapshot();
    const auto t0 = snap->tracks[0].id, t1 = snap->tracks[1].id;
    auto& picker = f.shell->lanePicker();

    // Right-click menus make rows: every row header's and the empty space's end with Add track,
    // Add automation lane and Add return track.
    REQUIRE(view.onAddMenu);
    juce::PopupMenu add;
    view.onAddMenu(add);
    REQUIRE(invoke(add, "Add track"));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks.size() == snap->tracks.size() + 1);
    s.undo();
    s.updateSnapshotNow();
    // The mixer view's empty space: tracks and return tracks (lanes live in the arrangement).
    REQUIRE(f.shell->mixer().onAddMenu);
    juce::PopupMenu mix;
    f.shell->mixer().onAddMenu(mix);
    CHECK(mix.getNumItems() == 2);
    REQUIRE(invoke(mix, "Add return track"));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->returns.size() == snap->returns.size() + 1);
    s.undo();
    s.updateSnapshotNow();

    // An empty lane, like a new track: at the end of the tracks, its own colour, selected.
    view.selectTrack(t0);
    REQUIRE(invoke(add, "Add automation lane"));
    s.updateSnapshotNow();
    REQUIRE(view.selection().lane);
    const auto lane = *view.selection().lane;
    CHECK(s.snapshot()->automationLane(lane)->assignments.empty());
    CHECK(s.snapshot()->automationLane(lane)->after == s.snapshot()->tracks.back().id);
    CHECK_FALSE(view.selection().channel); // nothing assigned: no channel
    CHECK_FALSE(picker.isOpen());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: a new lane, driving nothing yet
    {
        f.shell->finishAnimations();
        juce::PNGImageFormat png;
        auto file = juce::File(dump).getChildFile("automation-new-lane.png");
        file.deleteFile();
        if (auto out = file.createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
    }

    // Ctrl+Alt+Shift+L assigns to the selected lane; the channel row switches to another track.
    REQUIRE(actions.isEnabled("automation.assign"));
    REQUIRE(actions.perform("automation.assign"));
    REQUIRE(picker.isOpen());
    CHECK(picker.channel() == t0); // the track selected last
    picker.setChannel(t1);
    picker.setQuery("pan");
    REQUIRE(picker.shownLanes().front() == "pan");
    picker.chooseSelected();
    CHECK(s.undoDescription() == "Assign parameter");
    s.updateSnapshotNow();
    REQUIRE(s.laneOf({t1, "pan"}) == lane);
    CHECK(view.selection().channel == t1); // now its first assignment's channel

    // The inspector edits what it drives: a second parameter, its range (typed, in its units)
    // and on switch, and taking it off.
    REQUIRE(s.assign(lane, {t0, "volume"}));
    s.updateSnapshotNow();
    auto& assignments = f.shell->inspector().assignments();
    REQUIRE(assignments.isVisible());
    auto fields = findAll<fd::ui::NumberField>(assignments);
    REQUIRE(fields.size() == 4); // from and to, for each
    fields[3]->setValue(*s.paramFromText({t0, "volume"}, "-6 dB"), juce::sendNotification);
    s.updateSnapshotNow();
    const auto* volume = s.snapshot()->automationLane(lane)->assignment(t0, "volume");
    REQUIRE(volume != nullptr);
    CHECK(*s.paramPlain({t0, "volume"}, volume->range.to) == Catch::Approx(-6.0).margin(0.05));
    CHECK(s.undoDescription() == "Assignment range");
    auto toggles = findAll<fd::ui::Toggle>(assignments); // the on switches, and Assign parameter...
    toggles.erase(std::remove_if(toggles.begin(), toggles.end(), [](auto* t) { return t->getButtonText().isNotEmpty(); }),
                  toggles.end());
    REQUIRE(toggles.size() == 2);
    toggles[1]->onClick(); // the volume's on switch
    s.updateSnapshotNow();
    CHECK_FALSE(s.snapshot()->automationLane(lane)->assignment(t0, "volume")->on);
    auto removes = findAll<fd::ui::IconButton>(assignments);
    REQUIRE(removes.size() == 2);
    removes[1]->onClick();
    s.updateSnapshotNow();
    CHECK(s.snapshot()->automationLane(lane)->assignments.size() == 1);

    // The lane's name, typed in the inspector.
    auto editors = findAll<juce::TextEditor>(f.shell->inspector());
    REQUIRE_FALSE(editors.empty());
    editors[0]->setText("Wobble", false);
    editors[0]->onReturnKey();
    s.updateSnapshotNow();
    CHECK(s.snapshot()->automationLane(lane)->displayName() == "Wobble");
    CHECK(s.undoDescription() == "Rename automation lane");

    // A mixer control's menu: assigning it to a lane moves it there, with that lane ticked.
    fd::app::VolumeBar bar(s, t1);
    juce::PopupMenu menu;
    fd::app::addAutomationItems(s, {t1, "volume"}, menu);
    REQUIRE(invoke(menu, "Wobble", "Assign to automation lane"));
    CHECK(s.laneOf({t1, "volume"}) == lane);
    s.updateSnapshotNow();
    juce::PopupMenu again;
    fd::app::addAutomationItems(s, {t1, "volume"}, again);
    bool ticked = false;
    for (juce::PopupMenu::MenuItemIterator it(again, true); it.next();)
        ticked = ticked || (it.getItem().text == "Wobble" && it.getItem().isTicked);
    CHECK(ticked);
    REQUIRE(invoke(again, "Automate in a new lane"));
    CHECK(s.laneOf({t1, "volume"}) != lane);
}

TEST_CASE("automated controls: the indicator, their menu, following the lane, the transport (C4c)", "[integration]")
{
    using fd::ui::AutomationState;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto eq = s.addDevice(t0, "fd:eq");
    REQUIRE(eq != 0);
    s.updateSnapshotNow();
    auto handle = s.openDevice(eq);
    REQUIRE(handle != nullptr);
    auto& ctx = handle->context();
    auto indexOf = [&](std::string_view id) {
        for (size_t i = 0; i < ctx.info.params.size(); ++i)
            if (ctx.info.params[i].id == id)
                return i;
        FAIL("no parameter " << id);
        return size_t{0};
    };
    const auto freq = indexOf("band1.freq"), analyzer = indexOf("analyzer");
    CHECK(ctx.params.canAutomate(freq));
    CHECK_FALSE(ctx.params.canAutomate(analyzer)); // a setting, not automatable
    CHECK(ctx.params.automationState(freq) == AutomationState::None);

    // "Show automation lane" from a control: its lane, selected, in the arrangement (even from
    // the mixer view).
    juce::PopupMenu fresh;
    ctx.params.addAutomationItems(freq, fresh);
    REQUIRE(invoke(fresh, "Automate in a new lane"));
    s.updateSnapshotNow();
    const auto lane = s.laneOf({t0, "m" + std::to_string(eq) + "/band1.freq"}).value_or(0);
    REQUIRE(lane != 0);
    REQUIRE(actions.perform("view.mixer"));
    juce::PopupMenu menu;
    ctx.params.addAutomationItems(freq, menu);
    REQUIRE(invoke(menu, "Show automation lane"));
    s.updateSnapshotNow();
    CHECK_FALSE(actions.isToggled("view.mixer"));
    CHECK(f.shell->arrangement().selection().lane == lane);

    // Points: the indicator plays; moving the knob turns its assignment off; the transport's
    // button turns every one back on.
    REQUIRE(s.setLanePoints(lane, {{0.0, 0.2f, 0.0f}, {2.0, 0.8f, 0.0f}}));
    fd::ui::ParamKnob knob(ctx, freq);
    knob.refresh();
    CHECK(knob.automationState() == AutomationState::On);
    CHECK_FALSE(actions.isEnabled("automation.reEnable"));
    ctx.params.beginGesture(freq);
    ctx.params.setPlain(freq, 1000.0f);
    ctx.params.endGesture(freq);
    s.updateSnapshotNow();
    knob.refresh();
    CHECK(knob.automationState() == AutomationState::Off);
    CHECK(actions.isEnabled("automation.reEnable"));
    CHECK(actions.isToggled("automation.reEnable")); // the button lights
    REQUIRE(actions.perform("automation.reEnable"));
    s.updateSnapshotNow();
    CHECK(ctx.params.automationState(freq) == AutomationState::On);
    CHECK_FALSE(actions.isEnabled("automation.reEnable"));
    juce::PopupMenu assigned;
    ctx.params.addAutomationItems(freq, assigned);
    REQUIRE(invoke(assigned, "Remove from automation lane"));
    s.updateSnapshotNow();
    CHECK(ctx.params.automationState(freq) == AutomationState::None);

    // A mix control: the indicator, and it follows the lane while it plays.
    REQUIRE(s.setLanePoints(s.automateInNewLane({t0, "volume"}), {{0.0, 0.8f, 0.0f}, {4.0, 0.2f, 0.0f}}));
    s.updateSnapshotNow();
    fd::app::VolumeBar bar(s, t0);
    bar.update(*s.snapshot()->channel(t0));
    CHECK(bar.automationState() == AutomationState::On);
    auto volume = s.impl().param({t0, "volume"});
    REQUIRE(volume);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // tracktion readies the curve's playback
    volume->param->updateFromAutomationSources(tracktion::TimePosition::fromSeconds(2.0)); // as playback does
    bar.follow();
    CHECK(bar.getValue() == Catch::Approx(0.5f).margin(0.01f));

    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: indicators on, off, and the transport
    {
        REQUIRE(s.setLanePoints(s.automateInNewLane({t0, "m" + std::to_string(eq) + "/band2.freq"}),
                                {{0.0, 0.2f, 0.0f}, {2.0, 0.8f, 0.0f}}));
        const fd::engine::Session::ParamTarget gain{t0, "m" + std::to_string(eq) + "/output"};
        REQUIRE(s.setLanePoints(s.automateInNewLane(gain), {{0.0, 0.2f, 0.0f}, {2.0, 0.8f, 0.0f}}));
        s.setAssignmentOn(gain, false);
        s.setAutomationArmed(true);
        f.shell->arrangement().selectTrack(t0);
        s.updateSnapshotNow();
        f.shell->finishAnimations();
        for (auto* k : findAll<fd::ui::ParamKnob>(*f.shell))
            k->refresh();
        for (auto* b : f.shell->getChildren())
            if (auto* transport = dynamic_cast<fd::app::TransportBar*>(b))
                transport->frame(0.0, s.snapshot()->tempo);
        juce::PNGImageFormat png;
        auto file = juce::File(dump).getChildFile("automation-controls.png");
        file.deleteFile();
        if (auto out = file.createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds(), true, 2.0f), *out);
        s.setAutomationArmed(false);
    }

    // Arm: touch recording while playing.
    REQUIRE(actions.perform("automation.arm"));
    CHECK(s.isAutomationArmed());
    CHECK(actions.isToggled("automation.arm"));
    REQUIRE(actions.perform("automation.arm"));
    CHECK_FALSE(s.isAutomationArmed());
}

TEST_CASE("controls: a right-click opens their menu; a double-click never nests gestures", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    fd::ui::Knob knob;
    knob.setSize(48, 76);
    int depth = 0, deepest = 0, menus = 0;
    knob.onGestureStart = [&] { deepest = std::max(deepest, ++depth); };
    knob.onGestureEnd = [&] { --depth; };
    knob.onPopupMenu = [&] { ++menus; };
    const juce::Point<float> p(24.0f, 38.0f);
    const auto left = juce::ModifierKeys::leftButtonModifier;
    knob.mouseDown(mouseEvent(knob, p, p, false, left, 1));
    knob.mouseUp(mouseEvent(knob, p, p, false, left, 1));
    knob.mouseDown(mouseEvent(knob, p, p, false, left, 2));
    knob.mouseDoubleClick(mouseEvent(knob, p, p, false, left, 2));
    knob.mouseUp(mouseEvent(knob, p, p, false, left, 2));
    CHECK(deepest == 1);
    CHECK(depth == 0);

    const auto right = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    knob.mouseDown(mouseEvent(knob, p, p, false, right, 1));
    CHECK(menus == 1);
    CHECK(depth == 0); // no gesture

    fd::ui::Toggle toggle("On");
    toggle.setSize(40, 20);
    toggle.onPopupMenu = [&] { ++menus; };
    const juce::Point<float> q(10.0f, 10.0f);
    toggle.mouseDown(mouseEvent(toggle, q, q, false, right, 1));
    toggle.mouseUp(mouseEvent(toggle, q, q, false, right, 1));
    CHECK(menus == 2);
    CHECK_FALSE(toggle.getToggleState()); // a right-click doesn't toggle
}

TEST_CASE("automation lanes: a step lane says why it doesn't bend (C4b)", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto comp = s.addDevice(t0, "fd:compressor");
    REQUIRE(comp != 0);
    const std::string character = "m" + std::to_string(comp) + "/character"; // a choice: two characters
    const auto lane = s.automateInNewLane({t0, character});
    REQUIRE(s.setLanePoints(lane, {{0.0, 0.0f, 0.0f}, {8.0, 1.0f, 0.0f}}));
    s.updateSnapshotNow();
    const auto before = s.snapshot()->automationLane(lane)->points;
    REQUIRE(s.snapshot()->automationLane(lane)->rules().steps == 2);

    fd::app::ArrangementView view(s);
    view.setSize(1200, 600);
    view.setSnapshot(s.snapshot());
    view.setAllTrackHeights(64.0f);
    juce::String message;
    view.onMessage = [&](const juce::String& m) { message = m; };
    // On the hold between the points (the lower value, until the step at 8 s).
    const juce::Point<float> on(static_cast<float>(fd::app::ArrangementView::headerWidth()) +
                                    static_cast<float>(view.timeline().xForTime(4.0)),
                                static_cast<float>(fd::app::ArrangementView::rulerHeight()) +
                                    static_cast<float>(fd::model::rowOfLane(view.rows(), lane)) * 64.0f + 64.0f - 1.0f - 5.0f);
    const juce::ModifierKeys alt(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    view.mouseDown(mouseEvent(view, on, on, false, alt));
    view.mouseDrag(mouseEvent(view, on + juce::Point<float>(0.0f, -30.0f), on, true, alt));
    view.mouseUp(mouseEvent(view, on + juce::Point<float>(0.0f, -30.0f), on, true, alt));
    s.updateSnapshotNow();
    CHECK(message.contains("Character has steps"));
    CHECK(s.snapshot()->automationLane(lane)->points == before); // nothing bent or moved
}

TEST_CASE("popovers close on a click anywhere outside them, not inside or in their menus", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::Component window, popover, inside, elsewhere, menu;
    window.setSize(400, 300);
    window.addAndMakeVisible(popover);
    window.addAndMakeVisible(elsewhere);
    popover.addAndMakeVisible(inside);
    menu.setSize(100, 100); // a window of its own, like a popup menu
    int closed = 0;
    fd::ui::OutsideClick outside(popover, [&] { ++closed; });
    outside.attach();
    outside.clickedOn(inside);
    outside.clickedOn(popover);
    outside.clickedOn(menu);
    CHECK(closed == 0);
    outside.ignore = [&](const juce::Component& c) { return &c == &elsewhere; };
    outside.clickedOn(elsewhere);
    CHECK(closed == 0);
    outside.ignore = nullptr;
    outside.clickedOn(elsewhere);
    CHECK(closed == 1);
    outside.clickedOn(window);
    CHECK(closed == 2);
    popover.setVisible(false); // closed already: nothing more to do
    outside.clickedOn(window);
    CHECK(closed == 2);
    outside.detach();
}

TEST_CASE("recording: arm buttons, the INPUT section, Record and its menu, the recorded clip selected (C5a)",
          "[integration]")
{
    using S = fd::model::InspectorSection;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    s.setUnsavedRoot(root);
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(f.engine, io);
    juce::AudioBuffer<float> in(2, 480);
    std::int64_t clock = 0;
    auto pump = [&](double seconds) {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 480; ++i)
                    in.setSample(c, i, 0.4f * std::sin(static_cast<float>(clock + i) * 0.03f));
            clock += 480;
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    pump(0.1);
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;

    // Track headers have an arm button; a click arms that track alone.
    auto& view = f.shell->arrangement();
    view.setAllTrackHeights(64.0);
    std::vector<fd::app::ChannelToggle*> arms;
    for (auto* toggle : findAll<fd::app::ChannelToggle>(view))
        if (toggle->getButtonText().isEmpty())
            arms.push_back(toggle);
    REQUIRE(arms.size() >= 4);
    arms[0]->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks[0].input.armed);

    // A selected track's inspector has INPUT: choosing the audio input is one undo step.
    view.selectTrack(t0);
    auto& inspector = f.shell->inspector();
    REQUIRE(std::find(inspector.sections().begin(), inspector.sections().end(), S::Input) != inspector.sections().end());
    auto boxes = findAll<juce::ComboBox>(inspector);
    REQUIRE(boxes.size() >= 4);
    auto* audio = boxes[0]; // Audio, first
    REQUIRE(audio->getNumItems() >= 2); // None, then the inputs
    audio->setSelectedItemIndex(1, juce::sendNotificationSync);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks[0].input.audio == s.audioInputs().front().id);
    CHECK(s.undoDescription() == "Track input");

    // The Record button's menu, and the count-in and punch toggles.
    REQUIRE(actions.perform("transport.countIn"));
    CHECK(s.countIn() == fd::model::CountIn::OneBar);
    REQUIRE(actions.perform("transport.countIn"));
    CHECK(s.countIn() == fd::model::CountIn::Off);
    REQUIRE(actions.perform("transport.punch"));
    CHECK(s.punch());
    REQUIRE(actions.perform("transport.punch"));

    // Record: a pass, drawn while it grows, then its clip is selected.
    REQUIRE(actions.perform("transport.record"));
    CHECK(actions.isToggled("transport.record"));
    pump(0.6);
    CHECK_FALSE(s.liveRecordings().empty());
    view.frame(0.0, s.position());
    auto image = f.shell->createComponentSnapshot(f.shell->getLocalBounds());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: a recording in progress
    {
        juce::PNGImageFormat png;
        auto file = juce::File(dump).getChildFile("recording.png");
        file.deleteFile();
        if (auto out = file.createOutputStream())
            png.writeImageToStream(image, *out);
    }
    REQUIRE(actions.perform("transport.playStop"));
    pump(0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->tracks[0].clips.size() == 1);
    CHECK(view.selection().clips == std::set{s.snapshot()->tracks[0].clips[0].id});

    // The mixer view's strips: an arm button and the input row for tracks.
    REQUIRE(actions.perform("view.mixer"));
    CHECK_FALSE(findAll<fd::app::InputButton>(f.shell->mixer()).empty());
    root.deleteRecursively();
}

TEST_CASE("takes: take lanes open after a loop pass; clicks, stars, the TAKE section, Alt+arrows, Delete, spread; "
          "Capture MIDI (C5b)",
          "[integration]")
{
    using S = fd::model::InspectorSection;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    s.setUnsavedRoot(root);
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(f.engine, io);
    juce::AudioBuffer<float> in(2, 480);
    std::int64_t clock = 0;
    auto pump = [&](double seconds) {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 480; ++i)
                    in.setSample(c, i, 0.4f * std::sin(static_cast<float>(clock + i) * 0.03f));
            clock += 480;
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    pump(0.1);
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    auto track = [&] { return *s.snapshot()->channel(t0); };

    // A loop pass of two laps and a half: three takes, their lanes open under the track.
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setArmed(t0, true, true);
    s.setLoopRange(4.0, 6.0);
    s.setLooping(true);
    s.setPosition(4.0);
    REQUIRE(actions.perform("transport.record"));
    pump(5.0);
    REQUIRE(actions.perform("transport.playStop"));
    s.setLooping(false);
    s.setArmed(t0, false, false);
    pump(0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    s.updateSnapshotNow();
    REQUIRE(track().takes.size() == 3);
    CHECK(track().showTakes);
    auto& view = f.shell->arrangement();
    REQUIRE(view.rows().size() > 4);
    CHECK(view.rows()[1].isTake());
    CHECK(view.rows()[3].take == track().takes[2].id);
    REQUIRE(track().clips.size() == 1);
    CHECK(track().clips[0].fromTake == track().takes[1].clips.at(0).id); // the last complete lap
    view.frame(0.0, s.position());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: take lanes
    {
        juce::PNGImageFormat png;
        auto file = juce::File(dump).getChildFile("takes.png");
        file.deleteFile();
        if (auto out = file.createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
    }

    // Each take lane's header has a star.
    std::vector<fd::ui::IconButton*> stars;
    for (auto* b : findAll<fd::ui::IconButton>(view))
        if (b->getName() == "Favourite" && b->isVisible())
            stars.push_back(b);
    REQUIRE(stars.size() == 3);
    stars[0]->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    s.updateSnapshotNow();
    CHECK(track().takes[0].star);

    // A click on a take uses it (one undo step) and selects it; rows: the track (64), takes (40).
    auto rowTop = [&](int row) {
        return static_cast<float>(fd::app::ArrangementView::rulerHeight()) + (row == 0 ? 0.0f : 64.0f + 40.0f * static_cast<float>(row - 1));
    };
    const auto laneX = [&](double seconds) {
        return static_cast<float>(fd::app::ArrangementView::headerWidth()) + static_cast<float>(view.timeline().xForTime(seconds));
    };
    click(view, {laneX(5.0), rowTop(1) + 20.0f});
    s.updateSnapshotNow();
    CHECK(view.selection().take == track().takes[0].id);
    CHECK(s.undoDescription() == "Use take");
    REQUIRE(track().clips.size() == 1);
    CHECK(track().clips[0].fromTake == track().takes[0].clips.at(0).id);

    // The inspector shows TAKE first; its "Use this take" uses the selected take.
    auto& inspector = f.shell->inspector();
    REQUIRE_FALSE(inspector.sections().empty());
    CHECK(inspector.sections().front() == S::Take);
    view.selectTake(t0, track().takes[1].id);
    fd::ui::Toggle* use = nullptr;
    for (auto* t : findAll<fd::ui::Toggle>(inspector))
        if (t->getButtonText() == "Use this take" && t->isVisible())
            use = t;
    REQUIRE(use != nullptr);
    use->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    s.updateSnapshotNow();
    CHECK(track().clips.at(0).fromTake == track().takes[1].clips.at(0).id);

    // Alt+Down / Alt+Up: the next or previous take covering the selected clip (the third lap is
    // short, so it's skipped); the clip now playing stays selected.
    view.setSelection(fd::model::withClips(*s.snapshot(), {track().clips.at(0).id}, t0, t0));
    REQUIRE(actions.perform("take.next"));
    s.updateSnapshotNow();
    CHECK(track().clips.at(0).fromTake == track().takes[0].clips.at(0).id);
    CHECK(view.selection().clips == std::set{track().clips.at(0).id});
    REQUIRE(actions.perform("take.previous"));
    s.updateSnapshotNow();
    CHECK(track().clips.at(0).fromTake == track().takes[1].clips.at(0).id);

    // Delete with a take selected deletes it (one undo step).
    view.selectTake(t0, track().takes[2].id);
    REQUIRE(actions.perform("edit.delete"));
    s.updateSnapshotNow();
    CHECK(track().takes.size() == 2);
    CHECK(s.undoDescription() == "Delete take");

    // The track's menu: hide its takes; spread them to new tracks.
    CHECK(view.selection().channel == t0);
    REQUIRE(actions.perform("view.takes"));
    s.updateSnapshotNow();
    CHECK_FALSE(track().showTakes);
    CHECK_FALSE(view.rows()[1].isTake());
    const auto before = s.snapshot()->tracks.size();
    REQUIRE(actions.perform("take.spread"));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks.size() == before + 2);

    // Capture MIDI: dim until something is played, then the played notes become a selected clip.
    const auto keys = s.snapshot()->tracks.back().id;
    REQUIRE(s.addDevice(keys, "fd:subtractive-synth") != 0);
    view.selectTrack(keys);
    s.setMidiInputTarget(keys);
    pump(0.2);
    CHECK_FALSE(actions.isEnabled("transport.capture"));
    for (int note : {60, 64})
    {
        s.computerKeyboardNote(note, 0.8f);
        pump(0.15);
        s.computerKeyboardNote(note, 0.0f);
        pump(0.1);
    }
    CHECK(actions.isEnabled("transport.capture"));
    s.setPosition(8.0);
    REQUIRE(actions.perform("transport.capture"));
    s.updateSnapshotNow();
    CHECK(s.undoDescription() == "Capture MIDI");
    REQUIRE(view.selection().clips.size() == 1);
    CHECK(s.snapshot()->channel(keys)->clips.size() == 1);
    root.deleteRecursively();
}
