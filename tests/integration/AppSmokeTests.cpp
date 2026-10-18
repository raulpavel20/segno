// Smoke tests of the DAW shell (docs/design/daw-shell.md, C1): it builds offscreen around a
// Session, every action has a unique id and shortcut, and keys reach the registry.

#include "TableEditor.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/WavIO.h"
#include "mcp/McpServer.h"
#include "model/Chance.h"
#include "model/ChannelStripLayout.h"
#include "model/Editing.h"
#include "model/MenuBar.h"
#include "model/Modulators.h"
#include "model/Selection.h"
#include "ui/AutomationMenu.h"
#include "ui/ChannelControls.h"
#include "ui/ChannelInspector.h"
#include "ui/DeviceChain.h"
#include "ui/MixerView.h"
#include "ui/Shell.h"
#include "ui/TrackInputs.h"
#include "ui/WindowContent.h"

#include <juce_opengl/juce_opengl.h>
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

/// Writes a screenshot for review into `dir`, replacing any earlier one (a file's output stream
/// appends, which would leave the old picture in front).
void dumpPng(const char* dir, const juce::String& name, const juce::Image& image)
{
    const auto file = juce::File(dir).getChildFile(name);
    file.deleteFile();
    juce::PNGImageFormat png;
    if (auto out = file.createOutputStream())
        png.writeImageToStream(image, *out);
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

    for (int width : {1800, 1200, 1024}) // 1024: the window's minimum
    {
        f.shell->setSize(width, 900);
        // Select the first track (its chain shows in the detail panel).
        for (auto* child : f.shell->getChildren())
            if (auto* view = dynamic_cast<fd::app::ArrangementView*>(child))
            {
                const juce::Point<float> header(
                    40.0f, static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 20.0f);
                view->mouseDown(mouseEvent(*view, header, header, false));
                view->mouseUp(mouseEvent(*view, header, header, false));
            }
        const auto image = f.shell->createComponentSnapshot(f.shell->getLocalBounds());
        CHECK(image.isValid());
        // The top bar's controls never overlap, at either width (model::layoutTopBar).
        for (auto* child : f.shell->getChildren())
            if (auto* bar = dynamic_cast<fd::app::TransportBar*>(child))
            {
                std::vector<juce::Rectangle<int>> shown;
                for (auto* c : bar->getChildren())
                    if (c->isVisible())
                        shown.push_back(c->getBounds());
                CHECK(shown.size() >= 19); // every control (no assistants button without MCP)
                for (size_t a = 0; a < shown.size(); ++a)
                {
                    CHECK(bar->getLocalBounds().contains(shown[a]));
                    for (size_t b = a + 1; b < shown.size(); ++b)
                        CHECK_FALSE(shown[a].intersects(shown[b]));
                }
            }
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
        {
            juce::PNGImageFormat png;
            // Replaced, not appended to (an output stream appends to an existing file).
            const auto shellFile = juce::File(dump).getChildFile("shell-" + juce::String(width) + ".png");
            const auto topFile = juce::File(dump).getChildFile("transport-" + juce::String(width) + "@2x.png");
            shellFile.deleteFile();
            topFile.deleteFile();
            auto out = shellFile.createOutputStream();
            if (out != nullptr)
                png.writeImageToStream(image, *out);
            // The transport bar at 2x, as on the reference displays.
            auto top = topFile.createOutputStream();
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

    // Alt+S solos the selected channel; Ctrl+Alt+R hides and shows the returns (the master's row is
    // the song row, always there: ADR 0049).
    view.selectTrack(first);
    REQUIRE(actions.perform("track.solo"));
    f.session->updateSnapshotNow();
    CHECK(f.session->snapshot()->tracks[0].mix.solo);
    REQUIRE(actions.perform("view.returns"));
    CHECK_FALSE(view.mixRowsVisible());
    view.setSnapshot(f.session->snapshot());
    CHECK(findAll<fd::app::VolumeBar>(view).size() ==
          f.session->snapshot()->tracks.size() + 1); // and the master's
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
    return {40.0f, static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) +
                       kRowHeight * (static_cast<float>(row) + 0.5f)};
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

    // The return row by its lane, and the master by the song row's header (ADR 0049): their channel alone.
    click(view, laneAt(view, 6.0, rows));
    CHECK(view.selection() == Selection{.channel = snap->returns[0].id});
    CHECK(detail.chain().track() == snap->returns[0].id);
    CHECK(inspector.sections() == std::vector{S::Return, S::Mix});
    click(view, onClip);
    click(view, view.songHeaderArea().toFloat().getCentre().withX(40.0f));
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

    // A drag across empty space: a time selection on the rows it crossed (time-selection.md §5),
    // its channel the row it started on, told once when it ends.
    told = 0;
    drag(view, laneAt(view, 1.5, 0), laneAt(view, 0.2, 2));
    CHECK(view.selection().hasTime());
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{t0, t1, t2});
    CHECK(view.selection().channel == t0);
    CHECK(view.selection().clips.empty());
    CHECK(told == 1);

    // A clip dragged by its title strip to another track takes the channel along.
    const auto titleOf = [&](double seconds, int row)
    { return laneAt(view, seconds, row) - juce::Point<float>(0.0f, kRowHeight * 0.5f - 6.0f); };
    click(view, laneAt(view, 4.5, 2));
    told = 0;
    drag(view, titleOf(4.5, 2), titleOf(4.5, 1));
    CHECK(view.selection().channel == t1);
    CHECK(told == 1);
    f.session->updateSnapshotNow();
    view.setSnapshot(f.session->snapshot());
    CHECK(view.selection() == Selection{.channel = t1, .clips = {c}});
}

TEST_CASE("track headers: the instrument's name clears the mute and solo buttons", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    REQUIRE(s.addDevice(t0, "fd:subtractive-synth") != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().setSnapshot(s.snapshot());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    // The layout model keeps them apart (ModelTests); here, as drawn, for review.
    const auto headers = f.shell->arrangement()
                             .getLocalBounds()
                             .withWidth(fd::ui::Theme::current().shell.trackHeaderWidth + 40)
                             .withHeight(200);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "track-headers@2x.png",
                f.shell->arrangement().createComponentSnapshot(headers, true, 2.0f));
    CHECK(f.shell->arrangement().getHeight() > 0);
}

TEST_CASE("every action has a unique shortcut and the shell routes keys to it", "[integration]")
{
    ShellFixture f;
    const auto& actions = f.shell->actions();
    CHECK(actions.conflicts().empty());
    // ui-principles §1: a shortcut for every action but the rare ones the menus hold alone.
    const std::set<std::string> menuOnly{
        "take.useWhole", "take.rename", "take.copyToTrack", "take.moveToTrack", "take.keepOnly",
        "app.systemTitleBar", "app.about", "track.duplicate", "track.delete",
        // Reduced latency when monitoring (recording.md §22.1): a setting, also in Settings.
        "project.reducedLatency",
        // MIDI map mode (midi-learn.md §3): Ctrl+M is the metronome's; Settings > Keys can give it one.
        "midi.mapMode",
        // Presets (presets.md §10): on screen in the header; keys come with remapping.
        "module.previousPreset", "module.nextPreset", "module.savePreset", "module.savePresetAs",
        "module.revertPreset", "module.compare", "module.copyCompare", "module.useAsDefault",
        "module.clearDefault", "module.presetsFolder",
        // The browser and a project's files (browser.md §14): the switch is on screen; the
        // others are rare.
        "browser.preview", "file.collect", "file.removeUnused",
        // Clip loops (clip-loops.md §10): Ctrl+L and every L combination are taken; assignable.
        "clip.loop",
        // The song's sections (arranger.md §12): Delete, Ctrl+D and F2 act on a selected section,
        // a double-click loops it; the rest are rare.
        "song.addSection", "song.insertSection", "song.loopSection", "song.selectSectionTime",
        "song.duplicateSection", "song.deleteSection", "song.removeSectionMark",
        // The song's key (key-and-scale.md §13): the field is on screen; set once per song.
        "song.key", "song.noKey",
        // The Wavetable's tables (table-editor.md §8): a double-click on the table display edits;
        // Ctrl+Shift+E is the project's Export.
        "module.editWavetable", "module.importWavetable", "module.exportWavetable", "clip.makeWavetable",
        // The Sampler's sound (devices/sampler/SPEC.md, S10): a click on its panel does the same.
        "module.loadSample",
        // A Drum Kit's pads (drum-kit.md §12): a double-click renames, Ctrl+drag copies, and every
        // item is on the pad's right-click.
        "module.renamePad", "module.copyPad", "module.pastePad", "module.clearPad",
        // The Granular's sources (devices/granular/SPEC.md): its panel's picker and display load.
        "module.loadSource1", "module.loadSource2", "module.swapSources",
        // Racks (racks.md §9): Ctrl+G and Ctrl+Shift+G group and ungroup; the rest are on the
        // rack itself (its macro's and header's right-click, "+ Chain", a double-click on a name).
        "module.editMacros", "module.addChain", "module.renameRack", "module.saveRack", "module.saveRackAs",
        "module.racksFolder",
        // A rack's snapshots and Morph (racks.md §18.9): in the menu and on the rack's panel.
        "module.storeSnapshot", "module.updateSnapshot", "module.renameSnapshot", "module.removeSnapshot",
        "module.morph",
        // Modulators (modulators.md §15): no new shortcuts; the chain's + and the browser add
        // them, Keep is on the panel, Remove routes in the handle's and header's right-click.
        "module.addModulator.lfo", "module.addModulator.random", "module.addModulator.envelope",
        "module.addModulator.follower", "module.addModulator.steps", "module.keepRoll", "module.removeRoutes",
        "song.keepAllRolls",
        // The piano roll's tools (piano-roll.md §13): in the Notes menu, which the clip view's
        // Notes button opens where you work, and the most used on the notes' right-click menu.
        "notes.fitToKey", "notes.makeChords", "notes.changeChord", "notes.voiceSmooth", "notes.voiceClose",
        "notes.voiceOpen", "notes.voiceDrop2", "notes.voiceDrop3", "notes.voiceSpread", "notes.addBass",
        "notes.legato", "notes.fixedLength", "notes.doubleSpeed", "notes.halfSpeed", "notes.reverse",
        "notes.mirror", "notes.reroll", "notes.keepChance", "notes.tool.humanize", "notes.tool.strum",
        "notes.tool.arpeggiate", "notes.tool.chop", "notes.tool.ornament", "notes.tool.velocity",
        "notes.tool.span", "notes.tool.randomise", "notes.tool.rhythm", "notes.tool.melody",
        "notes.tool.chords", "notes.tool.bassline", "notes.selectSamePitch", "notes.selectOutsideKey",
        "notes.selectInLoop", "notes.invertSelection",
        // The clip view's options (piano-roll.md §4): on screen in its header.
        "view.clipOthers", "view.toolsTransform", "view.toolsGenerate", "view.drawNote", "view.drawChord",
        "view.drawChordUnder", "view.drawTriads", "view.drawSevenths", "view.drawNinths", "view.drawSus2",
        "view.drawSus4", "view.drawAdd9", "view.voicingSmooth", "view.voicingClose", "view.voicingOpen",
        "view.voicingDrop2", "view.voicingDrop3", "view.voicingSpread",
        // The Audio menu (audio-editor.md §13): on the editor's bar, its right-click and the inspector.
        "audio.gainUp", "audio.gainDown", "audio.silence", "audio.normalise", "audio.reverse",
        "audio.polarity", "audio.channels.stereo", "audio.channels.left", "audio.channels.right",
        "audio.channels.mono", "audio.channels.swapped", "audio.crop", "audio.fadeRange", "audio.loopRange",
        // The waveform's height: its level column dragged, double-clicked (§4).
        "view.waveformTaller", "view.waveformShorter", "view.waveformReset",
        // Warping (§7, §13): Warp, its mode, the tempo and ×2 ÷2 are on the bar and in the inspector;
        // the markers' commands on their right-click; Detect… on the bar's Tempo.
        "audio.warp", "audio.warpMode.beats", "audio.warpMode.tones", "audio.warpMode.repitch",
        "audio.tempoDouble", "audio.tempoHalf", "audio.detectTempo", "audio.songTempo",
        "audio.markersAtTransients", "audio.warpFromHere", "audio.setDownbeat", "audio.removeMarkers",
        // Transients by hand (§7.4): rarely needed; also on the marker strip's right-click.
        "audio.addTransient", "audio.removeTransients",
        // Follow key and Slice… (§8.3, §9): Follow beside the key in the inspector, Slice on the bar.
        "audio.followKey", "audio.slice",
        // Time selection (time-selection.md §6, §9): Select loop (Live's Ctrl+Shift+L adds a lane
        // here) and Cut time (Live's Ctrl+Shift+X crossfades here); both on a selection's menus.
        // Crop clip and Clip loop to selection (§14.1): the audio editor's have none either.
        "edit.selectLoop", "song.cutTime", "notes.cropClip", "notes.loopClip",
        // Groups (groups.md §12): Remove from group and the group's colour are rare, also on a
        // header's right-click; folding every group is the triangle's Alt+click.
        "track.removeFromGroup", "track.colourMembers", "view.foldGroups", "view.unfoldGroups"};
    for (const auto& a : actions.all())
    {
        INFO(a.id);
        CHECK_FALSE(a.name.isEmpty());
        CHECK(a.perform != nullptr);
        // An action that shares another's key (the Audio menu's Quantise and Transpose: the key
        // goes to whichever clip the clip view shows) shows that key, and has none of its own.
        if (!a.keysFrom.empty())
        {
            CHECK(a.keys.empty());
            CHECK(actions.shortcut(a.id) == actions.shortcut(a.keysFrom));
            CHECK(actions.shortcut(a.id).isNotEmpty());
            continue;
        }
        CHECK(a.keys.empty() == menuOnly.contains(a.id));
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
    const auto top = static_cast<float>(fd::app::ArrangementView::defaultRowsTop());

    // Grab the clip by its title strip at 0.5 s (its body selects time: time-selection.md §5), drag
    // right ~1.3 s and down one track.
    const juce::Point<float> down(laneX + static_cast<float>(tl.xForTime(0.5)), top + 6.0f);
    const auto to = down + juce::Point<float>(static_cast<float>(1.3 * tl.pixelsPerSecond()), 64.0f);
    view.mouseDown(mouseEvent(view, down, down, false));
    CHECK(view.selectedClips() == std::set<std::uint64_t>{clip});
    view.mouseDrag(mouseEvent(view, (down + to) * 0.5f, down, true));
    view.mouseDrag(mouseEvent(view, to, down, true));
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // the drag preview, for review
        dumpPng(dump, "arrangement-drag.png", view.createComponentSnapshot(view.getLocalBounds(), true, 2.0f));
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
        dumpPng(dump, "palette.png", f.shell->createComponentSnapshot({500, 0, 800, 520}, true, 2.0f));
    palette.setQuery("show brows");
    REQUIRE_FALSE(palette.shownIds().empty());
    CHECK(palette.shownIds().front() == "view.browser");
    const bool before = f.shell->actions().isToggled("view.browser");
    palette.performSelected();
    CHECK_FALSE(palette.isOpen());
    CHECK(f.shell->actions().isToggled("view.browser") != before);
    CHECK(palette.recent().front() == "view.browser");
}

TEST_CASE("piano roll: the clip's loop brace drawn, clicked and moved; the view shows the loop; the playhead wraps (C10)",
          "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    auto& roll = detail.pianoRoll();
    const auto track = s.snapshot()->tracks.front().id;
    // Two bars at 120 BPM, a note on each beat.
    const auto id = s.createMidiClip(track, 0.0, 4.0);
    std::vector<fd::model::Note> notes;
    for (int b = 0; b < 8; ++b)
        notes.push_back({.pitch = 60 + b, .start = static_cast<double>(b), .length = 0.5});
    s.setNotes(id, notes);
    s.updateSnapshotNow();
    auto clip = [&] {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), id)->first;
    };
    // Opened in the clip view (a double-click on its body).
    const auto& tl = view.timeline();
    const juce::Point<float> body(static_cast<float>(fd::app::ArrangementView::headerWidth()) +
                                      static_cast<float>(tl.xForTime(1.0)),
                                  static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 40.0f);
    click(view, body);
    click(view, body, 2);
    REQUIRE(detail.showsPianoRoll());
    f.shell->finishAnimations();
    auto settle = [&] { // the snapshot reaches the clip view
        s.updateSnapshotNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    };
    auto stripPoint = [&](double seconds) {
        const auto strip = roll.loopStripArea();
        return juce::Point<float>(static_cast<float>(strip.getX() + roll.timeline().xForTime(seconds)),
                                  static_cast<float>(strip.getCentreY()));
    };
    REQUIRE_FALSE(roll.loopStripArea().isEmpty());

    // Drawn on the strip over beats 2 to 6: the clip loops that, and the view shows its loop.
    drag(roll, stripPoint(1.0), stripPoint(3.0));
    CHECK(s.undoDescription() == "Loop");
    CHECK(clip().loop.start == Catch::Approx(1.0).margin(1e-6));
    CHECK(clip().loop.length == Catch::Approx(2.0).margin(1e-6));
    CHECK(clip().length == Catch::Approx(4.0)); // the clip as long as it was
    settle();

    // A click on the brace: Loop off; again: on, looping what the clip shows.
    const auto origin = fd::model::clipViewOrigin(clip());
    click(roll, stripPoint(origin + 2.0));
    CHECK(s.undoDescription() == "Stop looping clips");
    CHECK_FALSE(clip().loop.on());
    settle();
    click(roll, stripPoint(clip().start + 1.0));
    CHECK(s.undoDescription() == "Loop clips");
    CHECK(clip().loop.on());
    settle();

    // A one-bar loop out to four bars: the playhead in the third bar shows where in the loop it is.
    REQUIRE(s.setClipLoop(id, {0.0, 2.0}).empty());
    s.applyClipEdits({{.clip = id, .track = track, .start = 0.0, .length = 8.0, .offset = 0.0}});
    settle();
    roll.frame(4.5);
    CHECK(roll.shownPlayhead() == Catch::Approx(fd::model::clipViewOrigin(clip()) + 0.5));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "piano-roll-loop@2x.png", roll.createComponentSnapshot(roll.getLocalBounds(), true, 2.0f));
}

TEST_CASE("audio clip view: a click opens an audio clip there; its loop brace drawn and clicked; the "
          "playhead wraps (C10, P6a)",
          "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    auto& audio = detail.audioClipView();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto track = s.snapshot()->tracks.front().id;
    fd::testkit::AudioBuffer beat(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < beat.numSamples(); ++i)
            beat.at(c, i) = 0.8f * std::exp(-static_cast<float>(i % 12000) / 1500.0f) * std::sin(static_cast<float>(i) * 0.05f);
    const auto file = root.getChildFile("Drums.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), beat, 48000.0));
    const auto id = s.insertAudioFile(track, file, 0.0);
    s.applyClipEdits({{.clip = id, .track = track, .start = 0.0, .length = 2.0, .offset = 0.0}});
    s.updateSnapshotNow();
    auto clip = [&] {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), id)->first;
    };
    auto settle = [&] {
        s.updateSnapshotNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    };

    // Selected, it opens in the clip view, as a MIDI clip does (audio-editor.md §4).
    const auto& tl = view.timeline();
    const juce::Point<float> body(static_cast<float>(fd::app::ArrangementView::headerWidth()) +
                                      static_cast<float>(tl.xForTime(1.0)),
                                  static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 40.0f);
    click(view, body);
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Clip);
    REQUIRE(detail.showsAudioClip());
    CHECK_FALSE(detail.showsPianoRoll());
    f.shell->finishAnimations();
    audio.waitForWaveform();
    settle();
    REQUIRE(audio.waveformArea().getHeight() > 60);

    auto stripPoint = [&](double seconds) {
        const auto strip = audio.loopStripArea();
        return juce::Point<float>(static_cast<float>(audio.waveformArea().getX() + audio.timeline().xForTime(seconds)),
                                  static_cast<float>(strip.getCentreY()));
    };
    // Drawn on the strip: the clip loops a beat from half a second into its file.
    drag(audio, stripPoint(0.5), stripPoint(1.0));
    CHECK(s.undoDescription() == "Loop");
    CHECK(clip().loop.start == Catch::Approx(0.5).margin(1e-4));
    CHECK(clip().loop.length == Catch::Approx(0.5).margin(1e-4));
    settle();
    // A click on the brace: Loop off; again (on what the clip shows): on.
    click(audio, stripPoint(fd::model::clipViewOrigin(clip()) + 0.75));
    CHECK(s.undoDescription() == "Stop looping clips");
    settle();
    click(audio, stripPoint(clip().start + 0.1));
    CHECK(s.undoDescription() == "Loop clips");
    CHECK(clip().loop.on());
    settle();

    // Extended to 4 s: the playhead at 3.2 s shows where in the loop the song is.
    s.applyClipEdits({{.clip = id, .track = track, .start = 0.0, .length = 4.0, .offset = clip().offset}});
    settle();
    audio.frame(3.2);
    CHECK(audio.shownPlayhead() == Catch::Approx(fd::model::clipViewTime(clip(), 3.2)));
    CHECK(audio.shownPlayhead() < fd::model::clipViewOrigin(clip()) + clip().loop.end());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "audio-clip-view@2x.png", audio.createComponentSnapshot(audio.getLocalBounds(), true, 2.0f));
    root.deleteRecursively();
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
    const juce::Point<float> spot(static_cast<float>(fd::app::ArrangementView::headerWidth()) +
                                      static_cast<float>(tl.xForTime(2.1)),
                                  static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 20.0f);
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
        dumpPng(dump, "piano-roll.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
        dumpPng(dump, "piano-roll@2x.png", detail.createComponentSnapshot(detail.getLocalBounds().removeFromLeft(700), true, 2.0f));
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
    CHECK(utility->getX() < before); // narrower, down to what its header needs
    CHECK(utility->getX() == saturator->getX() + saturator->preferredWidth() + fd::app::DeviceChain::gap);
    CHECK(saturator->preferredWidth() ==
          std::max(saturator->headerMinimumWidth(),
                   before - fd::app::DeviceChain::gap - saturator->getX() -
                       (grid.sectionWidth(shape->columns()) - grid.collapsedWidth)));
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

TEST_CASE("the first module that delays its track says so, once per project", "[integration]")
{
    ShellFixture f;
    const auto t0 = f.session->snapshot()->tracks[0].id;
    const auto eq = f.session->addDevice(t0, "fd:eq");
    REQUIRE(eq != 0);
    f.shell->checkDelay();
    CHECK(f.shell->lastWarning().isEmpty()); // zero latency: nothing to say
    {
        auto handle = f.session->openDevice(eq);
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
    }
    f.shell->checkDelay();
    const auto said = f.shell->lastWarning();
    CHECK(said.startsWith("EQ on "));
    CHECK(said.contains(" ms of delay. The tracks stay in time; what you play live on "));
    f.shell->showWarning({});
    f.shell->checkDelay();
    CHECK(f.shell->lastWarning().isEmpty()); // once
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
            const auto popovers = findAll<fd::app::AssistantsPopover>(shell);
            auto* popover = popovers.empty() ? nullptr : popovers.front();
            REQUIRE(popover != nullptr);
            CHECK(popover->getRight() <= shell.getWidth());
            CHECK(popover->getY() >= fd::app::TransportBar::height);

            // On (the settings port may be taken on a developer machine: any free port will do).
            CHECK_FALSE(server.isRunning());
            server.setEnabled(true);
            if (!server.isRunning())
                REQUIRE(server.start(0));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            CHECK(Json::parse(folder.getChildFile("settings.json")
                                  .loadFileAsString()
                                  .toStdString())["assistants"]["enabled"] == true);

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
    CHECK(view.rows().size() ==
          tracks + 3 + s.snapshot()->returns.size()); // 3 lanes (the master: the song row)
    CHECK(view.rows()[tracks + 1].lane == freqLane);
    CHECK(view.rows()[tracks + 2].lane == panLane);
    view.setAllTrackHeights(64.0);

    // Double-click in the volume lane adds a point (one undo step).
    auto rowTop = [&](size_t row)
    {
        float y = static_cast<float>(fd::app::ArrangementView::defaultRowsTop());
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
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [&](auto* field) { return field->getParentComponent() != &inspector.content() || !field->isVisible(); }),
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
    CHECK(s.snapshot()->automationLane(freqLane)->assignments.size() == 2);

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
    CHECK(s.undoDescription() == "Delete automation lane");

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

    // The lane's name, typed in the inspector (the one field showing).
    auto editors = findAll<juce::TextEditor>(f.shell->inspector());
    editors.erase(std::remove_if(editors.begin(), editors.end(), [](auto* e) { return !e->isVisible(); }),
                  editors.end());
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
                                static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) +
                                    static_cast<float>(fd::model::rowOfLane(view.rows(), lane)) * 64.0f +
                                    64.0f - 1.0f - 5.0f);
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

    // The count-in toggle (no punch toggle: looping is punching).
    REQUIRE(actions.perform("transport.countIn"));
    CHECK(s.countIn() == fd::model::CountIn::OneBar);
    REQUIRE(actions.perform("transport.countIn"));
    CHECK(s.countIn() == fd::model::CountIn::Off);
    CHECK(actions.find("transport.punch") == nullptr);

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

TEST_CASE("takes: takes open under their clip after a loop pass; strips, the badge, the TAKE section, Alt+arrows, Delete, "
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
    // One clip with its three takes (ADR 0032), opened under it: no rows of their own.
    auto stack = [&] { return track().stacks.at(0); };
    REQUIRE(track().stacks.size() == 1);
    REQUIRE(stack().takes.size() == 3);
    CHECK(stack().open);
    auto& view = f.shell->arrangement();
    for (const auto& row : view.rows())
        CHECK((row.kind != fd::model::ArrangementRow::Kind::Track || row.channel != t0 || row.track == 0));
    REQUIRE(track().clips.size() == 1);
    CHECK(track().clips[0].fromTake == stack().takes[1].clips.at(0).id); // the last complete lap
    CHECK(fd::model::badge(stack(), track().clips, false)->text == "2/3");
    view.setView(0.0, 150.0); // 4..6 s well in sight
    view.frame(0.0, s.position());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: takes under their clip
    {
        juce::PNGImageFormat png;
        auto file = juce::File(dump).getChildFile("takes.png");
        file.deleteFile();
        if (auto out = file.createOutputStream())
            png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
    }

    // The track's row is its own 64 px and a 24 px strip per take below, newest on top.
    const float strip = static_cast<float>(fd::ui::Theme::current().shell.takeStripHeight);
    const float stripsTop = static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 64.0f;
    auto stripMiddle = [&](size_t take) { return stripsTop + strip * static_cast<float>(2 - take) + strip * 0.6f; };
    const auto laneX = [&](double seconds) {
        return static_cast<float>(fd::app::ArrangementView::headerWidth()) + static_cast<float>(view.timeline().xForTime(seconds));
    };
    // A click on a take's strip, on its clip: the clip plays it (one undo step), and it's selected.
    click(view, {laneX(5.0), stripMiddle(0)});
    s.updateSnapshotNow();
    CHECK(view.selection().takes == std::set{stack().takes[0].id});
    CHECK(s.undoDescription() == "Use take");
    // The speaker in a strip's name chip auditions its take: heard in place of the clip, changing
    // nothing; a second click stops it (ADR 0038).
    {
        const auto& theme = fd::ui::Theme::current();
        const juce::Point<float> speaker(laneX(4.0) + static_cast<float>(theme.shell.laneTextInset + theme.shell.takeIconSize / 2),
                                         stripMiddle(2));
        const auto playing = track().clips.at(0).fromTake;
        click(view, speaker);
        s.updateSnapshotNow();
        CHECK(track().audition == stack().takes[2].id);
        REQUIRE(track().clips.size() == 1);
        CHECK(track().clips[0].fromTake == playing); // the clip as it was
        view.setSnapshot(s.snapshot());
        if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: an auditioned take
        {
            dumpPng(dump, "takes-audition@2x.png",
                    view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(3.5)), 0, 500, 170), true, 2.0f));
            f.shell->setSize(1024, 800);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            dumpPng(dump, "takes-audition-compact.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
            f.shell->setSize(1800, 1000);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            view.setView(0.0, 150.0);
        }
        click(view, speaker);
        s.updateSnapshotNow();
        CHECK(track().audition == 0);
    }
    // Ctrl+click adds a take to the selected ones, and uses nothing; again, takes it out.
    {
        const auto ctrl = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
        const juce::Point<float> at(laneX(5.0), stripMiddle(1));
        view.mouseDown(mouseEvent(view, at, at, false, ctrl));
        view.mouseUp(mouseEvent(view, at, at, false, ctrl));
        s.updateSnapshotNow();
        CHECK(view.selection().takes == std::set{stack().takes[0].id, stack().takes[1].id});
        CHECK(track().clips[0].fromTake == stack().takes[0].clips.at(0).id);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(f.shell->inspector().sections().front() == S::Takes);
        if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: two takes selected
            dumpPng(dump, "takes-two@2x.png",
                    view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(3.5)), 0, 500, 170), true, 2.0f));
        view.mouseDown(mouseEvent(view, at, at, false, ctrl));
        view.mouseUp(mouseEvent(view, at, at, false, ctrl));
        CHECK(view.selection().takes == std::set{stack().takes[0].id});
        // Delete deletes every selected take, in one undo step.
        view.selectTake(t0, stack().takes[1].id, true);
        REQUIRE(actions.perform("edit.delete"));
        s.updateSnapshotNow();
        CHECK(stack().takes.size() == 1);
        CHECK(s.undoDescription() == "Delete takes");
        s.undo();
        s.updateSnapshotNow();
        REQUIRE(stack().takes.size() == 3);
        view.selectTake(t0, stack().takes[0].id);
    }
    REQUIRE(track().clips.size() == 1);
    CHECK(track().clips[0].fromTake == stack().takes[0].clips.at(0).id);
    CHECK(fd::model::badge(stack(), track().clips, false)->text == "1/3");
    // A click on the clip's badge (the title's right end) hides its takes, and another shows them.
    const juce::Point<float> badge(laneX(6.0) - 8.0f,
                                   static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 8.0f);
    click(view, badge);
    s.updateSnapshotNow();
    CHECK_FALSE(stack().open);
    CHECK(fd::model::takeStripsHeight(track(), strip) == 0.0);
    click(view, badge);
    s.updateSnapshotNow();
    CHECK(stack().open);

    // The inspector shows TAKE first; its "Use this take" uses the selected take.
    auto& inspector = f.shell->inspector();
    REQUIRE_FALSE(inspector.sections().empty());
    CHECK(inspector.sections().front() == S::Take);
    view.selectTake(t0, stack().takes[1].id);
    fd::ui::Toggle* use = nullptr;
    for (auto* t : findAll<fd::ui::Toggle>(inspector))
        if (t->getButtonText() == "Use this take" && t->isVisible())
            use = t;
    REQUIRE(use != nullptr);
    use->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    s.updateSnapshotNow();
    CHECK(track().clips.at(0).fromTake == stack().takes[1].clips.at(0).id);

    // Alt+Down / Alt+Up: the clip's next or previous take covering it (the third lap is short, so
    // it's skipped); the clip stays selected.
    view.setSelection(fd::model::withClips(*s.snapshot(), {track().clips.at(0).id}, t0, t0));
    REQUIRE(actions.perform("take.next"));
    s.updateSnapshotNow();
    CHECK(track().clips.at(0).fromTake == stack().takes[0].clips.at(0).id);
    CHECK(view.selection().clips == std::set{track().clips.at(0).id});
    REQUIRE(actions.perform("take.previous"));
    s.updateSnapshotNow();
    CHECK(track().clips.at(0).fromTake == stack().takes[1].clips.at(0).id);

    // A drag across a strip comps that stretch of its take in, on release (recording.md §7): one
    // undo step, "Comp" on the badge; while dragging, the stretch shows on the strip.
    {
        const juce::Point<float> from(laneX(4.5), stripMiddle(0)), to(laneX(5.5), stripMiddle(0));
        view.mouseDown(mouseEvent(view, from, from, false));
        view.mouseDrag(mouseEvent(view, to, from, true));
        s.updateSnapshotNow();
        CHECK(track().clips.size() == 1); // nothing yet
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
            dumpPng(dump, "takes-swipe@2x.png",
                    view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(3.5)), 0, 500, 170), true, 2.0f));
        view.mouseUp(mouseEvent(view, to, from, true));
        s.updateSnapshotNow();
        CHECK(s.undoDescription() == "Comp");
        REQUIRE(track().clips.size() == 3);
        CHECK(track().clips[1].fromTake == stack().takes[0].clips.at(0).id);
        CHECK(track().clips[1].start == Catch::Approx(4.5));
        CHECK(fd::model::badge(stack(), track().clips, false)->comp);
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
            dumpPng(dump, "takes-comped@2x.png",
                    view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(3.5)), 0, 500, 170), true, 2.0f));
        // A click on a strip uses that take for the section under it only (ADR 0038): the third
        // take for 4..4.5; the others stay.
        click(view, {laneX(4.25), stripMiddle(2)});
        s.updateSnapshotNow();
        REQUIRE(track().clips.size() == 3);
        CHECK(track().clips[0].fromTake == stack().takes[2].clips.at(0).id);
        CHECK(track().clips[0].end() == Catch::Approx(4.5));
        CHECK(track().clips[1].fromTake == stack().takes[0].clips.at(0).id);
        CHECK(track().clips[2].fromTake == stack().takes[1].clips.at(0).id);
        CHECK(s.undoDescription() == "Use take");
        s.undo();
        s.updateSnapshotNow();
        CHECK(track().clips[0].fromTake == stack().takes[1].clips.at(0).id);
        // The line between two takes, dragged in the body from 5.5 to 5: the take after it plays
        // from there (one undo step).
        const float body = static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 40.0f;
        const juce::Point<float> join(laneX(5.5), body), moved(laneX(5.0), body);
        s.setPosition(0.0); // the playhead out of the way
        view.frame(0.0, 0.0);
        view.mouseDown(mouseEvent(view, join, join, false));
        view.mouseDrag(mouseEvent(view, moved, join, true));
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
            dumpPng(dump, "takes-join@2x.png",
                    view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(3.5)), 0, 500, 170), true, 2.0f));
        view.mouseUp(mouseEvent(view, moved, join, true));
        s.updateSnapshotNow();
        CHECK(s.undoDescription() == "Comp");
        REQUIRE(track().clips.size() == 3);
        CHECK(track().clips[1].end() == Catch::Approx(5.0));
        {
            // On the strips, what the clip plays (the first take over 4.5..5) is outlined in the
            // clip's colour; the rest of the takes' audio is neutral.
            view.setSnapshot(s.snapshot());
            const auto image = view.createComponentSnapshot(view.getLocalBounds());
            const auto& theme = fd::ui::Theme::current();
            auto stripTop = [&](size_t take) { return static_cast<int>(stripsTop + strip * static_cast<float>(2 - take)); };
            const int x = static_cast<int>(laneX(4.75)), gap = theme.shell.clipGap;
            const auto colour = theme.clipColour(track().clips[1].colour);
            CHECK(image.getPixelAt(x, stripTop(0) + gap) == colour);                      // its border
            CHECK(image.getPixelAt(x, stripTop(0) + gap + 2) == theme.clipBody(colour, false)); // its body
            CHECK(image.getPixelAt(x, stripTop(1) + gap + 2) == theme.roles.takeUnused);  // the second take there
        }
        CHECK(track().clips[2].start == Catch::Approx(5.0));
        CHECK(track().clips[2].fromTake == stack().takes[1].clips.at(0).id);
        s.undo();
        s.updateSnapshotNow();
        CHECK(track().clips[1].end() == Catch::Approx(5.5));
        s.undo();
        s.updateSnapshotNow();
        REQUIRE(track().clips.size() == 1);
    }

    // Delete with a take selected deletes it (one undo step).
    view.selectTake(t0, stack().takes[2].id);
    REQUIRE(actions.perform("edit.delete"));
    s.updateSnapshotNow();
    CHECK(stack().takes.size() == 2);
    CHECK(s.undoDescription() == "Delete take");

    // Ctrl+Alt+K with the clip selected closes its takes (the row shrinks back); spread them.
    view.setSelection(fd::model::withClips(*s.snapshot(), {track().clips.at(0).id}, t0, t0));
    REQUIRE(actions.perform("view.takes"));
    s.updateSnapshotNow();
    CHECK_FALSE(stack().open);
    CHECK(fd::model::takeStripsHeight(track(), strip) == 0.0);
    const auto before = s.snapshot()->tracks.size();
    REQUIRE(actions.perform("take.spread"));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks.size() == before + 2);

    // A pass over part of the clip makes its own clip (ADR 0037): the first is cut where it starts,
    // and what it recorded over is the new clip's "Before take".
    auto recordPass = [&](double from, double seconds) {
        s.setArmed(t0, true, true);
        s.setPosition(from);
        REQUIRE(actions.perform("transport.record"));
        pump(seconds);
        REQUIRE(actions.perform("transport.playStop"));
        s.setArmed(t0, false, false);
        pump(0.1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        s.updateSnapshotNow();
    };
    recordPass(5.0, 2.0);
    REQUIRE(track().clips.size() == 2);
    REQUIRE(track().stacks.size() == 2);
    CHECK(track().stacks[1].takes.front().name.starts_with("Before take"));
    CHECK(fd::model::clipGroups(track()).size() == 2);
    // A pass over both makes one clip with all their takes; what played before it is two parts,
    // drawn as one clip when chosen.
    recordPass(3.5, 4.0);
    REQUIRE(track().clips.size() == 1);
    REQUIRE(track().stacks.size() == 1);
    const auto replaced = stack().takes.at(stack().takes.size() - 2);
    CHECK(replaced.name.starts_with("Before take"));
    REQUIRE(replaced.clips.size() == 2);
    REQUIRE(s.useTake(t0, replaced.id));
    s.setTakesOpen(stack().id, true);
    s.updateSnapshotNow();
    CHECK(fd::model::clipGroups(track()).size() == 1);
    {
        // Selected, every part of it is lighter, and so is a crossfade at its join (drawn as its
        // clip's body): just under the title, clear of the waveform.
        REQUIRE(track().clips.size() == 2);
        const auto parts = std::vector{track().clips[0].id, track().clips[1].id};
        REQUIRE(s.crossfade(parts, 0.2).size() == 1);
        s.updateSnapshotNow();
        const auto x = *track().clips[0].crossfadeOut;
        view.setSelection(fd::model::withClips(*s.snapshot(), {parts[0]}, t0, t0));
        view.setSnapshot(s.snapshot());
        s.setPosition(0.0); // the playhead out of the way
        view.frame(0.0, 0.0);
        const auto& theme = fd::ui::Theme::current();
        const auto image = view.createComponentSnapshot(view.getLocalBounds());
        const int y = fd::app::ArrangementView::defaultRowsTop() + theme.shell.clipGap +
                      theme.shell.clipTitleHeight + 2;
        const auto lighter = theme.clipBody(theme.clipColour(track().clips[0].colour), true);
        for (const double at : {4.5, 6.5, track().clips[0].end() + x.from + x.length * 0.5})
            CHECK(image.getPixelAt(static_cast<int>(laneX(at)), y) == lighter);
        // One clip in parts: no seam through its title; the line where the take changes, in its
        // body only, stays under the crossfade.
        const int inner = static_cast<int>(std::round(laneX(track().clips[1].start))) - theme.shell.clipGap;
        const int titleY = fd::app::ArrangementView::defaultRowsTop() + theme.shell.clipGap + 4;
        CHECK(image.getPixelAt(inner, titleY) == theme.clipColour(track().clips[0].colour));
        CHECK(image.getPixelAt(inner, y + 20) == theme.surface0); // below the crossfade's grip, between its curves
    }
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: a clip in parts, and a narrow clip's badge
        for (const auto& [name, zoom] : {std::pair{"takes-parts.png", 150.0}, std::pair{"takes-narrow.png", 24.0}})
        {
            view.setView(3.5, zoom);
            juce::PNGImageFormat png;
            auto file = juce::File(dump).getChildFile(name);
            file.deleteFile();
            if (auto out = file.createOutputStream())
                png.writeImageToStream(f.shell->createComponentSnapshot(f.shell->getLocalBounds()), *out);
        }

    // Capture MIDI: dim until something is played; then the phrase becomes a selected clip on the
    // selected track, which has no instrument and gets none (ADR 0033); its tooltip names it.
    const auto keys = s.snapshot()->tracks.back().id;
    view.selectTrack(keys);
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
    for (auto* child : f.shell->getChildren())
        if (auto* bar = dynamic_cast<fd::app::TransportBar*>(child))
        {
            bar->frame(s.position(), s.snapshot()->tempo);
            const auto name = juce::String::fromUTF8(s.snapshot()->channel(keys)->name.c_str());
            const auto buttons = findAll<fd::ui::IconButton>(*bar);
            CHECK(std::any_of(buttons.begin(), buttons.end(), [&](auto* b) {
                return b->getTooltip().startsWith("Capture MIDI to " + name);
            }));
        }
    s.setPosition(8.0);
    REQUIRE(actions.perform("transport.capture"));
    s.updateSnapshotNow();
    CHECK(s.undoDescription() == "Capture MIDI");
    REQUIRE(view.selection().clips.size() == 1);
    CHECK(s.snapshot()->channel(keys)->clips.size() == 1);
    CHECK(s.snapshot()->channel(keys)->instrument.empty());
    root.deleteRecursively();
}

TEST_CASE("bounce: Ctrl+B bounces the selected clips in place, 0 mutes, the clip menu and refusals (C7)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    s.setUnsavedRoot(root);
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id, t1 = s.snapshot()->tracks[1].id;
    REQUIRE(s.addDevice(t0, "fd:subtractive-synth") != 0);
    std::vector<fd::model::Note> notes;
    for (int i = 0; i < 8; ++i)
        notes.push_back({.pitch = 48 + (i * 5) % 12, .start = i * 0.5, .length = 0.4, .velocity = 0.8f});
    const auto clip = s.createMidiClip(t0, 0.5, 2.0);
    s.setNotes(clip, notes);
    const auto silent = s.createMidiClip(t1, 0.5, 2.0); // no instrument there
    s.setNotes(silent, notes);
    s.updateSnapshotNow();
    auto& view = f.shell->arrangement();
    view.setView(0.0, 150.0);
    auto track = [&](std::uint64_t id) {
        s.updateSnapshotNow();
        return *s.snapshot()->channel(id);
    };
    auto waitForBounces = [&] {
        for (int i = 0; i < 3000 && !s.bouncing().empty(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        s.updateSnapshotNow();
    };
    const juce::KeyPress ctrlB('b', juce::ModifierKeys::commandModifier, 0);

    // A clip that can't be bounced: dimmed in menus, and the shortcut says why; nothing changes.
    view.setSelection(fd::model::withClips(*s.snapshot(), {silent}, t1, t1));
    CHECK_FALSE(actions.isEnabled("clip.bounce"));
    CHECK(actions.handleKey(ctrlB));
    CHECK(f.shell->lastWarning() == juce::String(fd::model::refusalText(fd::model::BounceRefusal::NoInstrument)));
    CHECK(s.bouncing().empty());
    CHECK(actions.isEnabled("clip.mute")); // muting needs no instrument

    // The clip's menu: bounce and mute, then the edits; each with its shortcut, dimmed when it can't run.
    {
        juce::PopupMenu menu;
        REQUIRE(view.onClipMenu);
        view.onClipMenu(menu);
        std::vector<juce::String> names;
        std::vector<bool> enabled;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
            if (!it.getItem().isSeparator)
            {
                names.push_back(it.getItem().text);
                enabled.push_back(it.getItem().isEnabled);
            }
        CHECK(names == std::vector<juce::String>{"Bounce in place", "Bounce to new track", "Mute clips", "Loop clips",
                                                 "Crossfade", "Split at playhead", "Duplicate", "Delete", "Cut", "Copy",
                                                 "Paste", "Colour"}); // the clipboard and colour: tracks-and-clips.md §10
        CHECK_FALSE(enabled[0]); // no instrument on this clip's track
        CHECK(enabled[2]);
        CHECK(enabled[3]); // looping needs no instrument either
    }

    // Loop clips (C10, clip-loops.md §10): on loops what the clip shows, ticked; again, the repeats go.
    CHECK(actions.perform("clip.loop"));
    CHECK(s.undoDescription() == "Loop clips");
    CHECK(track(t1).clips[0].loop.on());
    CHECK(actions.isToggled("clip.loop"));
    CHECK(actions.perform("clip.loop"));
    CHECK_FALSE(track(t1).clips[0].loop.on());
    CHECK_FALSE(actions.isToggled("clip.loop"));

    // Ctrl+B: bounced in place, and the bounce is selected.
    view.setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
    REQUIRE(actions.isEnabled("clip.bounce"));
    REQUIRE(actions.handleKey(ctrlB));
    CHECK(s.bouncing().size() == 1);
    waitForBounces();
    REQUIRE(track(t0).clips.size() == 1);
    const auto bounced = track(t0).clips[0];
    CHECK(bounced.kind == fd::model::ClipInfo::Kind::Audio);
    CHECK(view.selection().clips == std::set{bounced.id});
    CHECK(view.selection().channel == t0);
    CHECK(s.undoDescription() == "Bounce in place");
    // A bounce keeps its source as a take: a clip with takes can't loop, and says why.
    CHECK_FALSE(actions.isEnabled("clip.loop"));
    CHECK(actions.find("clip.loop")->whyNot().contains("takes"));

    // 0 mutes the selected clips, and again unmutes them; the inspector's CLIP shows it.
    REQUIRE(actions.handleKey(juce::KeyPress('0')));
    CHECK(track(t0).clips[0].muted);
    CHECK(actions.isToggled("clip.mute"));
    CHECK_FALSE(actions.isEnabled("clip.bounce")); // a muted clip plays nothing to bounce
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    fd::ui::Toggle* mute = nullptr;
    for (auto* t : findAll<fd::ui::Toggle>(f.shell->inspector()))
        if (t->getButtonText() == "M" && t->isVisible() && f.shell->inspector().isParentOf(t))
            mute = t;
    REQUIRE(mute != nullptr);
    CHECK(mute->getToggleState());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: a muted bounce with its takes open
    {
        s.setTakesOpen(track(t0).stacks.at(0).id, true);
        s.updateSnapshotNow();
        view.frame(0.0, s.position());
        dumpPng(dump, "bounce-muted.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
        dumpPng(dump, "bounce-muted@2x.png",
                view.createComponentSnapshot(view.getLocalBounds().withHeight(220).withWidth(900), true, 2.0f));
        dumpPng(dump, "bounce-inspector@2x.png",
                f.shell->inspector().createComponentSnapshot(f.shell->inspector().getLocalBounds().withHeight(320), true, 2.0f));
        f.shell->setSize(1024, 800); // the window's minimum
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        dumpPng(dump, "bounce-compact.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
        f.shell->setSize(1800, 1000);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    mute->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK_FALSE(track(t0).clips[0].muted);
    CHECK(s.undoDescription() == "Unmute clips");

    // A long part: its progress shows on the clip while it renders; Escape cancels it.
    const auto longClip = s.createMidiClip(t0, 10.0, 60.0);
    std::vector<fd::model::Note> many;
    for (int i = 0; i < 120; ++i)
        many.push_back({.pitch = 48 + (i * 7) % 24, .start = i * 1.0, .length = 0.8, .velocity = 0.8f});
    s.setNotes(longClip, many);
    s.updateSnapshotNow();
    view.setSelection(fd::model::withClips(*s.snapshot(), {longClip}, t0, t0));
    REQUIRE(actions.perform("clip.bounce"));
    for (int i = 0; i < 300 && !s.bouncing().empty() && s.bouncing().front().progress < 0.3f; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    if (!s.bouncing().empty() && s.bouncing().front().seconds >= fd::ui::Theme::current().motion.progressDelay)
    {
        view.setView(8.0, 40.0);
        view.frame(0.0, s.position());
        if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: the progress line and its cancel
            dumpPng(dump, "bounce-progress@2x.png",
                    view.createComponentSnapshot(view.getLocalBounds().withHeight(160).withWidth(900), true, 2.0f));
        if (const char* dump = std::getenv("FD_DUMP_DIR")) // its cancel, at the right end of what's in view
            dumpPng(dump, "bounce-cancel@2x.png",
                    view.createComponentSnapshot(view.getLocalBounds().withHeight(160).removeFromRight(300), true, 2.0f));
        // Cancel, where the progress shows (menu-bar.md §15): a click on it cancels, as Escape does.
        std::optional<juce::Point<float>> cancel;
        for (float y = 0.0f; y < 160.0f && !cancel; y += 1.0f)
            for (float x = 0.0f; x < static_cast<float>(view.getWidth()) && !cancel; x += 1.0f)
                if (view.cancelAt({x, y}))
                    cancel = juce::Point<float>(x + 2.0f, y + 2.0f);
        CHECK(cancel.has_value());
        if (cancel && !s.bouncing().empty())
            click(view, *cancel);
    }
    CHECK(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) == !s.bouncing().empty());
    waitForBounces();
    CHECK(s.bouncing().empty());
    // Cancelled, or landed just before Escape: either way nothing is left half done.
    const auto after = track(t0);
    CHECK(std::all_of(after.clips.begin(), after.clips.end(), [](const auto& c) { return !c.muted; }));
    root.deleteRecursively();
}

TEST_CASE("fades: two crossfades in a short clip meet and never overlap; their grips split by side (C6)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    // Three 4 s files: A plays 0..2, a short B 2..2.4, C 2.4..4, each with audio past its edges.
    fd::testkit::AudioBuffer tone(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < tone.numSamples(); ++i)
            tone.at(c, i) = 0.5f * std::sin(static_cast<float>(i) * 0.02f);
    std::vector<std::uint64_t> ids;
    for (const auto& [name, start, length] : {std::tuple{"a.wav", 0.0, 2.0}, std::tuple{"b.wav", 2.0, 0.4}, std::tuple{"c.wav", 2.4, 1.6}})
    {
        const auto file = root.getChildFile(name);
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), tone, 48000.0));
        const auto id = s.insertAudioFile(t0, file, start);
        s.applyClipEdits({{.clip = id, .track = t0, .start = start, .length = length, .offset = 1.0}});
        ids.push_back(id);
    }
    // Two long crossfades: the second stops where the first ends, inside B.
    REQUIRE(s.setCrossfade(ids[0], fd::model::Crossfade{.length = 0.6, .from = -0.3}));
    REQUIRE(s.setCrossfade(ids[1], fd::model::Crossfade{.length = 0.6, .from = -0.3}));
    s.updateSnapshotNow();
    auto clipOf = [&](std::uint64_t id) {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), id)->first;
    };
    auto endOf = [&](std::uint64_t id) {
        const auto c = clipOf(id);
        return c.end() + c.crossfadeOut->from + c.crossfadeOut->length;
    };
    auto startOf = [&](std::uint64_t id) {
        const auto c = clipOf(id);
        return c.end() + c.crossfadeOut->from;
    };
    REQUIRE(clipOf(ids[1]).crossfadeOut);
    CHECK(endOf(ids[0]) == Catch::Approx(2.3));
    CHECK(startOf(ids[1]) == Catch::Approx(2.3));

    auto& view = f.shell->arrangement();
    view.setView(1.5, 400.0);
    view.setSelection(fd::model::withClips(*s.snapshot(), {ids[1]}, t0, t0)); // both crossfades' grips show
    for (int i = 0; i < 50; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        view.frame(0.02, s.position());
    }
    const auto& theme = fd::ui::Theme::current();
    const float lanesX = static_cast<float>(fd::app::ArrangementView::headerWidth());
    auto laneX = [&](double seconds) { return lanesX + static_cast<float>(view.timeline().xForTime(seconds)); };
    const float rowTop = static_cast<float>(fd::app::ArrangementView::defaultRowsTop());
    const float handleY = rowTop + static_cast<float>(theme.shell.clipGap + theme.shell.clipTitleHeight) +
                          static_cast<float>(theme.shell.fadeHandleSize) * 0.5f;
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: two crossfades meeting in B
        dumpPng(dump, "crossfades-meet@2x.png",
                view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(1.6)), 0, 420, 130), true, 2.0f));

    // On the spot where they meet: left of it drags the first one's end, right of it the second's start.
    const float spot = laneX(2.3);
    drag(view, {spot - 2.0f, handleY}, {spot - 40.0f, handleY});
    CHECK(endOf(ids[0]) == Catch::Approx(2.2).margin(0.01));
    CHECK(startOf(ids[1]) == Catch::Approx(2.3));
    s.undo();
    drag(view, {spot + 2.0f, handleY}, {spot + 40.0f, handleY});
    CHECK(startOf(ids[1]) == Catch::Approx(2.4).margin(0.01));
    CHECK(endOf(ids[0]) == Catch::Approx(2.3));
    s.undo();
    // Dragged past each other, they stop where the other begins.
    drag(view, {spot - 2.0f, handleY}, {laneX(3.0), handleY});
    CHECK(endOf(ids[0]) == Catch::Approx(2.3));
    root.deleteRecursively();
}

TEST_CASE("clip loops: every lap drawn the same, a notch and a line at each seam, the loop mark (C10)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id, t1 = s.snapshot()->tracks[1].id;
    // A drum-like loop: a decaying burst every quarter second, 4 s long.
    fd::testkit::AudioBuffer beat(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < beat.numSamples(); ++i)
            beat.at(c, i) = 0.8f * std::exp(-static_cast<float>(i % 12000) / 1500.0f) * std::sin(static_cast<float>(i) * 0.05f);
    const auto file = root.getChildFile("Drums.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), beat, 48000.0));
    const auto audio = s.insertAudioFile(t0, file, 0.0);
    s.applyClipEdits({{.clip = audio, .track = t0, .start = 0.0, .length = 0.5, .offset = 0.0}});
    REQUIRE(s.setClipsLooping({audio}, true).empty());
    s.applyClipEdits({{.clip = audio, .track = t0, .start = 0.0, .length = 3.0, .offset = 0.0}}); // six laps
    // A one-bar MIDI loop, out to four bars.
    const auto midi = s.createMidiClip(t1, 0.0, 2.0);
    s.setNotes(midi, {{.pitch = 60, .start = 0.0, .length = 0.5}, {.pitch = 64, .start = 1.0, .length = 0.5},
                      {.pitch = 67, .start = 2.0, .length = 0.5}, {.pitch = 72, .start = 3.0, .length = 0.5}});
    REQUIRE(s.setClipsLooping({midi}, true).empty());
    s.applyClipEdits({{.clip = midi, .track = t1, .start = 0.0, .length = 8.0, .offset = 0.0}});
    s.updateSnapshotNow();

    auto& view = f.shell->arrangement();
    view.setView(0.0, 300.0); // 300 px a second: the audio clip whole in view
    for (int i = 0; i < 50; ++i) // the waveforms build in the background
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        view.frame(0.02, s.position());
    }
    const float lanesX = static_cast<float>(fd::app::ArrangementView::headerWidth());
    auto laneX = [&](double seconds) { return static_cast<int>(std::round(lanesX + static_cast<float>(view.timeline().xForTime(seconds)))); };
    const auto& theme = fd::ui::Theme::current();
    const int rowTop = fd::app::ArrangementView::defaultRowsTop();
    const int titleTop = rowTop + theme.shell.clipGap;
    const int bodyTop = titleTop + theme.shell.clipTitleHeight;
    const auto image = view.createComponentSnapshot(view.getLocalBounds());
    const auto colour = theme.clipColour(fd::model::findClip(*s.snapshot(), audio)->first->colour);
    // At each seam: a notch of the canvas in the title strip, a line of the clip's colour in the body.
    for (const double seam : {0.5, 1.0, 2.5})
    {
        INFO("seam at " << seam);
        CHECK(image.getPixelAt(laneX(seam), titleTop + 1) == theme.surface0);
        CHECK(image.getPixelAt(laneX(seam), titleTop + theme.shell.loopNotch + 2) == colour);
        CHECK(image.getPixelAt(laneX(seam), bodyTop + 20) == colour);
    }
    // Every lap the same: the same content at the same place in two laps.
    for (int y = bodyTop + 2; y < bodyTop + 40; y += 3)
        CHECK(image.getPixelAt(laneX(0.6), y) == image.getPixelAt(laneX(1.6), y));
    // The loop mark at the end of the title strip: the icon in the canvas colour on the clip's.
    int markPixels = 0;
    for (int x = laneX(3.0) - 16; x < laneX(3.0); ++x)
        for (int y = titleTop; y < bodyTop; ++y)
            markPixels += image.getPixelAt(x, y) == theme.surface0 ? 1 : 0;
    CHECK(markPixels > 4);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dump, "loops@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(220).withWidth(1500), true, 2.0f));
        view.setView(0.0, 60.0); // zoomed out: laps too close to tell apart
        view.frame(0.02, s.position());
        dumpPng(dump, "loops-zoomed-out@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(220).withWidth(700), true, 2.0f));
        view.setView(0.0, 1600.0);
        view.frame(0.02, s.position());
        dumpPng(dump, "loops-zoomed-in@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(220).withWidth(1500), true, 2.0f));
    }
    root.deleteRecursively();
}

TEST_CASE("clip loops by mouse: the upper edge loops a clip, a notch sets the loop's length, edges extend (C10)",
          "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    fd::testkit::AudioBuffer beat(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < beat.numSamples(); ++i)
            beat.at(c, i) = 0.8f * std::exp(-static_cast<float>(i % 12000) / 1500.0f) * std::sin(static_cast<float>(i) * 0.05f);
    const auto file = root.getChildFile("Drums.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), beat, 48000.0));
    const auto clip = s.insertAudioFile(t0, file, 0.0);
    s.applyClipEdits({{.clip = clip, .track = t0, .start = 0.0, .length = 0.5, .offset = 0.0}}); // one beat
    auto info = [&] {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), clip)->first;
    };

    auto& view = f.shell->arrangement();
    view.setView(0.0, 300.0);
    for (int i = 0; i < 10; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        view.frame(0.02, s.position());
    }
    const float lanesX = static_cast<float>(fd::app::ArrangementView::headerWidth());
    auto laneX = [&](double seconds) { return lanesX + static_cast<float>(view.timeline().xForTime(seconds)); };
    const auto& theme = fd::ui::Theme::current();
    const float titleY =
        static_cast<float>(fd::app::ArrangementView::defaultRowsTop() + theme.shell.clipGap) + 5.0f;
    const float bodyY = titleY + 30.0f;

    // The upper part of its right edge: the loop cursor; dragged out, it loops and repeats.
    view.mouseMove(mouseEvent(view, {laneX(0.5) - 2.0f, titleY}, {laneX(0.5) - 2.0f, titleY}, false, {}));
    CHECK_FALSE(view.getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor);
    CHECK_FALSE(view.getMouseCursor() == juce::MouseCursor::NormalCursor);
    CHECK(view.getTooltip() == "Drag to loop the clip");
    view.mouseMove(mouseEvent(view, {laneX(0.5) - 2.0f, bodyY + 20.0f}, {laneX(0.5) - 2.0f, bodyY + 20.0f}, false, {}));
    CHECK(view.getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor); // below: it trims
    // The loop cursor is a system cursor's size (the product owner: at least twice the 16 px first
    // tried), the icon outlined.
    const auto cursor = fd::ui::cursorImage(fd::ui::Icon::Loop);
    CHECK(cursor.getImage().getWidth() / cursor.getScale() >= 32.0);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "loop-cursor@2x.png", cursor.getImage());
    drag(view, {laneX(0.5) - 2.0f, titleY}, {laneX(2.0), titleY});
    CHECK(s.undoDescription() == "Loop clips");
    CHECK(info().loop == fd::model::ClipLoop{0.0, 0.5});
    CHECK(info().length == Catch::Approx(2.0).margin(1e-3));
    view.setSnapshot(s.snapshot());

    // A notch: the left-right cursor; dragged, the loop's length (snapped), the laps reflow. While
    // dragging, the tag says what it'll be.
    view.mouseMove(mouseEvent(view, {laneX(0.5), titleY}, {laneX(0.5), titleY}, false, {}));
    CHECK(view.getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor);
    CHECK(view.getTooltip() == "Drag to change the loop's length");
    const juce::Point<float> notch{laneX(0.5), titleY}, to{laneX(0.75), titleY};
    view.mouseDown(mouseEvent(view, notch, notch, false));
    view.mouseDrag(mouseEvent(view, (notch + to) * 0.5f, notch, true));
    view.mouseDrag(mouseEvent(view, to, notch, true));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "loop-notch-drag@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(130).withWidth(1100), true, 2.0f));
    view.mouseUp(mouseEvent(view, to, notch, true));
    CHECK(s.undoDescription() == "Loop length");
    CHECK(info().loop.length == Catch::Approx(0.75).margin(1e-3));
    CHECK(info().length == Catch::Approx(2.0).margin(1e-3));
    view.setSnapshot(s.snapshot());

    // The lower part of its right edge extends it further, the loop unchanged.
    drag(view, {laneX(2.0) - 2.0f, bodyY + 20.0f}, {laneX(3.0), bodyY + 20.0f});
    CHECK(s.undoDescription() == "Move clips");
    CHECK(info().length == Catch::Approx(3.0).margin(1e-3));
    CHECK(info().loop.length == Catch::Approx(0.75).margin(1e-3));
    view.setSnapshot(s.snapshot());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "loop-extended@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(130).withWidth(1300), true, 2.0f));
    root.deleteRecursively();
}

TEST_CASE("fades: drawn on clips, handles on hover, a handle drag sets a fade, Ctrl+Shift+X crossfades (C6)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    // A 4 s sine in two files; the first up to 2 s, the copy from 2 s: they meet at 2 s.
    fd::testkit::AudioBuffer sine(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < sine.numSamples(); ++i)
            sine.at(c, i) = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
    const auto fa = root.getChildFile("a.wav"), fb = root.getChildFile("b.wav");
    REQUIRE(fd::testkit::writeWav(fa.getFullPathName().toStdString(), sine, 48000.0));
    REQUIRE(fd::testkit::writeWav(fb.getFullPathName().toStdString(), sine, 48000.0));
    const auto a = s.insertAudioFile(t0, fa, 0.0);
    s.applyClipEdits({{.clip = a, .track = t0, .start = 0.0, .length = 2.0, .offset = 0.0}});
    const auto b = s.insertAudioFile(t0, fb, 2.0);
    s.applyClipEdits({{.clip = b, .track = t0, .start = 2.0, .length = 2.0, .offset = 2.0}});
    s.setFades({a}, fd::model::Fade{0.5, 0.4f}, std::nullopt);
    s.setFades({b}, std::nullopt, fd::model::Fade{0.8, 0.0f});
    s.updateSnapshotNow();
    auto clipOf = [&](std::uint64_t id) {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), id)->first;
    };

    auto& view = f.shell->arrangement();
    view.setView(0.0, 400.0); // 4 s across 1600 px
    for (int i = 0; i < 50; ++i) // the waveforms build in the background
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        view.frame(0.02, s.position());
    }
    const float lanesX = static_cast<float>(fd::app::ArrangementView::headerWidth());
    auto laneX = [&](double seconds) { return lanesX + static_cast<float>(view.timeline().xForTime(seconds)); };
    const auto& theme = fd::ui::Theme::current();
    const float rowTop = static_cast<float>(fd::app::ArrangementView::defaultRowsTop());
    const float bodyTop = rowTop + static_cast<float>(theme.shell.clipGap) + static_cast<float>(theme.shell.clipTitleHeight);
    const float handleY = bodyTop + static_cast<float>(theme.shell.fadeHandleSize) * 0.5f;
    // Where two clips meet, a seam of the canvas separates them, through the title and the body.
    auto pixel = [&](double seconds, int dx, float y) {
        const auto image = view.createComponentSnapshot(view.getLocalBounds());
        return image.getPixelAt(static_cast<int>(std::round(laneX(seconds))) + dx, static_cast<int>(y));
    };
    const float titleY = rowTop + static_cast<float>(theme.shell.clipGap) + 4.0f;
    const auto clipColour = theme.clipColour(clipOf(a).colour);
    CHECK(pixel(2.0, -1, titleY) == theme.surface0);
    CHECK(pixel(2.0, -1, bodyTop + 2.0f) == theme.surface0);
    CHECK(pixel(2.0, -2, titleY) == clipColour);
    CHECK(pixel(2.0, 0, titleY) == clipColour);

    // The pointer over a clip shows its handles; a drag of its fade-in handle sets the fade (one step).
    view.mouseMove(mouseEvent(view, {laneX(1.0), rowTop + 40.0f}, {laneX(1.0), rowTop + 40.0f}, false, {}));
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: fades, a hovered clip's handles
        dumpPng(dump, "fades@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(130).withWidth(1000), true, 2.0f));
    drag(view, {laneX(0.5), handleY}, {laneX(0.8), handleY});
    CHECK(clipOf(a).fadeIn.length == Catch::Approx(0.8).margin(0.01));
    CHECK(clipOf(a).fadeIn.bend == Catch::Approx(0.4f)); // its bend kept
    CHECK(s.undoDescription() == "Fades");

    // Its fade-out handle (at its corner) dragged past the join makes a crossfade there.
    drag(view, {laneX(2.0) - 2.0f, handleY}, {laneX(2.1), handleY});
    REQUIRE(clipOf(a).crossfadeOut);
    CHECK(clipOf(a).crossfadeOut->length == Catch::Approx(0.2).margin(0.01));
    CHECK(s.undoDescription() == "Crossfade");
    view.setSnapshot(s.snapshot());
    CHECK(pixel(2.0, -1, titleY) == theme.surface0); // the crossfade keeps the seam
    CHECK(pixel(2.0, -1, bodyTop + 2.0f) == theme.surface0);
    view.mouseMove(mouseEvent(view, {laneX(2.5), rowTop + 40.0f}, {laneX(2.5), rowTop + 40.0f}, false, {}));
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: the crossfade, the sum it plays, its grips
    {
        const auto area = juce::Rectangle<int>(static_cast<int>(laneX(1.5)), 0, 500, 130);
        (void)view.createComponentSnapshot(area); // asks for the sum, built in the background
        for (int i = 0; i < 30; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        dumpPng(dump, "crossfade@2x.png", view.createComponentSnapshot(area, true, 2.0f));
    }

    // Dragging its end shows what it's becoming next to the pointer (for review).
    {
        const juce::Point<float> end(laneX(2.1), handleY), to(laneX(2.2), handleY);
        view.mouseDown(mouseEvent(view, end, end, false));
        view.mouseDrag(mouseEvent(view, to, end, true));
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
            dumpPng(dump, "crossfade-drag@2x.png",
                    view.createComponentSnapshot(juce::Rectangle<int>(static_cast<int>(laneX(1.6)), 0, 500, 130), true, 2.0f));
        view.mouseUp(mouseEvent(view, to, end, true));
        CHECK(clipOf(a).crossfadeOut->length == Catch::Approx(0.3).margin(0.01)); // its start stayed put
        CHECK(s.undoDescription() == "Crossfade");
    }

    // A double-click on either end removes it; Ctrl+Shift+X on the two clips makes a 10 ms one.
    const juce::Point<float> end(laneX(2.2), handleY);
    click(view, end);
    click(view, end, 2);
    s.updateSnapshotNow();
    CHECK_FALSE(clipOf(a).crossfadeOut);
    CHECK(s.undoDescription() == "Remove crossfade");
    s.updateSnapshotNow();
    view.setSelection(fd::model::withClips(*s.snapshot(), {a, b}, t0, t0));
    REQUIRE(actions.isEnabled("clip.crossfade"));
    REQUIRE(actions.handleKey(juce::KeyPress('x', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE(clipOf(a).crossfadeOut);
    CHECK(clipOf(a).crossfadeOut->length == Catch::Approx(fd::model::kCrossfadeSeconds));

    // The inspector's CLIP shows an audio clip's fades, and typing one sets it.
    view.setSelection(fd::model::withClips(*s.snapshot(), {b}, t0, t0));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    std::vector<fd::ui::NumberField*> fields;
    for (auto* field : findAll<fd::ui::NumberField>(f.shell->inspector()))
        if (field->isVisible() && f.shell->inspector().isParentOf(field))
            fields.push_back(field);
    // Fade in, fade out, their curves (menu-bar.md §15), then AUDIO's gain, transpose, fine and
    // sensitivity.
    REQUIRE(fields.size() == 8);
    fields[0]->setValue(25.0, juce::sendNotification);
    CHECK(clipOf(b).fadeIn.length == Catch::Approx(0.025));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dump, "fades-inspector@2x.png",
                f.shell->inspector().createComponentSnapshot(f.shell->inspector().getLocalBounds().withHeight(300), true, 2.0f));
        dumpPng(dump, "fades-window.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
        f.shell->setSize(1024, 800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        dumpPng(dump, "fades-compact.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
        f.shell->setSize(1800, 1000); // back, for the checks after it
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    // Edge fades toggle from the palette's action, one undo step.
    REQUIRE(actions.perform("project.edgeFades"));
    CHECK_FALSE(s.edgeFades());
    CHECK(s.undoDescription() == "Edge fades");
    REQUIRE(actions.perform("project.edgeFades"));

    // Many clips edge to edge, one with a crossfade, one selected: each its own tile (for review,
    // at the standard and compact widths, and zoomed out).
    REQUIRE(s.splitClips({b}, 2.5).size() == 1);
    s.updateSnapshotNow();
    for (const double at : {2.8, 3.1, 3.6})
        for (const auto& c : s.snapshot()->tracks[0].clips)
            if (c.start < at && c.end() > at)
            {
                REQUIRE(s.splitClips({c.id}, at).size() == 1);
                s.updateSnapshotNow();
                break;
            }
    const auto& clips = s.snapshot()->tracks[0].clips;
    REQUIRE(clips.size() == 6);
    REQUIRE(s.setCrossfade(a, fd::model::Crossfade{.length = 0.1, .from = -0.05}));
    s.updateSnapshotNow();
    view.setSelection(fd::model::withClips(*s.snapshot(), {s.snapshot()->tracks[0].clips[4].id}, t0, t0));
    view.setSnapshot(s.snapshot());
    view.setView(0.0, 250.0); // all of it in sight
    for (const auto& c : s.snapshot()->tracks[0].clips) // a seam at every join
        if (c.start > 0.0)
        {
            const auto seam = pixel(c.start, -1, titleY);
            INFO("the join at " << c.start << " s: " << seam.toDisplayString(true) << ", left "
                                << pixel(c.start, -2, titleY).toDisplayString(true) << ", right "
                                << pixel(c.start, 0, titleY).toDisplayString(true));
            CHECK(seam == theme.surface0);
        }
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        f.shell->setSize(1800, 1000);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        view.setView(1.5, 600.0);
        (void)view.createComponentSnapshot(view.getLocalBounds()); // asks for the crossfade's sum
        s.updateSnapshotNow();
        for (int i = 0; i < 30; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        dumpPng(dump, "seams@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(130).withWidth(1500), true, 2.0f));
        view.setView(0.0, 60.0);
        dumpPng(dump, "seams-out@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(130).withWidth(700), true, 2.0f));
        f.shell->setSize(1024, 800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        view.setView(0.0, 200.0);
        dumpPng(dump, "seams-compact.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
    }
    root.deleteRecursively();
}


TEST_CASE("export: Ctrl+Shift+E opens the panel, Enter exports in the background, a toast with Show (C8)", "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    fd::testkit::AudioBuffer tone(2, 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < tone.numSamples(); ++i)
            tone.at(c, i) = 0.2f * std::sin(static_cast<float>(i) * 0.05f);
    const auto file = root.getChildFile("tone.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), tone, 48000.0));
    REQUIRE(s.insertAudioFile(s.snapshot()->tracks[0].id, file, 0.0) != 0);
    s.updateSnapshotNow();

    // Ctrl+Shift+E: the panel, as the project's settings say.
    REQUIRE(actions.handleKey(juce::KeyPress('e', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    auto& panel = f.shell->exportPanel();
    REQUIRE(panel.isOpen());
    // Nothing touches: every control inside the padding, none overlapping another.
    {
        const auto& theme = fd::ui::Theme::current();
        const auto inside = panel.getLocalBounds().reduced(theme.shell.padding);
        std::vector<juce::Rectangle<int>> shown;
        for (auto* child : panel.getChildren())
            if (child->isVisible())
            {
                INFO(child->getName() << " " << child->getBounds().toString());
                CHECK(inside.contains(child->getBounds()));
                for (const auto& other : shown)
                    CHECK_FALSE(other.intersects(child->getBounds()));
                shown.push_back(child->getBounds());
            }
    }
    // A choice is the project's at once; a format holds only its depths.
    auto boxes = findAll<juce::ComboBox>(panel);
    // what, range, format, depth, quality, rate, dither, normalise, also as, its detail
    REQUIRE(boxes.size() == 10);
    boxes[2]->setSelectedId(2, juce::sendNotificationSync); // FLAC
    CHECK(s.exportSettings().format == fd::model::ExportFormat::Flac);
    CHECK_FALSE(boxes[3]->isItemEnabled(32));
    boxes[2]->setSelectedId(1, juce::sendNotificationSync); // WAV again
    // MP3 (export.md §17): Quality in Bit depth's place, no Dither; Also as WAV: its depth's row.
    {
        const int wavHeight = panel.getHeight();
        boxes[2]->setSelectedId(4, juce::sendNotificationSync);
        CHECK(s.exportSettings().format == fd::model::ExportFormat::Mp3);
        CHECK(s.exportSettings().kbps == 320);
        CHECK(boxes[4]->isVisible());
        CHECK_FALSE(boxes[3]->isVisible());
        CHECK_FALSE(boxes[6]->isVisible()); // dither: no PCM file
        CHECK(panel.getHeight() == wavHeight - fd::ui::Theme::current().shell.rowHeight);
        boxes[8]->setSelectedId(static_cast<int>(fd::model::ExportFormat::Wav) + 2,
                                juce::sendNotificationSync);
        REQUIRE(s.exportSettings().alsoAs == fd::model::ExportFormat::Wav);
        CHECK(boxes[9]->isVisible());
        CHECK(boxes[6]->isVisible()); // the WAV is dithered
        CHECK_FALSE(boxes[8]->isItemEnabled(static_cast<int>(fd::model::ExportFormat::Mp3) + 2));
        const auto inside = panel.getLocalBounds().reduced(fd::ui::Theme::current().shell.padding);
        std::vector<juce::Rectangle<int>> shown;
        for (auto* child : panel.getChildren())
            if (child->isVisible())
            {
                CHECK(inside.contains(child->getBounds()));
                for (const auto& other : shown)
                    CHECK_FALSE(other.intersects(child->getBounds()));
                shown.push_back(child->getBounds());
            }
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
            dumpPng(dump, "export-mp3-also-wav@2x.png",
                    panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
        boxes[8]->setSelectedId(1, juce::sendNotificationSync);
        boxes[2]->setSelectedId(1, juce::sendNotificationSync);
        CHECK(panel.getHeight() == wavHeight);
    }
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: the panel, and the window
    {
        dumpPng(dump, "export-panel@2x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
        dumpPng(dump, "export-window.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
    }
    // Stems and a loudness target: their rows appear, and the panel grows to hold them.
    const int small = panel.getHeight();
    boxes[0]->setSelectedId(3, juce::sendNotificationSync); // all tracks
    boxes[7]->setSelectedId(3, juce::sendNotificationSync); // loudness
    CHECK(panel.getHeight() == small + 2 * fd::ui::Theme::current().shell.rowHeight);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "export-stems@2x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
    boxes[0]->setSelectedId(1, juce::sendNotificationSync);
    boxes[7]->setSelectedId(1, juce::sendNotificationSync);
    CHECK(panel.getHeight() == small);

    // Enter: it exports in the background; the panel shows its progress, then its report.
    REQUIRE(panel.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    CHECK(panel.progress().has_value());
    for (int i = 0; i < 1000 && panel.progress(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE_FALSE(panel.progress());
    CHECK(s.exportsFolder().getChildFile("Untitled.wav").existsAsFile());
    CHECK(f.shell->lastWarning().startsWith("Exported Untitled.wav"));
    CHECK(f.shell->lastWarning().contains("LUFS"));
    // Show in browser (§17.3): the browser at the file, in the project's Exports.
    REQUIRE(panel.inBrowserButton().isVisible());
    panel.inBrowserButton().onClick();
    CHECK(f.shell->browser().selected() ==
          "file:" + s.exportsFolder().getChildFile("Untitled.wav").getFullPathName().toStdString());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: the report, and the toast
    {
        dumpPng(dump, "export-report@2x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
        panel.close();
        dumpPng(dump, "export-toast.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
        f.shell->setSize(1024, 800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        actions.perform("project.export");
        dumpPng(dump, "export-compact.png", f.shell->createComponentSnapshot(f.shell->getLocalBounds()));
    }
    // Escape closes it; Ctrl+Shift+E again, it's as it was left.
    if (!panel.isOpen())
        actions.perform("project.export");
    REQUIRE(panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(panel.isOpen());
    // A song that leaves something to chance (ADR 0062): the panel says so, Keep all as played
    // beside it, and nothing touches; kept, the line goes.
    CHECK_FALSE(panel.showsChance());
    REQUIRE(s.addModulator(s.snapshot()->tracks[0].id, fd::model::ModulatorKind::Random) != 0);
    s.updateSnapshotNow();
    actions.perform("project.export");
    REQUIRE(panel.showsChance());
    CHECK(panel.keepAllButton().isVisible());
    {
        const auto inside = panel.getLocalBounds().reduced(fd::ui::Theme::current().shell.padding);
        std::vector<juce::Rectangle<int>> shown;
        for (auto* child : panel.getChildren())
            if (child->isVisible())
            {
                CHECK(inside.contains(child->getBounds()));
                for (const auto& other : shown)
                    CHECK_FALSE(other.intersects(child->getBounds()));
                shown.push_back(child->getBounds());
            }
    }
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "export-chance@2x.png",
                panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
    const int withChance = panel.getHeight();
    panel.keepAllButton().onClick();
    CHECK_FALSE(panel.showsChance());
    CHECK(panel.getHeight() < withChance);
    REQUIRE(panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    // With nothing to export, Enter says why.
    s.setExportSettings({.range = fd::model::ExportRange::Loop});
    actions.perform("project.export");
    REQUIRE(panel.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    CHECK(f.shell->lastWarning().contains("loop"));
    CHECK_FALSE(panel.progress());
    root.deleteRecursively();
}

namespace
{
/// The item named `text` in `menu` or its submenus, or null.
const juce::PopupMenu::Item* findItem(const juce::PopupMenu& menu, const juce::String& text)
{
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

/// A menu as its popup draws it (for review: a real popup would open a window on the desktop).
juce::Image renderMenu(const juce::PopupMenu& menu, juce::LookAndFeel& lnf, float scale)
{
    const juce::PopupMenu::Options options;
    std::vector<std::pair<const juce::PopupMenu::Item*, int>> rows;
    int w = 0, h = 0;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        const auto& item = it.getItem();
        int iw = 0, ih = 0;
        const auto text = item.shortcutKeyDescription.isNotEmpty() ? item.text + "   " + item.shortcutKeyDescription : item.text;
        lnf.getIdealPopupMenuItemSizeWithOptions(text, item.isSeparator, options.getStandardItemHeight(), iw, ih, options);
        rows.emplace_back(&item, ih);
        w = std::max(w, iw);
        h += ih;
    }
    const int border = lnf.getPopupMenuBorderSizeWithOptions(options);
    juce::Image image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(w + 2 * border) * scale),
                      juce::roundToInt(static_cast<float>(h + 2 * border) * scale), true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));
    lnf.drawPopupMenuBackgroundWithOptions(g, w + 2 * border, h + 2 * border, options);
    int y = border;
    for (const auto& [item, rowHeight] : rows)
    {
        lnf.drawPopupMenuItemWithOptions(g, {border, y, w, rowHeight}, false, *item, options);
        y += rowHeight;
    }
    return image;
}
} // namespace

TEST_CASE("menu bar: every action in a menu once; items show their action's name, key, state (C10)", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    const auto& actions = f.shell->actions();
    auto& menus = f.shell->menus();
    CHECK(menus.getMenuBarNames() == juce::StringArray{"File", "Edit", "Track", "Clip", "Notes", "Audio",
                                                       "Song", "Module", "Transport", "View", "Options",
                                                       "Help"});

    // ADR 0040: every action is in the menu bar exactly once, and every item is an action (the
    // assistants' only with a server; the fixture has none).
    const auto placed = fd::model::menuActions(menus.menus());
    for (const auto& a : actions.all())
    {
        INFO(a.id);
        CHECK(std::count(placed.begin(), placed.end(), a.id) == 1);
    }
    for (const auto& id : placed)
    {
        INFO(id);
        CHECK((actions.find(id) != nullptr || id == "app.assistants"));
    }
    CHECK(fd::model::menuProblems(menus.menus()).empty());

    // Each menu builds, without stray separators where an action is missing.
    for (int i = 0; i < menus.getMenuBarNames().size(); ++i)
    {
        const auto menu = menus.menu(i);
        CHECK(menu.getNumItems() > 0);
        juce::PopupMenu::MenuItemIterator it(menu);
        REQUIRE(it.next());
        CHECK_FALSE(it.getItem().isSeparator);
    }

    // Items are their actions: the name and shortcut, dimmed while they can't run, ticked while on.
    const auto* undo = findItem(menus.menu(1), "Undo");
    REQUIRE(undo != nullptr);
    CHECK_FALSE(undo->isEnabled);
    CHECK(undo->shortcutKeyDescription.isNotEmpty());
    actions.perform("track.add");
    s.updateSnapshotNow();
    const auto* named = findItem(menus.menu(1), "Undo Add track"); // it names its step
    REQUIRE(named != nullptr);
    CHECK(named->isEnabled);
    CHECK(actions.tooltip("edit.undo").startsWith("Undo Add track ("));
    CHECK_FALSE(findItem(menus.menu(8), "Loop")->isTicked);
    actions.perform("transport.loop");
    CHECK(findItem(menus.menu(8), "Loop")->isTicked);
    actions.perform("transport.loop");

    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review: each menu and submenu
        for (int i = 0; i < menus.getMenuBarNames().size(); ++i)
        {
            const auto menu = menus.menu(i);
            const auto name = menus.getMenuBarNames()[i];
            dumpPng(dump, "menu-" + name + "@2x.png", renderMenu(menu, f.lnf, 2.0f));
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                if (it.getItem().subMenu != nullptr)
                    dumpPng(dump, "menu-" + name + "-" + it.getItem().text.removeCharacters(" ") + "@2x.png",
                            renderMenu(*it.getItem().subMenu, f.lnf, 2.0f));
        }

    // Choosing an item runs its action: File > Export… opens the export panel.
    CHECK(invoke(menus.menu(0), juce::String::fromUTF8("Export\xE2\x80\xA6")));
    CHECK(f.shell->exportPanel().isOpen());
    f.shell->exportPanel().close();
    // The lists the Record button's menu shows, in Transport too: one builder.
    CHECK(invoke(menus.menu(8), "2 bars", "Count-in length"));
    CHECK(s.countIn() == fd::model::CountIn::TwoBars);
    CHECK(invoke(menus.menu(8), "Each lap: add to the clip (overdub)", "MIDI loop recording"));
    CHECK(s.midiOverdub());
    // Back as the other tests expect them: the count-in is kept in the engine's settings file.
    s.setCountIn(fd::model::CountIn::Off);
    s.setMidiOverdub(false);

    // What only a key reached has a mouse route, dimmed until it can run.
    CHECK_FALSE(actions.isEnabled("clip.cancelBounces"));
    CHECK_FALSE(actions.isEnabled("midi.octaveUp"));
    CHECK_FALSE(actions.isEnabled("take.copyToTrack"));
    actions.perform("midi.computerKeyboard");
    REQUIRE(actions.isEnabled("midi.octaveUp"));
    CHECK(invoke(menus.menu(10), "Octave up", "Computer keyboard"));
    CHECK(f.shell->lastWarning().contains("C4"));
    // A key held when the keys stop reaching the shell (a text field elsewhere, another window,
    // the app in the background) ends its note: its release would never arrive.
    REQUIRE(f.shell->keyPressed(juce::KeyPress('a', {}, 'a')));
    CHECK(f.shell->computerKeyboardHolds());
    f.shell->focusLost(juce::Component::focusChangedByMouseClick);
    CHECK_FALSE(f.shell->computerKeyboardHolds());
    actions.perform("midi.computerKeyboard");
    // Without a window (tests), what needs one is dimmed.
    CHECK_FALSE(actions.isEnabled("app.quit"));
    CHECK_FALSE(actions.isEnabled("view.fullScreen"));
    CHECK_FALSE(actions.isEnabled("app.systemTitleBar"));

    // The keyboard: F10 opens File, Alt+letter its menu; Alt+R stays Arm.
    int opened = -1;
    f.shell->onOpenMenu = [&opened](int menu) { opened = menu; };
    CHECK(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::F10Key)));
    CHECK(opened == 0);
    CHECK(f.shell->keyPressed(juce::KeyPress('d', juce::ModifierKeys::altModifier, 0)));
    CHECK(opened == 7); // Module, after Song
    CHECK(f.shell->keyPressed(juce::KeyPress('n', juce::ModifierKeys::altModifier, 0)));
    CHECK(opened == 4); // Notes (ADR 0058)
    CHECK(f.shell->keyPressed(juce::KeyPress('a', juce::ModifierKeys::altModifier, 0)));
    CHECK(opened == 5); // Audio (ADR 0063)
    CHECK(f.shell->keyPressed(juce::KeyPress('g', juce::ModifierKeys::altModifier, 0)));
    CHECK(opened == 6); // Song (Alt+S solos)
    CHECK(f.shell->keyPressed(juce::KeyPress('p', juce::ModifierKeys::altModifier, 0)));
    CHECK(opened == 8);
    CHECK(f.shell->keyPressed(juce::KeyPress('h', juce::ModifierKeys::altModifier, 0)));
    CHECK(opened == 11);
    opened = -1;
    f.shell->keyPressed(juce::KeyPress('r', juce::ModifierKeys::altModifier, 0));
    CHECK(opened == -1);

    // The window's title puts the document first.
    CHECK(f.shell->projectName() == "Untitled");

    // Help > About Segno: the licence notice, inside the window, nothing touching.
    CHECK(invoke(menus.menu(11), "About Segno"));
    auto& about = f.shell->about();
    REQUIRE(about.isOpen());
    CHECK(about.paragraphs().joinIntoString(" ").contains("GNU Affero General Public License"));
    CHECK(about.paragraphs().joinIntoString(" ").contains("no warranty"));
    CHECK(f.shell->getLocalBounds().contains(about.getBounds()));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "about@2x.png", about.createComponentSnapshot(about.getLocalBounds(), true, 2.0f));
    CHECK(about.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(about.isOpen());
}

TEST_CASE("menu bar: Open recent lists the projects opened, kept between launches (C10)", "[integration]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-recent-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    fd::model::SettingsStore store(root.getFullPathName().toStdString()); // outlives the shell
    const auto settings = root.getChildFile("settings.json");
    ShellFixture f;
    auto& s = *f.session;
    f.shell->setSettingsStore(store);
    auto recent = [&f] {
        juce::StringArray names;
        const auto file = f.shell->menus().menu(0);
        if (const auto* item = findItem(file, "Open recent"); item != nullptr && item->subMenu != nullptr)
            for (juce::PopupMenu::MenuItemIterator it(*item->subMenu); it.next();)
                if (!it.getItem().isSeparator)
                    names.add(it.getItem().text);
        return names;
    };
    CHECK(recent() == juce::StringArray{"No recent projects", "Clear the list"});

    // Opening a project puts it first; a project gone from disk isn't listed.
    const auto song = root.getChildFile("Song").getChildFile("Song.segno");
    const auto other = root.getChildFile("Other").getChildFile("Other.segno");
    REQUIRE(s.saveAs(other));
    REQUIRE(s.saveAs(song));
    REQUIRE(s.open(other));
    REQUIRE(s.open(song));
    CHECK(recent() == juce::StringArray{"Song", "Other", "Clear the list"});
    CHECK(settings.loadFileAsString().contains("Song.segno")); // kept for the next launch
    other.getParentDirectory().deleteRecursively();
    CHECK(recent() == juce::StringArray{"Song", "Clear the list"});
    const auto* item = findItem(f.shell->menus().menu(0), "Song");
    REQUIRE(item != nullptr);
    CHECK(item->shortcutKeyDescription.endsWith("Song")); // where it is

    // A new shell reads the list back; Clear the list empties it.
    fd::model::SettingsStore storeAgain(root.getFullPathName().toStdString());
    fd::app::Shell again(s);
    again.setSettingsStore(storeAgain);
    CHECK(again.settings().recent.paths.front() == song.getFullPathName().toStdString());
    CHECK(invoke(again.menus().menu(0), "Clear the list", "Open recent"));
    CHECK(again.settings().recent.paths.empty());
    root.deleteRecursively();
}

TEST_CASE("title bar: brand, menus, name and window buttons never touch; the caption moves the window (C10)",
          "[integration]")
{
    ShellFixture f;
    fd::app::WindowContent content(*f.session, nullptr, true);
    auto* bar = content.titleBar();
    REQUIRE(bar != nullptr);
    const auto& sh = fd::ui::Theme::current().shell;
    using K = juce::Component::WindowControlKind;
    for (int width : {2560, 1600, 1280, 1024}) // 1024: the window's minimum
    {
        INFO(width);
        content.setSize(width, 900);
        CHECK(bar->getHeight() == sh.titleBarHeight);
        CHECK(content.shell().getY() == sh.titleBarHeight);
        // Every part inside the bar, none touching another; the menus whole at every width.
        const auto menusBounds = bar->menuBar().getBounds();
        const auto buttons = bar->windowButtons().getBounds();
        CHECK(buttons.getRight() == width);
        CHECK(buttons.getWidth() == fd::ui::WindowButtons::widthFor());
        std::vector<juce::Rectangle<int>> parts{bar->brandArea(), menusBounds, buttons};
        if (!bar->titleArea().isEmpty())
            parts.push_back(bar->titleArea());
        for (size_t a = 0; a < parts.size(); ++a)
        {
            CHECK(bar->getLocalBounds().contains(parts[a]));
            for (size_t b = a + 1; b < parts.size(); ++b)
                CHECK_FALSE(parts[a].intersects(parts[b]));
        }
        int menusWidth = 0;
        const auto names = f.shell->menus().getMenuBarNames();
        for (int i = 0; i < names.size(); ++i)
            menusWidth += bar->getLookAndFeel().getMenuBarItemWidth(bar->menuBar(), i, names[i]);
        CHECK(menusBounds.getWidth() == menusWidth);
        CHECK_FALSE(bar->titleArea().isEmpty()); // the name fits from 1024 up
        // What the system asks (Windows): the caption, the buttons, the menus and the content.
        const float mid = static_cast<float>(sh.titleBarHeight) * 0.5f;
        CHECK(content.controlAt({static_cast<float>(bar->brandArea().getCentreX()), mid}) == K::caption);
        CHECK(content.controlAt({static_cast<float>(bar->titleArea().getCentreX()), mid}) == K::caption);
        CHECK(content.controlAt({static_cast<float>(menusBounds.getX() + 4), mid}) == K::client);
        CHECK(content.controlAt({static_cast<float>(width - 1), mid}) == K::close);
        CHECK(content.controlAt({static_cast<float>(width - sh.windowButtonWidth - 1), mid}) == K::maximise);
        CHECK(content.controlAt({static_cast<float>(width - 2 * sh.windowButtonWidth - 1), mid}) == K::minimise);
        CHECK(content.controlAt({static_cast<float>(width / 2), 200.0f}) == K::client);
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
        {
            dumpPng(dump, "titlebar-" + juce::String(width) + "@2x.png",
                    bar->createComponentSnapshot(bar->getLocalBounds(), true, 2.0f));
            dumpPng(dump, "window-" + juce::String(width) + ".png", content.createComponentSnapshot(content.getLocalBounds()));
        }
    }
    // The buttons' clicks go to the window; dragging the caption hands the move to it (Linux).
    int minimised = 0, maximised = 0, closed = 0, moves = 0, doubles = 0;
    bar->onMinimise = [&] { ++minimised; };
    bar->onMaximise = [&] { ++maximised; };
    bar->onClose = [&] { ++closed; };
    bar->onCaptionDrag = [&](const juce::MouseEvent&) { ++moves; };
    bar->onCaptionDoubleClick = [&] { ++doubles; };
    auto& wb = bar->windowButtons();
    for (const auto part : {fd::ui::WindowButtons::Part::Minimise, fd::ui::WindowButtons::Part::Maximise,
                            fd::ui::WindowButtons::Part::Close})
        click(wb, wb.area(part).getCentre().toFloat());
    CHECK((minimised == 1 && maximised == 1 && closed == 1));
    const auto caption = bar->titleArea().getCentre().toFloat();
    drag(*bar, caption, caption + juce::Point<float>(40.0f, 10.0f));
    CHECK(moves == 1); // once per drag
    click(*bar, caption, 2);
    CHECK(doubles == 1);
    // Maximised: Restore; the unsaved dot after the name.
    content.setWindowState(true, false);
    CHECK(wb.isMaximised());
    f.session->addTrack();
    bar->setTitle("A long project name that has to give way to the menus and buttons", true);
    content.setSize(1024, 900);
    CHECK(bar->titleArea().getRight() < bar->windowButtons().getX());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dump, "titlebar-unsaved-1024@2x.png", bar->createComponentSnapshot(bar->getLocalBounds(), true, 2.0f));
        bar->setMnemonicsShown(true);
        dumpPng(dump, "titlebar-mnemonics@2x.png", bar->createComponentSnapshot(bar->getLocalBounds().withWidth(600), true, 2.0f));
        bar->setMnemonicsShown(false);
    }
    // Full screen: no window buttons. The system's title bar: the menus alone, every point the
    // content's.
    content.setWindowState(false, true);
    CHECK_FALSE(bar->windowButtons().isVisible());
    content.setWindowState(false, false);
    content.setOwnsWindow(false);
    CHECK_FALSE(bar->windowButtons().isVisible());
    CHECK(bar->titleArea().isEmpty());
    CHECK(content.controlAt({static_cast<float>(bar->brandArea().getCentreX()), 10.0f}) == K::client);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "titlebar-system@2x.png", bar->createComponentSnapshot(bar->getLocalBounds(), true, 2.0f));
}

TEST_CASE("title bar: only its own option reaches the window; other settings leave the window alone (C10 fix)",
          "[integration]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-titlebar-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    fd::model::SettingsStore store(root.getFullPathName().toStdString()); // outlives the shell
    int given = 0;
    ShellFixture f;
    f.shell->window.setSystemTitleBar = [&given](bool) { ++given; };
    f.shell->setSettingsStore(store);
    CHECK(given == 1); // the window is told at once
    // A browser category opened, a favourite, the preview: settings, but not the window's. Each
    // used to lay the window out again, which showed a maximised window as not maximised.
    store.change([](fd::model::AppSettings& s) { s.browser.open.push_back("cat:instruments"); });
    store.change([](fd::model::AppSettings& s) { s.browser.favourites.push_back("module:fd:eq"); });
    store.change([](fd::model::AppSettings& s) { s.browser.preview = !s.browser.preview; });
    CHECK(given == 1);
    f.shell->actions().perform("app.systemTitleBar"); // Options > Use the system title bar
    CHECK(f.shell->settings().display.systemTitleBar);
    CHECK(given == 2);
    root.deleteRecursively();
}

TEST_CASE("inspector: a clip's fade curves and the crossfades at its ends, set by mouse (C10 review)", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    // Two clips that meet at 2 s, each with audio past its edges, crossfaded; the first faded in.
    fd::testkit::AudioBuffer tone(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < tone.numSamples(); ++i)
            tone.at(c, i) = 0.5f * std::sin(static_cast<float>(i) * 0.02f);
    std::vector<std::uint64_t> ids;
    for (const auto& [name, start] : {std::pair{"a.wav", 0.0}, std::pair{"b.wav", 2.0}})
    {
        const auto file = root.getChildFile(name);
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), tone, 48000.0));
        const auto id = s.insertAudioFile(t0, file, start);
        s.applyClipEdits({{.clip = id, .track = t0, .start = start, .length = 2.0, .offset = 1.0}});
        ids.push_back(id);
    }
    REQUIRE(s.setCrossfade(ids[0], fd::model::Crossfade{.length = 0.2, .from = -0.1}));
    s.setFades({ids[0]}, fd::model::Fade{0.5, 0.4f}, std::nullopt);
    s.updateSnapshotNow();
    auto clipOf = [&](std::uint64_t id) {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), id)->first;
    };
    auto& inspector = f.shell->inspector();
    auto select = [&](std::uint64_t clip) {
        f.shell->arrangement().setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    };
    auto shown = [&inspector]<typename T>(T*) {
        std::vector<T*> out;
        for (auto* c : findAll<T>(inspector))
            if (c->isVisible())
                out.push_back(c);
        return out;
    };
    auto crossfadeBoxes = [&] { // the crossfade's shape and position (not the track's inputs)
        std::vector<juce::ComboBox*> out;
        for (auto* box : shown(static_cast<juce::ComboBox*>(nullptr)))
            if (box->getItemText(1) == "Equal power" || box->getItemText(0) == "Before the join")
                out.push_back(box);
        return out;
    };
    // Clips aren't Clip > Fades' any more: the menu bar has no Fades.
    CHECK(findItem(f.shell->menus().menu(3), "Fades") == nullptr);

    // The second clip: the crossfade meeting its start, its shape and where it sits.
    select(ids[1]);
    auto boxes = crossfadeBoxes();
    REQUIRE(boxes.size() == 2);
    CHECK(boxes[0]->getText() == "Equal power");
    CHECK(boxes[1]->getText() == "Centred on the join");
    boxes[0]->setSelectedId(3, juce::sendNotificationSync); // equal gain
    CHECK(clipOf(ids[0]).crossfadeOut->shape == fd::model::CrossfadeShape::EqualGain);
    CHECK(s.undoDescription() == "Crossfade shape");
    boxes[1]->setSelectedId(3, juce::sendNotificationSync); // after the join
    CHECK(clipOf(ids[0]).crossfadeOut->from == Catch::Approx(0.0));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "inspector-crossfade@2x.png", inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));

    // The first: its fade-in's curve, straightened by typing 0; then its crossfade out removed.
    select(ids[0]);
    const auto fields = shown(static_cast<fd::ui::NumberField*>(nullptr));
    // Fade in, fade out, their curves, then AUDIO's gain, transpose, fine and sensitivity.
    REQUIRE(fields.size() == 8);
    CHECK(fields[2]->getValue() == Catch::Approx(40.0));
    CHECK_FALSE(fields[3]->isEnabled()); // no fade-out: no curve to set
    fields[2]->setValue(0.0, juce::sendNotificationSync);
    CHECK(clipOf(ids[0]).fadeIn.bend == 0.0f);
    CHECK(clipOf(ids[0]).fadeIn.length == Catch::Approx(0.5));
    // Every control on the row grid, none touching another.
    {
        std::vector<juce::Rectangle<int>> placed;
        for (auto* c : inspector.content().getChildren())
            if (c->isVisible())
            {
                for (const auto& other : placed)
                    CHECK_FALSE(other.intersects(c->getBounds()));
                placed.push_back(c->getBounds());
            }
    }
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "inspector-fades@2x.png", inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));
    boxes = crossfadeBoxes();
    REQUIRE(boxes.size() == 2); // the crossfade out
    boxes[0]->setSelectedId(1, juce::sendNotificationSync); // None
    CHECK_FALSE(clipOf(ids[0]).crossfadeOut.has_value());
    CHECK(s.undoDescription() == "Remove crossfade");
    root.deleteRecursively();
}

TEST_CASE("inspector: a clip's loop: on, its start and length dragged or typed; dimmed, saying why, where it can't (C10)",
          "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-shell-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    s.setUnsavedRoot(root.getChildFile("Unsaved"));
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    fd::testkit::AudioBuffer tone(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < tone.numSamples(); ++i)
            tone.at(c, i) = 0.5f * std::sin(static_cast<float>(i) * 0.02f);
    const auto file = root.getChildFile("tone.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), tone, 48000.0));
    const auto id = s.insertAudioFile(t0, file, 0.0);
    s.applyClipEdits({{.clip = id, .track = t0, .start = 0.0, .length = 2.0, .offset = 0.0}});
    s.updateSnapshotNow();
    auto clip = [&] {
        s.updateSnapshotNow();
        return *fd::model::findClip(*s.snapshot(), id)->first;
    };
    auto& inspector = f.shell->inspector();
    auto select = [&] {
        f.shell->arrangement().setSelection(fd::model::withClips(*s.snapshot(), {id}, t0, t0));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    };
    auto loopToggle = [&]() -> fd::ui::Toggle*
    {
        auto& t = inspector.clipLoop();
        return t.isVisible() ? &t : nullptr;
    };
    auto field = [&](const char* tip) -> fd::ui::NumberField* {
        for (auto* n : findAll<fd::ui::NumberField>(inspector))
            if (n->isVisible() && n->getTooltip().startsWith(tip))
                return n;
        return nullptr;
    };
    select();
    REQUIRE(loopToggle() != nullptr);
    CHECK_FALSE(loopToggle()->getToggleState());
    CHECK(field("Where the loop starts") == nullptr); // no fields while it doesn't loop

    // On: it loops what it shows, and the fields show its start and length in the content.
    loopToggle()->triggerClick(); // a click arrives on the message thread
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(s.undoDescription() == "Loop clips");
    CHECK(clip().loop == fd::model::ClipLoop{0.0, 2.0});
    select();
    auto* start = field("Where the loop starts");
    auto* length = field("The loop's length");
    REQUIRE(start != nullptr);
    REQUIRE(length != nullptr);
    CHECK(loopToggle()->getToggleState());
    CHECK(start->getValue() == Catch::Approx(0.0));
    CHECK(length->getValue() == Catch::Approx(4.0)); // a bar at 120 BPM

    // Typed: a length, a start, and seconds for audio.
    REQUIRE(length->valueFromText("1 bar 2 beats"));
    CHECK(*length->valueFromText("1 bar 2 beats") == Catch::Approx(6.0));
    CHECK(*length->valueFromText("0.75 s") == Catch::Approx(1.5));
    length->setValue(2.0, juce::sendNotificationSync);
    CHECK(s.undoDescription() == "Loop");
    CHECK(clip().loop.length == Catch::Approx(1.0));
    select();
    start = field("Where the loop starts");
    REQUIRE(start != nullptr);
    start->setValue(*start->valueFromText("1.2.1"), juce::sendNotificationSync); // one beat in
    CHECK(clip().loop.start == Catch::Approx(0.5));
    CHECK(clip().length == Catch::Approx(2.0)); // the clip stays as long as it was
    select();
    // Every control on the row grid, none touching another.
    {
        std::vector<juce::Rectangle<int>> placed;
        for (auto* c : inspector.content().getChildren())
            if (c->isVisible())
            {
                for (const auto& other : placed)
                    CHECK_FALSE(other.intersects(c->getBounds()));
                placed.push_back(c->getBounds());
            }
    }
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "inspector-loop@2x.png", inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));

    // Off again; a clip that follows the tempo (warped, ADR 0063) loops as any other.
    loopToggle()->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK_FALSE(clip().loop.on());
    REQUIRE(s.setWarp({id}, {.on = true}));
    s.updateSnapshotNow();
    select();
    REQUIRE(loopToggle() != nullptr);
    CHECK(loopToggle()->isEnabled());
    root.deleteRecursively();
}

TEST_CASE("inspector: sections taller than the panel scroll, so the last one is always reachable", "[integration]")
{
    // The product owner's case: a tall clip view leaves the inspector little room.
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto clip = s.createMidiClip(t0, 0.0, 2.0);
    s.updateSnapshotNow();
    f.shell->arrangement().setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    auto& inspector = f.shell->inspector();
    auto& content = inspector.content();
    const int full = content.getHeight();
    REQUIRE(full > 300); // CLIP, TRACK, INPUT, MIX, SENDS

    inspector.setSize(inspector.getWidth(), 220);
    auto* viewport = content.findParentComponentOfClass<juce::Viewport>();
    REQUIRE(viewport != nullptr);
    CHECK(viewport->getBottom() <= inspector.getHeight());
    CHECK(content.getHeight() == full); // every row still there, below
    CHECK(viewport->getVerticalScrollBar().isVisible());
    // Scrolled to the end, the last section is in view.
    viewport->setViewPosition(0, content.getHeight());
    const auto shown = viewport->getViewArea();
    CHECK(shown.getBottom() == content.getHeight());
    juce::Component* lowest = nullptr;
    for (auto* c : content.getChildren())
        if (c->isVisible() && (lowest == nullptr || c->getBottom() > lowest->getBottom()))
            lowest = c;
    REQUIRE(lowest != nullptr);
    CHECK(shown.contains(lowest->getBounds()));
    // Nothing runs under the scroll bar.
    for (auto* c : content.getChildren())
        if (c->isVisible())
            CHECK(c->getRight() <= viewport->getMaximumVisibleWidth());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "inspector-scrolled@2x.png", inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));

    // Tall enough again: no scroll bar.
    inspector.setSize(inspector.getWidth(), full + 200);
    CHECK_FALSE(viewport->getVerticalScrollBar().isVisible());
}

TEST_CASE("snap and quantise where they're used; the take's commands in its inspector (C10 review)", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    const auto& actions = f.shell->actions();
    auto& view = f.shell->arrangement();
    // Snap in the corner above the track headers: the same as Ctrl+4, either way round.
    auto& snap = view.snapToggle();
    REQUIRE(snap.isVisible());
    CHECK(snap.getBounds().getBottom() <= fd::app::ArrangementView::rulerHeight());
    CHECK(snap.getRight() <= fd::app::ArrangementView::headerWidth());
    CHECK(snap.getToggleState() == view.isSnapEnabled());
    snap.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK_FALSE(view.isSnapEnabled());
    CHECK_FALSE(actions.isToggled("view.snap"));
    actions.perform("view.snap");
    CHECK(snap.getToggleState());
    CHECK(snap.getTooltip().contains("Snap to grid"));
    // A view option, like the mixer's Modules, Sends and Narrow (C11 review): the ruler's row
    // high, as wide as it needs.
    CHECK(snap.getHeight() == fd::ui::Theme::current().shell.rulerHeight);
    CHECK(snap.getWidth() == snap.preferredWidth());
    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review, zoomed in
    {
        snap.setState(juce::Button::buttonNormal); // triggerClick's flash never ends without a mouse
        const juce::Rectangle<int> corner(0, 0, fd::app::ArrangementView::headerWidth() + 120,
                                          fd::app::ArrangementView::rulerHeight() + 40);
        dumpPng(dump, "snap-on@2x.png", view.createComponentSnapshot(corner, true, 2.0f));
        actions.perform("view.snap");
        dumpPng(dump, "snap-off@2x.png", view.createComponentSnapshot(corner, true, 2.0f));
        actions.perform("view.snap");
    }

    // Quantise in the clip view's header, while it shows a clip.
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto clip = s.createMidiClip(t0, 0.0, 4.0);
    s.setNotes(clip, {{.pitch = 60, .start = 0.1, .length = 0.5, .velocity = 0.8f}});
    s.updateSnapshotNow();
    view.setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
    auto& detail = f.shell->detail();
    if (detail.mode() != fd::app::DetailPanel::Mode::Clip) // a MIDI clip opens the clip view by itself
        actions.perform("view.detailMode");
    f.shell->finishAnimations();
    detail.frame(0.0);
    fd::ui::Toggle* quantise = nullptr;
    for (auto* t : findAll<fd::ui::Toggle>(detail))
        if (t->getButtonText() == "Quantise" && t->isVisible()) // the notes' (the audio's shows for audio)
            quantise = t;
    REQUIRE(quantise != nullptr);
    CHECK(quantise->getTooltip().contains("Ctrl+U"));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dump, "clip-header@2x.png", detail.createComponentSnapshot(detail.getLocalBounds().withHeight(40), true, 2.0f));
        dumpPng(dump, "snap-corner@2x.png",
                view.createComponentSnapshot(view.getLocalBounds().withHeight(80).withWidth(300), true, 2.0f));
    }
    actions.perform("view.detailMode");
    detail.frame(0.0);
    CHECK_FALSE(quantise->isVisible()); // the modules: no Quantise

    // A selected take: Copy, Move and Keep only beside "Use this take"; they run its actions.
    // (Takes need a recording; the TAKE section's buttons are wired to the take actions.)
    std::vector<juce::String> takeButtons;
    for (auto* t : findAll<fd::ui::Toggle>(f.shell->inspector()))
        takeButtons.push_back(t->getButtonText());
    for (const auto* name : {"Copy", "Move", "Keep only this take", "Use this take"})
        CHECK(std::find(takeButtons.begin(), takeButtons.end(), juce::String(name)) != takeButtons.end());
    CHECK_FALSE(actions.isEnabled("take.copyToTrack")); // no take selected: they act on selected takes only
    CHECK(actions.find("take.delete") == nullptr);       // Delete deletes selected takes
}

namespace
{
/// The arrangement row of a channel (not a lane).
int rowOf(fd::app::ArrangementView& view, std::uint64_t channel)
{
    const auto& rows = view.rows();
    for (size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].isLane() && rows[i].channel == channel)
            return static_cast<int>(i);
    return -1;
}
} // namespace

TEST_CASE("tracks: renamed where their names are, deleted and duplicated from their header, dragged to a new place (C11)",
          "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    const auto& actions = f.shell->actions();
    f.shell->setSize(1600, 900);
    f.shell->finishAnimations();
    auto& view = f.shell->arrangement();
    auto ids = [&s] {
        s.updateSnapshotNow();
        std::vector<std::uint64_t> out;
        for (const auto& t : s.snapshot()->tracks)
            out.push_back(t.id);
        return out;
    };
    auto name = [&s](std::uint64_t id) {
        s.updateSnapshotNow();
        return s.snapshot()->channel(id)->name;
    };
    auto sync = [&] {
        s.updateSnapshotNow();
        view.setSnapshot(s.snapshot());
    };
    auto tracks = ids();
    REQUIRE(tracks.size() >= 3);
    const auto t0 = tracks[0], t1 = tracks[1];

    // A double-click on a header's name: the field over it; Return keeps the name.
    sync();
    auto area = view.nameArea(rowOf(view, t0));
    REQUIRE(area.has_value());
    click(view, area->getCentre().toFloat(), 2);
    auto& editor = view.nameEditor();
    REQUIRE(editor.isEditing());
    CHECK(editor.field().getText() == juce::String::fromUTF8(name(t0).c_str()));
    CHECK(view.getLocalBounds().contains(editor.field().getBounds()));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "rename-track@2x.png", view.createComponentSnapshot(area->expanded(60, 30), true, 2.0f));
    editor.field().setText("Bass", false);
    editor.field().onReturnKey();
    CHECK_FALSE(editor.isEditing());
    CHECK(name(t0) == "Bass");
    CHECK(s.undoDescription() == "Rename track");
    // Escape restores; an empty name changes nothing.
    sync();
    REQUIRE(view.renameChannel(t0));
    editor.field().setText("Nope", false);
    editor.field().onEscapeKey();
    CHECK(name(t0) == "Bass");
    REQUIRE(view.renameChannel(t0));
    editor.field().setText("   ", false);
    editor.field().onReturnKey();
    CHECK(name(t0) == "Bass");

    // F2 on a selected clip: its title; every selected clip takes the name.
    const auto clip = s.createMidiClip(t1, 2.0, 2.0);
    sync();
    view.setSelection(fd::model::withClips(*s.snapshot(), {clip}, t1, t1));
    REQUIRE(actions.perform("edit.rename"));
    REQUIRE(editor.isEditing());
    editor.field().setText("Riff", false);
    editor.field().onReturnKey();
    s.updateSnapshotNow();
    CHECK(fd::model::findClip(*s.snapshot(), clip)->first->name == "Riff");
    // A double-click on a clip's title renames it too (its body still opens it).
    sync();
    const auto title = view.clipTitleArea(clip);
    REQUIRE(title.has_value());
    click(view, title->getCentre().toFloat(), 2);
    CHECK(editor.isEditing());
    editor.cancel();

    // The inspector's Name row is a field; its Colour row a swatch.
    sync();
    view.selectTrack(t1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    juce::TextEditor* nameField = nullptr;
    for (auto* field : findAll<juce::TextEditor>(f.shell->inspector()))
        if (field->isVisible() && field->getText() == juce::String::fromUTF8(name(t1).c_str()))
            nameField = field;
    REQUIRE(nameField != nullptr);
    nameField->setText("Keys", false);
    nameField->onReturnKey();
    CHECK(name(t1) == "Keys");
    auto swatches = findAll<fd::ui::ColourSwatch>(f.shell->inspector());
    const auto swatch = std::find_if(swatches.begin(), swatches.end(), [](auto* sw) { return sw->isVisible(); });
    REQUIRE(swatch != swatches.end());
    (*swatch)->onChosen(9);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->channel(t1)->colour == 9);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "inspector-track@2x.png",
                f.shell->inspector().createComponentSnapshot(f.shell->inspector().getLocalBounds().withHeight(220), true, 2.0f));

    // Delete and Ctrl+D act on a track whose header was clicked last.
    sync();
    area = view.nameArea(rowOf(view, t1));
    click(view, area->getCentre().toFloat());
    REQUIRE(view.headerActive());
    REQUIRE(actions.handleKey(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
    tracks = ids();
    CHECK(name(tracks[2]) == "Keys 2"); // right after it
    CHECK(view.selection().channel == tracks[2]);
    sync();
    click(view, view.nameArea(rowOf(view, tracks[2]))->getCentre().toFloat());
    REQUIRE(actions.handleKey(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK(ids().size() == tracks.size() - 1);
    CHECK(f.shell->lastWarning() == "Deleted Keys 2");
    CHECK(view.selection().channel == t1); // the one above
    s.undo();
    CHECK(ids().size() == tracks.size());
    s.undo(); // the duplicate
    // A click on a clip: Delete deletes the clip, not its track.
    sync();
    view.setSelection(fd::model::withClips(*s.snapshot(), {clip}, t1, t1));
    CHECK_FALSE(view.headerActive());

    // Dragging a header: a line where it lands, then it moves there (one undo step).
    sync();
    tracks = ids();
    const auto from = view.nameArea(rowOf(view, t0))->getCentre().toFloat();
    const auto below = view.nameArea(rowOf(view, tracks[2]))->getBottom() + 30; // past the third track's middle
    view.mouseDown(mouseEvent(view, from, from, false));
    const juce::Point<float> to(from.x, static_cast<float>(below));
    view.mouseDrag(mouseEvent(view, to, from, true));
    REQUIRE(view.rowDrop().has_value());
    CHECK(view.rowDrop()->moves);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "drag-track@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(320).withWidth(700), true, 2.0f));
    view.mouseUp(mouseEvent(view, to, from, true));
    CHECK(ids()[0] == t1);
    CHECK(s.undoDescription() == "Move track");
    // Escape cancels a drag.
    sync();
    const auto again = view.nameArea(rowOf(view, t1))->getCentre().toFloat();
    view.mouseDown(mouseEvent(view, again, again, false));
    view.mouseDrag(mouseEvent(view, again + juce::Point<float>(0.0f, 200.0f), again, true));
    REQUIRE(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(view.rowDrop().has_value());
    view.mouseUp(mouseEvent(view, again + juce::Point<float>(0.0f, 200.0f), again, true));
    CHECK(ids()[0] == t1);

    // Every row moves alone (ADR 0042): a track dropped between two lanes lands there, and the
    // lanes stay where they were.
    const auto first = s.addAutomationLaneAfter(t1, fd::engine::Session::ParamTarget{t1, "volume"});
    const auto second = s.addAutomationLaneAfter(first, fd::engine::Session::ParamTarget{t1, "pan"});
    sync();
    const auto rowsNow = [&view] {
        std::vector<std::uint64_t> out;
        for (const auto& row : view.rows())
            if (!row.holdsClips() && !row.isLane())
                break;
            else
                out.push_back(row.isLane() ? row.lane : row.channel);
        return out;
    };
    REQUIRE(rowsNow()[0] == t1);
    REQUIRE(rowsNow()[1] == first);
    REQUIRE(rowsNow()[2] == second);
    const auto last = ids().back();
    const auto grab = view.nameArea(rowOf(view, last))->getCentre().toFloat();
    const auto between = juce::Point<float>(grab.x, static_cast<float>(view.nameArea(fd::model::rowOfLane(view.rows(), second))->getY() - 4));
    view.mouseDown(mouseEvent(view, grab, grab, false));
    view.mouseDrag(mouseEvent(view, between, grab, true));
    REQUIRE(view.rowDrop().has_value());
    CHECK(view.rowDrop()->before == second);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "drag-between-lanes@2x.png", view.createComponentSnapshot(view.getLocalBounds().withHeight(320).withWidth(700), true, 2.0f));
    view.mouseUp(mouseEvent(view, between, grab, true));
    sync();
    CHECK(rowsNow()[0] == t1);
    CHECK(rowsNow()[1] == first);
    CHECK(rowsNow()[2] == last);
    CHECK(rowsNow()[3] == second);
    s.undo();
    sync();

    // The mixer: a double-click on a strip's name renames it; dragging the name moves it.
    actions.perform("view.mixer");
    f.shell->finishAnimations();
    auto& mixer = f.shell->mixer();
    mixer.setSnapshot(s.snapshot());
    REQUIRE(mixer.renameChannel(t0));
    REQUIRE(mixer.nameEditor().isEditing());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "rename-strip@2x.png", mixer.createComponentSnapshot(mixer.getLocalBounds().withHeight(120).withWidth(400), true, 2.0f));
    mixer.nameEditor().field().setText("Low", false);
    mixer.nameEditor().field().onReturnKey();
    CHECK(name(t0) == "Low");
    actions.perform("view.mixer");
}

TEST_CASE("clipboard: Ctrl+C and Ctrl+V paste clips at the playhead on the selected track; Ctrl+X cuts (C11)", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    const auto& actions = f.shell->actions();
    f.shell->setSize(1600, 900);
    auto& view = f.shell->arrangement();
    s.updateSnapshotNow();
    const auto t0 = s.snapshot()->tracks[0].id, t2 = s.snapshot()->tracks[2].id;
    const auto clip = s.createMidiClip(t0, 0.0, 2.0);
    s.setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 1.0}, {.pitch = 64, .start = 1.0, .length = 1.0}});
    s.updateSnapshotNow();
    view.setSnapshot(s.snapshot());
    CHECK_FALSE(actions.isEnabled("edit.paste"));
    view.setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
    REQUIRE(actions.handleKey(juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0)));
    // The playhead at 8 s on the third track.
    s.setPosition(8.0);
    view.selectTrack(t2);
    REQUIRE(actions.handleKey(juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0)));
    s.updateSnapshotNow();
    const auto* third = s.snapshot()->channel(t2);
    REQUIRE(third->clips.size() == 1);
    CHECK(third->clips[0].start == Catch::Approx(8.0));
    CHECK(s.undoDescription() == "Paste");
    CHECK(view.selection().clips.contains(third->clips[0].id)); // the copy, selected

    // Ctrl+X: gone, and it still pastes.
    view.setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
    REQUIRE(actions.handleKey(juce::KeyPress('x', juce::ModifierKeys::commandModifier, 0)));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->channel(t0)->clips.empty());
    CHECK(s.undoDescription() == "Cut");
    s.setPosition(4.0);
    view.selectTrack(t0);
    REQUIRE(actions.perform("edit.paste"));
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->channel(t0)->clips.size() == 1);
    CHECK(s.snapshot()->channel(t0)->clips[0].notes.size() == 2);

    // The clip's menu has them, and its colour.
    juce::PopupMenu menu;
    view.onClipMenu(menu);
    CHECK(findItem(menu, "Copy") != nullptr);
    CHECK(findItem(menu, "Colour") != nullptr);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dump, "clip-menu@2x.png", renderMenu(menu, f.lnf, 2.0f));
        fd::ui::ColourGrid grid(5, [](int) {});
        int w = 0, h = 0;
        grid.getIdealSize(w, h);
        grid.setSize(w, h);
        dumpPng(dump, "colour-grid@2x.png", grid.createComponentSnapshot(grid.getLocalBounds(), true, 2.0f));
    }
}

namespace
{
/// The app's window on the desktop (Main.cpp): our title bar over the shell, drawn by OpenGL.
/// Keyboard focus only exists on a showing window.
struct AppWindow
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    tracktion::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<fd::engine::Session> session;
    struct Window final : juce::DocumentWindow
    {
        Window() : juce::DocumentWindow("Segno", juce::Colours::black, 0, true) {}
        void closeButtonPressed() override {}
    };
    std::unique_ptr<Window> window;
    fd::app::WindowContent* content = nullptr;
    juce::OpenGLContext gl;

    AppWindow()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            return;
        window = std::make_unique<Window>();
        window->setUsingNativeTitleBar(false);
        window->setTitleBarHeight(0);
        content = new fd::app::WindowContent(*session, nullptr, true);
        window->setContentOwned(content, false);
        window->setBounds(40, 40, 1800, 900); // the inspector open
        gl.setComponentPaintingEnabled(true);
        gl.attachTo(*content);
        window->setVisible(true);
        window->toFront(true);
        fd::ui::takeFocus(content->shell());
        pump();
    }

    ~AppWindow()
    {
        gl.detach();
        window.reset();
        session.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    [[nodiscard]] bool shows() const noexcept { return window != nullptr; }
    [[nodiscard]] fd::app::Shell& shell() const noexcept { return content->shell(); }
    static void pump(int ms = 150) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

    /// A press and release at `p` in `c`, through the window as the system sends them.
    void click(juce::Component& c, juce::Point<float> p, juce::ModifierKeys button = juce::ModifierKeys::leftButtonModifier) const
    {
        auto* peer = window->getPeer();
        const auto at = window->getLocalPoint(&c, p);
        for (const auto mods : {button, juce::ModifierKeys()})
            peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, at, mods,
                                   juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                   juce::Time::currentTimeMillis());
    }

    /// The menu showing (its window), or null.
    [[nodiscard]] juce::Component* menu() const
    {
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
            if (auto* c = juce::Desktop::getInstance().getComponent(i); c != window.get() && c->isShowing())
                return c;
        return nullptr;
    }

    /// The items of the menu showing, by their text, in order.
    [[nodiscard]] std::vector<std::pair<juce::String, juce::Component*>> menuItems() const
    {
        std::vector<std::pair<juce::String, juce::Component*>> out;
        auto* shown = menu();
        std::function<void(juce::Component&)> find = [&](juce::Component& c) {
            if (auto* h = c.getAccessibilityHandler(); h != nullptr && &c != shown && h->getTitle().isNotEmpty())
                out.emplace_back(h->getTitle(), &c);
            for (auto* child : c.getChildren())
                find(*child);
        };
        if (shown != nullptr)
            find(*shown);
        return out;
    }

    /// Chooses the item whose text starts with `text` in the menu showing (as a click on it does).
    [[nodiscard]] bool choose(const juce::String& text) const
    {
        for (const auto& [title, item] : menuItems())
            if (title.startsWith(text))
            {
                item->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
                return true;
            }
        return false;
    }
};
} // namespace

// One test case, so no two windows take the focus from each other (CTest runs cases in parallel).
TEST_CASE("a showing window: renaming takes the focus by every route, a press elsewhere keeps what was typed "
          "(C11 review); Maximise and Restore (C10 fix)",
          "[integration][window]")
{
    AppWindow app;
    if (!app.shows())
        SKIP("no display to show a window on");
    auto& s = *app.session;
    auto& shell = app.shell();
    auto& view = shell.arrangement();
    s.updateSnapshotNow();

    SECTION("renaming takes the keyboard focus, by every route")
    {
        const auto track = s.snapshot()->tracks[0].id;
        REQUIRE(s.addDevice(track, "fd:subtractive-synth") != 0); // selected, it takes live MIDI (as lanes' tracks do)
        const auto lane = s.addAutomationLane(fd::engine::Session::ParamTarget{track, "volume"});
        const auto ret = s.addReturnTrack();
        s.updateSnapshotNow();
        view.setSnapshot(s.snapshot());
        AppWindow::pump();
        auto& editor = view.nameEditor();
        auto* peer = app.window->getPeer();
        // In the field, the whole name selected to type over.
        auto typing = [&editor] {
            return editor.isEditing() && editor.field().hasKeyboardFocus(false) &&
                   editor.field().getHighlightedRegion().getLength() == editor.field().getTotalNumChars();
        };
        auto done = [&editor] {
            editor.cancel();
            AppWindow::pump();
        };

        struct Row
        {
            const char* what;
            std::function<int()> row;
        };
        for (const auto& [what, row] : {Row{"track", [&] { return rowOf(view, track); }}, Row{"return", [&] { return rowOf(view, ret); }},
                                        Row{"lane", [&] { return fd::model::rowOfLane(view.rows(), lane); }}})
        {
            INFO(what);
            const auto name = view.nameArea(row());
            REQUIRE(name);
            const auto at = name->getCentre().toFloat();
            // A double-click on its name.
            app.click(view, at);
            app.click(view, at);
            AppWindow::pump();
            CHECK(typing());
            done();
            // F2, with it selected.
            shell.actions().perform("edit.rename");
            AppWindow::pump();
            CHECK(typing());
            done();
            // Its header menu's Rename... A lane's menu reads like a track's: its own commands,
            // the same verb for deleting, then its colour; a track's has Group tracks before it
            // (groups.md §12).
            app.click(view, at, juce::ModifierKeys::rightButtonModifier);
            AppWindow::pump(300);
            std::vector<juce::String> items;
            for (const auto& [title, item] : app.menuItems())
                items.push_back(title);
            const auto ellipsis = juce::String::fromUTF8("\xE2\x80\xA6");
            const auto own =
                juce::String(what) == "lane"
                    ? std::vector<juce::String>{"Rename" + ellipsis, "Assign parameter" + ellipsis,
                                                "Turn lane off", "Delete automation lane", "Colour"}
                : juce::String(what) == "return"
                    ? std::vector<juce::String>{"Rename" + ellipsis, "Duplicate track", "Delete track",
                                                "Colour"}
                    : std::vector<juce::String>{"Rename" + ellipsis, "Duplicate track", "Delete track",
                                                "Group tracks", "Colour"};
            INFO(juce::StringArray(items.data(), static_cast<int>(items.size())).joinIntoString(" | "));
            REQUIRE(items.size() > own.size());
            CHECK(std::vector<juce::String>(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(own.size())) == own);
            if (const char* dump = std::getenv("FD_DUMP_DIR"); dump != nullptr && juce::String(what) == "lane")
                if (auto* m = app.menu())
                    dumpPng(dump, "lane-menu@2x.png", m->createComponentSnapshot(m->getLocalBounds(), true, 2.0f));
            REQUIRE(app.choose("Rename"));
            AppWindow::pump(300);
            CHECK(typing());
            done();
            // The window getting the system's focus back after the field took it (the window
            // manager's answer to a menu closing can come late): JUCE hands it to what had it
            // before, without telling the field.
            peer->handleFocusLoss();
            shell.actions().perform("edit.rename");
            peer->handleFocusGain();
            AppWindow::pump();
            CHECK(typing());
            done();
        }

        // Return keeps it and the keys go back to the shell, not to a control in the header (where
        // Return would press it).
        shell.actions().perform("edit.rename"); // the lane, selected last
        AppWindow::pump();
        REQUIRE(typing());
        editor.field().insertTextAtCaret("Filter");
        editor.field().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        AppWindow::pump();
        CHECK(juce::Component::getCurrentlyFocusedComponent() == &shell);
        s.updateSnapshotNow();
        CHECK(s.snapshot()->automationLane(lane)->name == "Filter");
    }
    SECTION("a press elsewhere ends typing and keeps what was typed")
    {
        const auto first = s.snapshot()->tracks[0].id, second = s.snapshot()->tracks[1].id;
        auto& editor = view.nameEditor();
        auto nameOf = [&s](std::uint64_t id) {
            s.updateSnapshotNow();
            return s.snapshot()->channel(id)->name;
        };
        auto renameFirst = [&](const char* typed) {
            view.selectTrack(first);
            shell.actions().perform("edit.rename");
            AppWindow::pump();
            REQUIRE(editor.field().hasKeyboardFocus(false));
            editor.field().insertTextAtCaret(typed);
            AppWindow::pump();
        };

        SECTION("a press on the empty lanes")
        {
            renameFirst("Drums");
            app.click(view, view.getLocalBounds().toFloat().getRelativePoint(0.6f, 1.0f).translated(0.0f, -30.0f));
            AppWindow::pump();
            CHECK_FALSE(editor.isEditing());
            CHECK(nameOf(first) == "Drums");
            CHECK(juce::Component::getCurrentlyFocusedComponent() == &shell);
        }
        SECTION("a press on another panel")
        {
            renameFirst("Keys");
            auto& inspector = shell.inspector();
            app.click(inspector, inspector.getLocalBounds().toFloat().getRelativePoint(0.5f, 1.0f).translated(0.0f, -10.0f));
            AppWindow::pump();
            CHECK_FALSE(editor.isEditing());
            CHECK(nameOf(first) == "Keys");
        }
        SECTION("a double-click on another name: this one kept, that one renamed next")
        {
            renameFirst("Bass");
            const auto at = view.nameArea(rowOf(view, second))->getCentre().toFloat();
            app.click(view, at); // both presses at once, as when the app was busy between them
            app.click(view, at);
            AppWindow::pump();
            CHECK(nameOf(first) == "Bass");
            CHECK(editor.isEditing());
            CHECK(editor.field().hasKeyboardFocus(false));
            CHECK(editor.field().getText().toStdString() == nameOf(second));
        }
        SECTION("a press in the field itself keeps typing")
        {
            renameFirst("Pad");
            app.click(editor.field(), editor.field().getLocalBounds().getCentre().toFloat());
            AppWindow::pump();
            CHECK(editor.isEditing());
            CHECK(editor.field().hasKeyboardFocus(false));
        }
    }

    // Maximise is the window manager's on Linux (menu-bar.md §4), so it needs one: a desktop
    // session, not CI's bare X server.
#if JUCE_LINUX || JUCE_BSD
    if (std::getenv("XDG_CURRENT_DESKTOP") != nullptr)
        SECTION("Maximise stays maximised, and Restore returns the window to where it was (C10 fix)")
        {
            auto& window = *app.window;
            const auto area =
                juce::Desktop::getInstance().getDisplays().getDisplayForRect(window.getBounds())->userBounds.toNearestInt();
            window.setBounds(area.reduced(area.getWidth() / 8, area.getHeight() / 8));
            AppWindow::pump(300);
            const auto before = window.getBounds();
            // Until `done`, or about two seconds: the window manager answers in its own time.
            const auto until = [](auto&& done)
            {
                for (int i = 0; i < 40 && !done(); ++i)
                    AppWindow::pump(50);
                return done();
            };
            window.maximiseButtonPressed();
            REQUIRE(until([&] { return window.isFullScreen() && window.getBounds() != before; }));
            AppWindow::pump(500);
            // Still maximised once the window manager has answered: JUCE read its state back from
            // there and, before the patch, forgot a maximise the window manager never heard of.
            CHECK(window.isFullScreen());
            window.maximiseButtonPressed(); // Restore (as a double-click on the title bar)
            CHECK(until([&] { return !window.isFullScreen() && window.getBounds() == before; }));
            AppWindow::pump(300);
            CHECK_FALSE(window.isFullScreen());
            CHECK(window.getBounds() == before);
        }
#endif
}

TEST_CASE("presets: the header's bar, the list and the save form, by mouse and keys; the Module menu (A1)",
          "[integration]")
{
    ShellFixture f;
    juce::TemporaryFile folderName;
    const auto folder = folderName.getFile();
    f.session->setPresetsFolder(folder);
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto comp = s.addDevice(t0, "fd:compressor");
    REQUIRE(s.addDevice(t0, "fd:utility") != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().selectTrack(t0); // its modules show below, as a click on its header does
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    auto& chain = f.shell->detail().chain();
    REQUIRE(chain.track() == t0);
    const auto panels = findAll<fd::ui::DevicePanel>(chain);
    REQUIRE(panels.size() == 2);
    auto* compressor = panels[0];
    auto* utility = panels[1];
    auto* bar = compressor->presetBar();
    REQUIRE(bar != nullptr);
    REQUIRE(utility->presetBar() != nullptr);
    auto type = [](juce::TextEditor& field, const juce::String& text)
    {
        field.setText(text, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(
            20); // fields tell of changes asynchronously
    };
    CHECK(bar->field().presetName() == "Default settings");

    // Two presets of yours, tagged.
    const auto& info = fd::findDevice("compressor")->info();
    auto& library = s.presetLibrary();
    auto host = s.presetHost(comp);
    auto state = host->state();
    state.params["threshold"] = -10.0f;
    REQUIRE(library.save(info, state, "Glue", {"Bus", "Subtle", "Mine"}, "Gentle glue for a bus", "Me", false)
                .preset);
    state.params["threshold"] = -40.0f;
    REQUIRE(library.save(info, state, "Smash", {"Drums", "Heavy"}, "", "Me", false).preset);
    const float defaultThreshold = host->state().params.at("threshold");

    // The Module menu acts on the selected module.
    CHECK_FALSE(actions.isEnabled("module.savePresetAs"));
    const juce::Point<float> header(60.0f, 10.0f);
    chain.mouseDown(mouseEvent(*compressor, header, header, false));
    chain.mouseUp(mouseEvent(*compressor, header, header, false));
    REQUIRE(chain.selectedPanel() == compressor);
    CHECK(actions.isEnabled("module.savePresetAs"));
    CHECK_FALSE(actions.isEnabled("module.revertPreset"));
    CHECK(actions.label("module.useAsDefault") == "Use as the default for a new Compressor");
    CHECK(actions.label("module.copyCompare") == "Copy A to B");

    // The list: Default settings, the factory set and yours, the tags in use.
    bar->openList();
    REQUIRE(bar->isListOpen());
    auto& list = bar->list();
    CHECK(list.shown().size() == library.presetsFor(info).size());
    CHECK(list.shown().size() > 3); // the factory set is there
    CHECK(list.highlighted() == 0); // the loaded one
    auto at = [&list](const std::string& name)
    {
        for (size_t i = 0; i < list.shown().size(); ++i)
            if (list.shown()[i]->name == name)
                return static_cast<int>(i);
        FAIL("no preset " << name);
        return -1;
    };
    const auto tags = list.tagFilters();
    CHECK(std::any_of(tags.begin(), tags.end(), [](const auto& t) { return t.first == "Mine"; }));
    CHECK(f.shell->getLocalBounds().contains(
        list.getBounds().reduced(juce::roundToInt(fd::ui::Theme::current().shell.shadowRadius))));

    // Browsing loads as it goes; Escape goes back.
    list.highlight(at("Glue"));
    CHECK(host->state().params.at("threshold") == Catch::Approx(-10.0f).margin(0.01f));
    CHECK(bar->field().presetName() == "Glue");
    list.highlight(at("Smash"));
    CHECK(bar->field().presetName() == "Smash");
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(
            dump, "preset-list@2x.png",
            f.shell->createComponentSnapshot(list.getBounds().getUnion(f.shell->getLocalArea(
                                                 compressor, compressor->getLocalBounds().withHeight(28))),
                                             true, 2.0f));
    list.close(false);
    CHECK_FALSE(bar->isListOpen());
    CHECK(host->state().params.at("threshold") == Catch::Approx(defaultThreshold).margin(0.01f));
    CHECK(bar->field().presetName() == "Default settings");

    // Enter keeps what's loaded, as one undo step.
    bar->openList();
    list.highlight(at("Glue"));
    list.highlight(at("Smash"));
    list.highlight(at("Glue"));
    list.close(true);
    CHECK(bar->field().presetName() == "Glue");
    CHECK(s.undoDescription() == "Load preset Glue");

    // Search, tags and favourites narrow the list; previous and next step through it as filtered.
    bar->openList();
    type(list.searchField(), "smash");
    REQUIRE(list.shown().size() == 2);
    CHECK(list.shown()[0]->name == "Smash"); // the name's start first, then "Parallel smash"
    type(list.searchField(), "");
    list.setTagFilter("Mine", true);
    REQUIRE(list.shown().size() == 1);
    CHECK(list.shown()[0]->name == "Glue");
    list.setTagFilter("Mine", false);
    list.setFavourite(at("Smash"), true);
    list.setFavouritesOnly(true);
    CHECK(list.shown().size() == 1);
    list.setFavouritesOnly(false);
    CHECK(library.isFavourite(info.id, "user/Compressor/Smash"));

    // Save as…: the form in place, the name in use asks to replace.
    list.saveAsButton().onClick();
    REQUIRE(list.isShowingForm());
    CHECK(list.nameField().getText() == "Glue");
    CHECK(list.tagsField().getText() == "Bus, Subtle, Mine");
    list.confirmButton().onClick();
    CHECK(list.isShowingForm());
    CHECK(list.confirmButton().getButtonText() == "Replace");
    CHECK(list.problem().contains("Glue"));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "preset-form@2x.png", f.shell->createComponentSnapshot(list.getBounds(), true, 2.0f));
    type(list.nameField(), "Warm glue");
    CHECK(list.confirmButton().getButtonText() == "Save");
    type(list.tagsField(), "Bus, Warm");
    list.confirmButton().onClick();
    CHECK_FALSE(list.isShowingForm());
    CHECK(bar->controller().loadedName() == "Warm glue");
    const auto* warm = library.find(info, "user/Compressor/Warm glue");
    REQUIRE(warm != nullptr);
    CHECK(warm->tags == std::vector<std::string>{"Bus", "Warm"});
    list.close(true);

    // Changed since it loaded: the mark, and Revert.
    auto handle = s.openDevice(comp);
    handle->context().params.setPlain(0, -25.0f);
    for (int i = 0; i < 9; ++i)
        bar->refresh();
    CHECK(bar->field().isModified());
    CHECK(actions.isEnabled("module.revertPreset"));
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(
            dump, "preset-header@2x.png",
            f.shell->createComponentSnapshot(
                f.shell->getLocalArea(compressor, compressor->getLocalBounds().withHeight(28)), true, 2.0f));
        dumpPng(dump, "preset-header-narrow@2x.png",
                f.shell->createComponentSnapshot(f.shell->getLocalArea(utility, utility->getLocalBounds()),
                                                 true, 2.0f));
    }
    REQUIRE(actions.perform("module.revertPreset"));
    for (int i = 0; i < 9; ++i)
        bar->refresh();
    CHECK_FALSE(bar->field().isModified());

    // A/B from the header button: one undo step, and the menu follows.
    bar->compareButton().onClick();
    CHECK(bar->compareButton().getButtonText() == "B");
    CHECK(s.undoDescription() == "Compare B");
    CHECK(actions.isToggled("module.compare"));
    CHECK(actions.label("module.copyCompare") == "Copy B to A");

    // Previous and next, from the menu bar.
    REQUIRE(actions.perform("module.nextPreset"));
    CHECK(bar->field().presetName() == "Default settings"); // past the last: the first
}

TEST_CASE("each editor has its own grid: set where you edit, from Options, kept with the project (ADR 0047)",
          "[integration]")
{
    using G = fd::model::GridSettings;
    using Editor = fd::engine::Session::GridEditor;
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    const auto& actions = f.shell->actions();
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    auto& roll = detail.pianoRoll();
    const auto tempo = [&] { return s.snapshot()->tempo; };
    const auto track = s.snapshot()->tracks.front().id;
    REQUIRE(s.addDevice(track, "fd:subtractive-synth") != 0);

    // The defaults: Narrow in the arrangement, Wide in the piano roll.
    CHECK(view.grid() == G::arrangement());
    CHECK(roll.grid() == G::pianoRoll());

    // The arrangement's step beside its Snap, the ruler's row high, inside the header's width,
    // touching nothing; it reads the step the grid snaps to.
    auto& chip = view.gridChoice();
    REQUIRE(chip.isVisible());
    CHECK(chip.getX() >= view.snapToggle().getRight() + fd::ui::Theme::current().shell.itemGap);
    CHECK(chip.getRight() <= fd::app::ArrangementView::headerWidth());
    CHECK(chip.getHeight() == fd::ui::Theme::current().shell.rulerHeight);
    CHECK(chip.getWidth() == chip.preferredWidth()); // as wide as its step
    CHECK(chip.value() == juce::String(view.timeline().gridStepName(tempo())));
    CHECK(s.gridStep(Editor::Arrangement) == chip.value().toStdString());

    // Options act on the editor used last: the arrangement here.
    REQUIRE(actions.perform("grid.narrower"));
    CHECK(view.grid().width == G::Width::Narrowest);
    CHECK_FALSE(actions.isEnabled("grid.narrower")); // the narrowest
    REQUIRE(actions.perform("grid.wider"));
    REQUIRE(actions.perform("grid.wider"));
    CHECK(view.grid().width == G::Width::Medium);
    REQUIRE(actions.perform("grid.triplets"));
    CHECK(view.grid().triplets);
    CHECK(actions.isToggled("grid.triplets"));
    CHECK(roll.grid() == G::pianoRoll()); // the piano roll's stays as it was
    REQUIRE(actions.perform("grid.triplets"));
    // Fixed starts at the step showing, so nothing moves.
    const double before = view.timeline().gridStepBeats(tempo());
    REQUIRE(actions.perform("grid.fixed"));
    CHECK(view.grid().mode == G::Mode::Fixed);
    CHECK(view.timeline().gridStepBeats(tempo()) == Catch::Approx(before));
    CHECK(chip.value() == juce::String(view.timeline().gridStepName(tempo())));
    REQUIRE(actions.perform("grid.fixed"));
    CHECK(view.grid().mode == G::Mode::Adaptive);

    // Open a MIDI clip in the piano roll (a double-click on the first track) and work there.
    const auto& tl = view.timeline();
    const juce::Point<float> spot(static_cast<float>(fd::app::ArrangementView::headerWidth()) +
                                      static_cast<float>(tl.xForTime(2.1)),
                                  static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 20.0f);
    click(view, spot);
    click(view, spot, 2);
    s.updateSnapshotNow();
    REQUIRE(detail.showsPianoRoll());
    f.shell->finishAnimations();
    detail.frame(0.0);
    // Its own Snap and step in the clip view's header.
    fd::ui::ViewChoice* rollChip = nullptr;
    std::function<void(juce::Component&)> findChip = [&](juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* v = dynamic_cast<fd::ui::ViewChoice*>(child))
                rollChip = v;
            findChip(*child);
        }
    };
    findChip(detail);
    REQUIRE(rollChip != nullptr);
    CHECK(rollChip->isVisible());
    CHECK(rollChip->value() == roll.gridStepName());

    // A fixed 1/16 draws sixteenths at any zoom.
    auto sixteenths = roll.grid();
    sixteenths.mode = G::Mode::Fixed;
    sixteenths.value = G::Value::Sixteenth;
    roll.setGrid(sixteenths);
    CHECK(s.grid(Editor::PianoRoll) == sixteenths);
    CHECK(view.grid().mode == G::Mode::Adaptive); // the arrangement's stays
    const auto clipNotes = [&]
    {
        s.updateSnapshotNow();
        return s.snapshot()->tracks.front().clips.front().notes;
    };
    for (const auto& [zoom, pitch] : {std::pair{1.0f, 60}, std::pair{4.0f, 62}})
    {
        INFO("zoom " << zoom);
        if (zoom != 1.0f)
            roll.mouseMagnify(mouseEvent(roll, roll.pointFor(0.5, 60), roll.pointFor(0.5, 60), false), zoom);
        const auto at = roll.pointFor(0.6, pitch);
        click(roll, at);
        click(roll, at, 2);
        const auto notes = clipNotes();
        const auto it =
            std::find_if(notes.begin(), notes.end(), [pitch](const auto& n) { return n.pitch == pitch; });
        REQUIRE(it != notes.end());
        CHECK(it->length == Catch::Approx(0.25));
        CHECK(it->start == Catch::Approx(0.5));
    }

    // Working on notes, Options act on the piano roll: its Snap off leaves the arrangement's on.
    click(roll, roll.pointFor(3.0, 70));
    REQUIRE(actions.perform("view.snap"));
    CHECK_FALSE(roll.isSnapEnabled());
    CHECK(view.isSnapEnabled());
    CHECK_FALSE(actions.isToggled("view.snap"));
    REQUIRE(actions.perform("grid.wider"));
    CHECK(roll.grid().value == G::Value::Eighth);
    CHECK(view.grid().width == G::Width::Medium);

    // For review, zoomed in: the arrangement's corner and the clip view's header, at a standard
    // and a compact width.
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        for (const int width : {1800, 1280})
        {
            f.shell->setSize(width, 1000);
            f.shell->finishAnimations();
            view.frame(0.0, 0.0);
            detail.frame(0.0);
            const auto corner = view.getBoundsInParent().getTopLeft();
            dumpPng(dump, "grid-corner-" + juce::String(width) + "@2x.png",
                    f.shell->createComponentSnapshot({corner.x, corner.y,
                                                      fd::app::ArrangementView::headerWidth() + 160,
                                                      fd::app::ArrangementView::rulerHeight() + 24},
                                                     true, 2.0f));
            const auto header = detail.getBoundsInParent();
            dumpPng(
                dump, "grid-clip-header-" + juce::String(width) + "@2x.png",
                f.shell->createComponentSnapshot(
                    header.withHeight(fd::ui::Theme::current().shell.panelHeaderHeight + 40), true, 2.0f));
        }

    // An assistant (through the Session) sets a grid: the editor follows on its next frame.
    auto widest = view.grid();
    widest.width = G::Width::Widest;
    s.setGrid(Editor::Arrangement, widest);
    view.frame(0.0, 0.0);
    CHECK(view.grid() == widest);

    // Kept with the project, never an undo step or an unsaved change.
    CHECK(s.undoDescription().find("rid") == std::string::npos); // the last step is the last note
    juce::TemporaryFile project(fd::engine::Session::fileExtension);
    REQUIRE(s.saveAs(project.getFile()));
    const auto savedRoll = roll.grid();
    s.newProject();
    CHECK(view.grid() == G::arrangement());
    CHECK(roll.grid() == G::pianoRoll());
    REQUIRE(s.open(project.getFile()));
    CHECK(view.grid() == widest);
    CHECK(roll.grid() == savedRoll);
}

TEST_CASE(
    "the song row: the master's header, sections made, moved, renamed and resized by mouse (C3, ADR 0049)",
    "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    f.shell->setSize(1800, 900);
    auto& view = f.shell->arrangement();
    const auto snap0 = s.snapshot();
    // Something in every track for the overview: a MIDI clip a bar long, a bar apart.
    for (size_t i = 0; i < snap0->tracks.size(); ++i)
        REQUIRE(s.createMidiClip(snap0->tracks[i].id, 2.0 * static_cast<double>(i), 2.0) != 0);
    s.updateSnapshotNow();
    view.setSnapshot(s.snapshot());
    const auto lane = view.songLaneArea().toFloat();
    REQUIRE(lane.getY() == static_cast<float>(fd::app::ArrangementView::rulerHeight()));
    CHECK(view.songRowHeight() == fd::ui::Theme::current().shell.songRowHeight);
    const auto& tl = view.timeline();
    const auto xAt = [&](double seconds) { return lane.getX() + static_cast<float>(tl.xForTime(seconds)); };
    // A section's body is its title band (time-selection.md §5: below it, the overview selects time).
    const float body = lane.getY() + 12.0f, title = lane.getY() + 6.0f;
    const auto sections = [&s]
    {
        s.updateSnapshotNow();
        return s.snapshot()->sections;
    };

    // Its header selects the master.
    click(view, view.songHeaderArea().toFloat().getCentre().withX(40.0f));
    CHECK(view.selection().channel == s.snapshot()->master.id);
    CHECK(view.selection().sections.empty());

    // A drag across empty time makes a section, snapped to the bars; two of them.
    drag(view, {xAt(0.05), body}, {xAt(3.95), body});
    REQUIRE(sections().size() == 1);
    CHECK(sections()[0].start == Catch::Approx(0.0));
    CHECK(sections()[0].end == Catch::Approx(4.0));
    view.setSnapshot(s.snapshot());
    CHECK(view.selection().sections.size() == 1);
    CHECK(f.shell->inspector().sections() == std::vector{fd::model::InspectorSection::Section,
                                                         fd::model::InspectorSection::Master,
                                                         fd::model::InspectorSection::Mix});
    drag(view, {xAt(4.0) + 10.0f, body}, {xAt(7.95), body}); // clear of A's edge, which would resize A
    REQUIRE(sections().size() == 2);
    view.setSnapshot(s.snapshot());
    const auto a = sections()[0].id, b = sections()[1].id;

    if (const char* dump = std::getenv("FD_DUMP_DIR")) // for review, zoomed in
        dumpPng(dump, "song-row@2x.png",
                view.createComponentSnapshot(
                    view.getLocalBounds().withHeight(view.songLaneArea().getBottom() + 140), true, 2.0f));

    // A's body dragged four seconds later lands after B, with its music: B A.
    const auto clipsBefore = s.snapshot()->tracks[0].clips.front().start;
    drag(view, {xAt(1.0), body}, {xAt(5.0), body});
    REQUIRE(sections().size() == 2);
    CHECK(sections()[0].id == b);
    CHECK(sections()[1].id == a);
    CHECK(s.undoDescription() == "Move section A");
    s.updateSnapshotNow();
    CHECK(s.snapshot()->tracks[0].clips.front().start ==
          Catch::Approx(clipsBefore + 4.0)); // A's music went with it
    view.setSnapshot(s.snapshot());

    // A double-click on its name renames it there.
    click(view, {xAt(1.0), title}, 2);
    REQUIRE(view.nameEditor().isEditing());
    view.nameEditor().field().setText("Chorus", false);
    view.nameEditor().field().onReturnKey();
    CHECK(s.snapshot()->section(b) != nullptr);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->section(b)->name == "Chorus");

    // An edge moves the mark alone.
    view.setSnapshot(s.snapshot());
    drag(view, {xAt(3.99), body}, {xAt(2.0), body});
    CHECK(sections()[0].end == Catch::Approx(2.0));
    CHECK(s.undoDescription() == "Move section mark");
    CHECK(s.snapshot()->tracks[0].clips.front().start == Catch::Approx(clipsBefore + 4.0)); // nothing moved

    // Escape cancels a drag on the way.
    view.setSnapshot(s.snapshot());
    view.mouseDown(mouseEvent(view, {xAt(9.0), body}, {xAt(9.0), body}, false));
    view.mouseDrag(mouseEvent(view, {xAt(11.0), body}, {xAt(9.0), body}, true));
    CHECK(f.shell->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    view.mouseUp(mouseEvent(view, {xAt(11.0), body}, {xAt(9.0), body}, true));
    CHECK(sections().size() == 2);

    // Delete and Ctrl+D act on the selected section, with its music.
    view.setSnapshot(s.snapshot());
    click(view, {xAt(5.0), body});
    REQUIRE(view.selection().sections == std::set<std::uint64_t>{a});
    REQUIRE(f.shell->actions().perform("edit.duplicate"));
    CHECK(sections().size() == 3);
    CHECK(s.undoDescription() == "Copy section A");
    view.setSnapshot(s.snapshot());
    REQUIRE(f.shell->actions().perform("edit.delete"));
    CHECK(sections().size() == 2);

    // Narrow: the row and its names still clear of each other.
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        f.shell->setSize(1024, 700);
        view.setSnapshot(s.snapshot());
        dumpPng(dump, "song-row-1024@2x.png",
                view.createComponentSnapshot(
                    view.getLocalBounds().withHeight(view.songLaneArea().getBottom() + 140), true, 2.0f));
    }
}

TEST_CASE(
    "the song row: a lone section follows the pointer into empty time, a ghost where it lands (C3 review)",
    "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    f.shell->setSize(1800, 900);
    auto& view = f.shell->arrangement();
    REQUIRE(s.addSection(0.0, 4.0) != 0);
    s.updateSnapshotNow();
    view.setSnapshot(s.snapshot());
    const auto lane = view.songLaneArea().toFloat();
    const auto& tl = view.timeline();
    const auto xAt = [&](double seconds) { return lane.getX() + static_cast<float>(tl.xForTime(seconds)); };
    const float body = lane.getY() + 12.0f; // the title band: below it, the overview selects time
    // Pressed, then dragged eight seconds later: a ghost where it lands, before it's let go.
    view.mouseDown(mouseEvent(view, {xAt(1.0), body}, {xAt(1.0), body}, false));
    view.mouseDrag(mouseEvent(view, {xAt(9.0), body}, {xAt(1.0), body}, true));
    REQUIRE(view.sectionDrop().has_value());
    CHECK(*view.sectionDrop() == Catch::Approx(12.0)); // it cuts at its new end
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "song-row-drag@2x.png",
                view.createComponentSnapshot(
                    view.getLocalBounds().withHeight(view.songLaneArea().getBottom() + 60), true, 2.0f));
    view.mouseUp(mouseEvent(view, {xAt(9.0), body}, {xAt(1.0), body}, true));
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->sections.size() == 1);
    CHECK(s.snapshot()->sections[0].start == Catch::Approx(8.0));
    CHECK(s.undoDescription() == "Move section A");
}

TEST_CASE("the song row: a section copied and pasted at the playhead, cut and pasted (C3 review)",
          "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    f.shell->setSize(1800, 900);
    auto& view = f.shell->arrangement();
    auto& actions = f.shell->actions();
    const auto t0 = s.snapshot()->tracks[0].id;
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(s.addSection(4.0 * i, 4.0 * i + 4.0) != 0);
        REQUIRE(s.createMidiClip(t0, 4.0 * i + 1.0, 2.0) != 0);
    }
    s.updateSnapshotNow();
    view.setSnapshot(s.snapshot());
    const auto lane = view.songLaneArea().toFloat();
    const auto xAt = [&](double seconds)
    { return lane.getX() + static_cast<float>(view.timeline().xForTime(seconds)); };
    const float body = lane.getY() + 12.0f; // the title band: below it, the overview selects time
    const auto order = [&s]
    {
        s.updateSnapshotNow();
        std::string out;
        for (const auto& x : s.snapshot()->sections)
            out += (out.empty() ? "" : " ") + x.name;
        return out;
    };
    const auto ctrl = [&](char c)
    { return actions.handleKey(juce::KeyPress(c, juce::ModifierKeys::commandModifier, 0)); };

    // B clicked and copied; the playhead at C's end; pasted there, selected.
    click(view, {xAt(6.0), body});
    REQUIRE(view.selection().sections.size() == 1);
    REQUIRE(actions.isEnabled("edit.copy"));
    REQUIRE(ctrl('c'));
    s.setPosition(12.0);
    REQUIRE(ctrl('v'));
    CHECK(order() == "A B C B 2");
    CHECK(s.undoDescription() == "Paste section B");
    s.updateSnapshotNow();
    view.setSnapshot(s.snapshot());
    CHECK(view.selection().sections == std::set{s.snapshot()->sections[3].id});
    CHECK(s.snapshot()->tracks[0].clips.size() == 4);

    // A cut: gone with its music, and it pastes, at the nearer edge of the section the playhead is in.
    click(view, {xAt(2.0), body}); // A
    REQUIRE(ctrl('x'));
    CHECK(order() == "B C B 2");
    CHECK(s.undoDescription() == "Cut");
    s.setPosition(5.0); // inside C (4-8 now), nearer its start
    REQUIRE(actions.perform("edit.paste"));
    CHECK(order() == "B A C B 2");

    // The section's menu has them.
    juce::PopupMenu menu;
    view.onSectionMenu(menu, s.snapshot()->sections[0].id);
    CHECK(findItem(menu, "Copy") != nullptr);
    CHECK(findItem(menu, "Paste") != nullptr);
}

TEST_CASE("modulation in the DAW: an envelope's handle dragged onto a knob makes a route through the session",
          "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto synth = s.addDevice(t0, "fd:wavetable");
    REQUIRE(synth != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().selectTrack(t0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    auto& chain = f.shell->detail().chain();
    const auto panels = findAll<fd::ui::DevicePanel>(chain);
    REQUIRE(panels.size() == 1);
    auto& panel = *panels[0];
    panel.setPage(1, true); // Filter & Amp: the envelopes and the filter
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

    // The envelope's handle, and Filter 1's Cutoff knob, on the page shown.
    const auto shown = [&panel](juce::Component* c)
    {
        for (; c != nullptr && c != &panel; c = c->getParentComponent())
            if (!c->isVisible())
                return false;
        return c == &panel;
    };
    fd::ui::ModulationHandle* handle = nullptr;
    for (auto* h : findAll<fd::ui::ModulationHandle>(panel))
        if (shown(h))
            handle = h;
    REQUIRE(handle != nullptr);
    const auto& info = fd::findDevice("wavetable")->info();
    size_t cutoffParam = 0;
    for (size_t i = 0; i < info.params.size(); ++i)
        if (info.params[i].id == "filter1.cutoff")
            cutoffParam = i;
    fd::ui::ParamKnob* cutoff = nullptr;
    for (auto* k : findAll<fd::ui::ParamKnob>(panel))
        if (k->param() == cutoffParam && shown(k))
            cutoff = k;
    REQUIRE(cutoff != nullptr);

    // Pressed on the handle, dragged over Cutoff, let go there: as a mouse does.
    const auto from = handle->getLocalBounds().getCentre().toFloat();
    const auto onCutoff = handle->getLocalPoint(cutoff, cutoff->getLocalBounds().getCentre()).toFloat();
    handle->mouseDown(mouseEvent(*handle, from, from, false));
    handle->mouseDrag(mouseEvent(*handle, onCutoff, from, true));
    CHECK(cutoff->isDropTarget());
    handle->mouseUp(mouseEvent(*handle, onCutoff, from, true));
    s.updateSnapshotNow();

    auto handleOf = s.openDevice(synth);
    auto& params = handleOf->context().params;
    size_t src = 0, dst = 0;
    for (size_t i = 0; i < info.params.size(); ++i)
    {
        if (info.params[i].id == "mod1.src")
            src = i;
        if (info.params[i].id == "mod1.dst")
            dst = i;
    }
    CHECK(params.getPlain(src) == 1.0f); // Amp Env
    CHECK(info.params[dst].choices[static_cast<size_t>(params.getPlain(dst))] == "Filter 1 Cutoff");
    cutoff->refresh();
    CHECK(cutoff->ring().has_value());
    CHECK(s.undoDescription() == "Modulate Filter 1 Cutoff with Amp Env");
}

TEST_CASE(
    "the table editor in the DAW: opened from the Module menu below its module, a third view Tab reaches, "
    "closed by Done or the module going (ADR 0053)",
    "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    auto& detail = f.shell->detail();
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto synth = s.addDevice(t0, "fd:wavetable");
    REQUIRE(synth != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().selectTrack(t0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK_FALSE(actions.isEnabled("module.editWavetable")); // no module selected
    detail.chain().selectDevice(synth);
    REQUIRE(actions.isEnabled("module.editWavetable"));
    CHECK(actions.isEnabled("module.importWavetable"));
    CHECK(actions.isEnabled("module.exportWavetable"));
    CHECK_FALSE(actions.isEnabled("module.loadSample")); // a Sampler's

    // Module > Edit wavetable: the editor below the module, the panel grown for it.
    REQUIRE(actions.perform("module.editWavetable"));
    auto* editor = detail.editor();
    REQUIRE(editor != nullptr);
    CHECK(detail.editorModule() == synth);
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Editor);
    CHECK(editor->title() == "Osc 1, Basic Shapes");
    f.shell->finishAnimations();
    const auto& t = fd::ui::Theme::current();
    CHECK(detail.getHeight() >=
          fd::app::DeviceChain::preferredHeight() + t.shell.panelHeaderHeight + editor->minimumHeight());
    CHECK(editor->isVisible());
    CHECK(detail.chain().isVisible());
    CHECK(editor->getY() >= detail.chain().getBottom());
    CHECK(editor->getBottom() <= detail.getHeight());
    CHECK(editor->getHeight() >= editor->minimumHeight());
    for (auto* c : editor->commands())
        CHECK((c->isVisible() && c->getParentComponent() != nullptr && c->getParentComponent()->isVisible()));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "daw-table-editor.png",
                f.shell->createComponentSnapshot(f.shell->getLocalBounds(), true, 1.0f));

    // Tab goes round Modules, Clip and Table.
    REQUIRE(actions.perform("view.detailMode"));
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Devices);
    CHECK_FALSE(editor->isVisible());
    REQUIRE(actions.perform("view.detailMode"));
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Clip);
    REQUIRE(actions.perform("view.detailMode"));
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Editor);
    CHECK(editor->isVisible());

    // An edit: one undo step, the module's; undone, the editor shows the table again.
    auto& table = dynamic_cast<fd::devices::wavetable_ui::TableEditor&>(*editor);
    table.addLayer(fd::devices::wavetable::newLayer(fd::devices::wavetable::Recipe::SourceKind::Draw));
    CHECK(s.undoDescription() == "Add a layer");
    CHECK(editor->title() == "Osc 1, Basic Shapes (edited)");
    s.undo();
    table.frame();
    CHECK(table.recipe().layers.size() == 1);
    CHECK(editor->title() == "Osc 1, Basic Shapes");

    // Module > Edit wavetable is ticked while it's open.
    CHECK(actions.isToggled("module.editWavetable"));
    // The × on its tab closes it; the modules show.
    auto& tabs = detail.header().tabRow();
    CHECK(tabs.tabs() == juce::StringArray{"Modules", "Clip", "Table"});
    CHECK(tabs.closable() == 2);
    tabs.onClose(2);
    CHECK(detail.editor() == nullptr);
    CHECK(tabs.closable() == -1);
    CHECK_FALSE(actions.isToggled("module.editWavetable"));
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Devices);
    REQUIRE(actions.perform("view.detailMode"));
    REQUIRE(actions.perform("view.detailMode"));
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Devices); // only Modules and Clip now

    // Edit wavetable again opens it; again, closes it.
    detail.chain().selectDevice(synth);
    REQUIRE(actions.perform("module.editWavetable"));
    REQUIRE(detail.editor() != nullptr);
    REQUIRE(actions.perform("module.editWavetable"));
    CHECK(detail.editor() == nullptr);

    // The module going closes it.
    REQUIRE(actions.perform("module.editWavetable"));
    REQUIRE(detail.editor() != nullptr);
    s.removeDevice(synth);
    s.updateSnapshotNow();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(detail.editor() == nullptr);
    CHECK_FALSE(actions.isEnabled("module.editWavetable"));
}

TEST_CASE(
    "Make wavetable: an audio clip into a new Wavetable's editor, the way chooser open (table-editor.md §5)",
    "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-make-wavetable-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    // A second of a 220 Hz saw.
    fd::testkit::AudioBuffer saw(1, 48000);
    for (int i = 0; i < saw.numSamples(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 30; ++h)
            v += std::sin(fd::dsp::twoPi * h * 220.0 * i / 48000.0) / h;
        saw.at(0, i) = static_cast<float>(0.4 * v);
    }
    const auto file = root.getChildFile("Saw lead.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), saw, 48000.0));
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto clip = s.insertAudioFile(t0, file, 0.0);
    REQUIRE(clip != 0);
    s.updateSnapshotNow();
    const auto sound = s.clipSound(clip, 0.25, 0.75);
    REQUIRE(sound);
    CHECK(sound->samples.size() == 24000);
    CHECK(sound->name == "Saw lead");

    CHECK_FALSE(actions.isEnabled("clip.makeWavetable"));
    f.shell->arrangement().setSelection(fd::model::withClips(*s.snapshot(), {clip}, t0, t0));
    REQUIRE(actions.isEnabled("clip.makeWavetable"));
    const auto tracks = s.snapshot()->tracks.size();
    REQUIRE(actions.perform("clip.makeWavetable"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->tracks.size() == tracks + 1);
    const auto& made = s.snapshot()->tracks.back();
    CHECK(made.name == "Saw lead");
    REQUIRE(made.devices.size() == 1);
    CHECK(made.devices[0].deviceId == "wavetable");
    auto* editor = f.shell->detail().editor();
    REQUIRE(editor != nullptr);
    auto& table = dynamic_cast<fd::devices::wavetable_ui::TableEditor&>(*editor);
    CHECK(table.choosingAudio());
    CHECK(table.audioWay() == fd::devices::wavetable::AudioWay::Pitched);
    CHECK(editor->title() == "Osc 1, Saw lead");
    table.useAudio();
    root.deleteRecursively();
}

TEST_CASE("Module > Load sample… is the selected Sampler's (devices/sampler/SPEC.md, S10)", "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto eq = s.addDevice(t0, "fd:eq");
    const auto sampler = s.addDevice(t0, "fd:sampler");
    REQUIRE(sampler != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().selectTrack(t0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK_FALSE(actions.isEnabled("module.loadSample")); // nothing selected
    f.shell->detail().chain().selectDevice(eq);
    CHECK_FALSE(actions.isEnabled("module.loadSample"));
    f.shell->detail().chain().selectDevice(sampler);
    CHECK(actions.isEnabled("module.loadSample"));
    // In the Module menu, with the module's own commands.
    const auto menus = fd::model::menuBar(fd::model::MenuPlatform::Windowed);
    const auto module =
        std::find_if(menus.begin(), menus.end(), [](const auto& m) { return m.title == "Module"; });
    REQUIRE(module != menus.end());
    const auto ids = fd::model::menuActions({*module});
    CHECK(std::find(ids.begin(), ids.end(), "module.loadSample") != ids.end());
}

TEST_CASE(
    "Module > the Granular's sources and its roll are the selected Granular's (devices/granular/SPEC.md)",
    "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    auto& actions = f.shell->actions();
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto eq = s.addDevice(t0, "fd:eq");
    const auto granular = s.addDevice(t0, "fd:granular");
    REQUIRE(granular != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().selectTrack(t0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    const std::vector<std::string> own{"module.loadSource1", "module.loadSource2", "module.swapSources"};
    f.shell->detail().chain().selectDevice(eq);
    for (const auto& id : own)
        CHECK_FALSE(actions.isEnabled(id));
    CHECK_FALSE(actions.isEnabled("module.keepRoll")); // an EQ rolls no dice
    f.shell->detail().chain().selectDevice(granular);
    for (const auto& id : own)
        CHECK(actions.isEnabled(id));
    // Keep as played: the Granular's roll, one undo step, its label turning to Let it roll.
    REQUIRE(actions.isEnabled("module.keepRoll"));
    CHECK(actions.label("module.keepRoll") == "Keep as played");
    actions.perform("module.keepRoll");
    s.updateSnapshotNow();
    CHECK(s.undoDescription() == "Keep as played");
    CHECK(actions.label("module.keepRoll") == "Let it roll");
    CHECK(fd::model::chanceIn(*s.snapshot()).keptModules == 1);
    s.undo();
    s.updateSnapshotNow();
    CHECK(fd::model::chanceIn(*s.snapshot()).keptModules == 0);
    // In the Module menu, with the module's own commands.
    const auto menus = fd::model::menuBar(fd::model::MenuPlatform::Windowed);
    const auto module =
        std::find_if(menus.begin(), menus.end(), [](const auto& m) { return m.title == "Module"; });
    REQUIRE(module != menus.end());
    const auto ids = fd::model::menuActions({*module});
    for (const auto& id : own)
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
}

TEST_CASE(
    "the song's key in the shell: the top bar's field, the Key popover, the Song menu, the piano roll (P3)",
    "[integration]")
{
    ShellFixture f;
    f.shell->setSize(1800, 1000);
    const char* dump = std::getenv("FD_DUMP_DIR");
    fd::app::TransportBar* bar = nullptr;
    for (auto* child : f.shell->getChildren())
        if (auto* b = dynamic_cast<fd::app::TransportBar*>(child))
            bar = b;
    REQUIRE(bar != nullptr);
    auto& field = bar->keyField();
    auto& popover = f.shell->keyPopover();
    const auto& actions = f.shell->actions();
    auto refresh = [&]
    {
        f.session->updateSnapshotNow();
        bar->frame(0.0, f.session->snapshot()->tempo);
    };
    auto barShot = [&](const juce::String& name)
    {
        if (dump != nullptr)
            dumpPng(dump, "key-bar-" + name + "-" + juce::String(f.shell->getWidth()) + "@2x.png",
                    f.shell->createComponentSnapshot(bar->getBounds(), true, 2.0f));
    };
    // Nothing in the top bar overlaps or leaves it, the key field shown.
    auto barFits = [&]
    {
        std::vector<juce::Rectangle<int>> shown;
        for (auto* c : bar->getChildren())
            if (c->isVisible())
                shown.push_back(c->getBounds());
        for (size_t a = 0; a < shown.size(); ++a)
        {
            CHECK(bar->getLocalBounds().contains(shown[a]));
            for (size_t b = a + 1; b < shown.size(); ++b)
                CHECK_FALSE(shown[a].intersects(shown[b]));
        }
        CHECK(field.isVisible());
        CHECK(field.getWidth() >= field.widthFor(field.value())); // never cut short
    };

    // A new project: "No key", dimmed.
    refresh();
    CHECK(field.value() == "No key");
    CHECK(field.isPlaceholder());
    CHECK_FALSE(actions.isEnabled("song.noKey"));
    barFits();
    barShot("none");

    // Song > Key… opens the popover under the field; a click on a key sets the root (its major).
    REQUIRE(actions.perform("song.key"));
    REQUIRE(popover.isOpen());
    // Under the bar (its bounds hold its shadow too), at the field's left.
    const int margin = juce::roundToInt(fd::ui::Theme::current().shell.shadowRadius);
    CHECK(popover.getY() + margin >= bar->getBottom());
    CHECK(popover.keys().root() == -1);
    CHECK(popover.found().empty()); // no notes yet
    if (dump != nullptr)
        dumpPng(dump, "key-popover-none.png", f.shell->createComponentSnapshot(popover.getBounds()));
    auto& keys = popover.keys();
    click(keys, keys.keyArea(9).getCentre());
    CHECK(popover.key() == fd::music::Key{9, "major"});

    // The search filters the scales, by name or other name; Return chooses the first.
    CHECK(popover.shownScales().size() == 36);
    popover.setQuery("dor");
    CHECK(popover.shownScales() == std::vector<std::string>{"dorian", "dorian-sharp-4"});
    popover.setQuery("bhairav");
    CHECK(popover.shownScales() == std::vector<std::string>{"double-harmonic"});
    // The widest rows fit whole: a list as wide as its widest row.
    for (const char* query : {"harmonic", "dim", "pent"})
    {
        popover.setQuery(query);
        if (dump != nullptr)
            dumpPng(dump, juce::String("key-popover-search-") + query + ".png",
                    f.shell->createComponentSnapshot(popover.getBounds()));
    }
    popover.setQuery("");
    CHECK(popover.shownScales().size() == 36);

    // Hovering a scale previews it on the keys, changing nothing.
    popover.previewScale("minor");
    CHECK(keys.notes() == fd::music::notes(fd::music::Key{9, "minor"}));
    CHECK(popover.key() == fd::music::Key{9, "major"});
    if (dump != nullptr)
        dumpPng(dump, "key-popover-preview.png", f.shell->createComponentSnapshot(popover.getBounds()));
    popover.previewScale({});
    popover.chooseScale("minor");
    CHECK(keys.root() == 9);
    CHECK(keys.notes() == fd::music::notes(fd::music::Key{9, "minor"}));
    if (dump != nullptr)
        dumpPng(dump, "key-popover-a-minor.png", f.shell->createComponentSnapshot(popover.getBounds()));

    // Closed: everything it changed is one undo step, named after the key.
    popover.close();
    CHECK_FALSE(popover.isOpen());
    CHECK(f.session->undoDescription() == "Set key to A minor");
    refresh();
    CHECK(field.value() == "A minor");
    CHECK_FALSE(field.isPlaceholder());
    CHECK(field.getTooltip().contains("A B C D E F G"));
    CHECK(actions.isEnabled("song.noKey"));
    barFits();
    barShot("a-minor");

    // The longest name: in full when there's room, short when it's tight, never dropped.
    REQUIRE(f.session->setKey(fd::music::Key{6, "phrygian-dominant"}));
    for (int width : {1800, 1200, 1024})
    {
        f.shell->setSize(width, 1000);
        refresh();
        barFits();
        barShot("long");
    }
    CHECK(field.value() == juce::String::fromUTF8("F♯ phr dom")); // at the window's least
    f.shell->setSize(1800, 1000);
    refresh();
    CHECK(field.value() == juce::String::fromUTF8("F♯ Phrygian dominant"));

    // Song > No key; undo brings it back.
    REQUIRE(actions.perform("song.noKey"));
    refresh();
    CHECK(field.value() == "No key");
    f.session->undo();
    refresh();
    CHECK(field.value() == juce::String::fromUTF8("F♯ Phrygian dominant"));

    // The piano roll: the key's rows lit, its root labelled, names spelled by it.
    const auto track = f.session->snapshot()->tracks.front().id;
    const auto clip = f.session->createMidiClip(track, 0.0, 4.0);
    REQUIRE(clip != 0);
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
    f.session->setNotes(clip, notes);
    f.session->updateSnapshotNow();
    f.shell->arrangement().setSelection({.channel = track, .clips = {clip}});
    f.shell->finishAnimations();
    auto& detail = f.shell->detail();
    REQUIRE(detail.showsPianoRoll());
    auto rollShot = [&](const juce::String& name)
    {
        if (dump != nullptr)
        {
            auto& roll = detail.pianoRoll();
            dumpPng(dump, "key-roll-" + name + ".png",
                    f.shell->createComponentSnapshot(f.shell->getLocalArea(&roll, roll.getLocalBounds())));
        }
    };
    rollShot("f-sharp-phrygian-dominant");
    REQUIRE(f.session->setKey(fd::music::Key{6, "major"}));
    f.session->updateSnapshotNow();
    f.shell->finishAnimations();
    rollShot("f-sharp-major");
    REQUIRE(f.session->setKey(std::nullopt));
    f.session->updateSnapshotNow();
    rollShot("none");

    // With the clip selected, the popover finds A minor in its notes; a click on it sets it.
    REQUIRE(actions.perform("song.key"));
    REQUIRE(popover.isOpen());
    REQUIRE_FALSE(popover.found().empty());
    CHECK(popover.found().front().key == fd::music::Key{9, "minor"});
    if (dump != nullptr)
        dumpPng(dump, "key-popover-found.png", f.shell->createComponentSnapshot(popover.getBounds()));
    popover.chooseKey(popover.found().front().key);
    // Changed back where it began: no step at all.
    popover.chooseKey(std::nullopt);
    const auto before = f.session->undoDescription();
    popover.close();
    CHECK(f.session->undoDescription() == before);
    CHECK_FALSE(f.session->snapshot()->keys.first().has_value());

    // A click on the field opens it, and a second closes it.
    REQUIRE(field.onOpen);
    field.onOpen();
    CHECK(popover.isOpen());
    field.onOpen();
    CHECK_FALSE(popover.isOpen());

    REQUIRE(f.session->setKey(fd::music::Key{9, "minor"}));
    f.session->updateSnapshotNow();
    f.shell->finishAnimations();
    rollShot("a-minor");
}

TEST_CASE(
    "presets: a click on the preset field opens the list once and keeps it open, however long it's held; "
    "the next click closes it",
    "[integration]")
{
    ShellFixture f;
    juce::TemporaryFile folderName;
    f.session->setPresetsFolder(folderName.getFile());
    f.shell->setSize(1800, 1000);
    // On screen, as in the app: JUCE's combo box only acts on a release over it in a window.
    f.shell->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    f.shell->setVisible(true);
    auto& s = *f.session;
    const auto t0 = s.snapshot()->tracks[0].id;
    REQUIRE(s.addDevice(t0, "fd:compressor") != 0);
    s.updateSnapshotNow();
    f.shell->arrangement().selectTrack(t0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    const auto panels = findAll<fd::ui::DevicePanel>(f.shell->detail().chain());
    REQUIRE(panels.size() == 1);
    auto* bar = panels[0]->presetBar();
    REQUIRE(bar != nullptr);
    auto& field = bar->field();
    const auto centre = field.getLocalBounds().getCentre().toFloat();
    // A click held long enough for what the press started to run before the release.
    const auto click = [&](int clicks)
    {
        field.mouseDown(
            mouseEvent(field, centre, centre, false, juce::ModifierKeys::leftButtonModifier, clicks));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        field.mouseUp(
            mouseEvent(field, centre, centre, false, juce::ModifierKeys::leftButtonModifier, clicks));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    };
    REQUIRE_FALSE(bar->isListOpen());
    click(1);
    CHECK(bar->isListOpen());
    click(2); // quickly after: still one toggle
    CHECK_FALSE(bar->isListOpen());
    click(3);
    CHECK(bar->isListOpen());
    bar->closeList(true);
    f.shell->removeFromDesktop();
}

TEST_CASE(
    "inspector: over the arrangement below 1440 px, open or closed across it; MODULE and ROUTES for the "
    "module selected in the chain (C6)",
    "[integration]")
{
    ShellFixture f;
    auto& s = *f.session;
    auto& shell = *f.shell;
    const auto t0 = s.snapshot()->tracks[0].id;
    const auto filter = s.addDevice(t0, "fd:filter");
    const auto lfo = s.addModulator(t0, fd::model::ModulatorKind::Lfo, 0);
    REQUIRE(lfo != 0);
    const fd::engine::Session::ParamTarget freq{t0, "m" + std::to_string(filter) + "/freq"};
    REQUIRE(s.routeModulator(lfo, freq, 0.5f));
    REQUIRE(s.automateInNewLane(freq) != 0);
    s.updateSnapshotNow();
    auto& inspector = shell.inspector();
    auto& arrangement = shell.arrangement();
    const auto open = [&](bool on)
    {
        shell.finishAnimations();
        if (inspector.isVisible() != on)
            shell.actions().perform("view.inspector");
        shell.finishAnimations();
    };

    // Wide: docked beside the arrangement.
    shell.setSize(1600, 900);
    open(true);
    REQUIRE(inspector.isVisible());
    CHECK(arrangement.getRight() <= inspector.getX());
    CHECK(arrangement.coveredRight() == 0);
    // Compact: still open, over the arrangement, which keeps its width and knows what's covered.
    shell.setSize(1280, 900);
    shell.finishAnimations();
    REQUIRE(inspector.isVisible());
    CHECK(inspector.getRight() == arrangement.getRight());
    CHECK(inspector.getX() > arrangement.getX());
    CHECK(arrangement.coveredRight() == inspector.getWidth());
    const auto overWidth = arrangement.getWidth();
    open(false);
    CHECK(arrangement.getWidth() == overWidth); // closing it gives no width back: it took none
    CHECK(arrangement.coveredRight() == 0);
    // Closed stays closed across the threshold, and open stays open.
    shell.setSize(1600, 900);
    shell.finishAnimations();
    CHECK_FALSE(inspector.isVisible());
    open(true);
    shell.setSize(1280, 900);
    shell.finishAnimations();
    CHECK(inspector.isVisible());

    // MODULE: the Filter selected in the chain, the chain used last.
    arrangement.selectTrack(t0);
    auto& chain = shell.detail().chain();
    chain.selectDevice(filter);
    REQUIRE(chain.onActivated);
    chain.onActivated();
    shell.updateInspectorModule();
    REQUIRE(inspector.module().has_value());
    CHECK(inspector.module()->type == "Filter");
    REQUIRE_FALSE(inspector.sections().empty());
    CHECK(inspector.sections().front() == fd::model::InspectorSection::Module);
    REQUIRE(inspector.movers().size() == 1);
    CHECK(inspector.movers()[0]->getButtonText().startsWith("LFO"));
    REQUIRE(inspector.laneShows().size() == 1);
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dir, "inspector-over-module@2x.png",
                shell.createComponentSnapshot(shell.getLocalBounds(), true, 2.0f));
        dumpPng(dir, "inspector-module-crop@2x.png",
                shell.createComponentSnapshot(inspector.getBounds().expanded(24, 0), true, 2.0f));
    }

    // Moved by: the LFO, selected; its ROUTES, whose amount is set there.
    REQUIRE(inspector.movers()[0]->onClick);
    inspector.movers()[0]->onClick();
    CHECK(chain.selectedDevice() == lfo);
    shell.updateInspectorModule();
    REQUIRE(inspector.sections().size() >= 2);
    CHECK(inspector.sections()[1] == fd::model::InspectorSection::Routes);
    REQUIRE(inspector.routes() != nullptr);
    REQUIRE(inspector.routes()->rows().size() == 1);
    REQUIRE(inspector.routes()->onAmount);
    inspector.routes()->onAmountGesture(true);
    inspector.routes()->onAmount(0, 1.0f);
    inspector.routes()->onAmountGesture(false);
    s.updateSnapshotNow();
    const auto* channel = s.snapshot()->channel(t0);
    REQUIRE(channel != nullptr);
    float amount = 0.0f;
    for (const auto& d : channel->devices)
        if (d.id == lfo && d.modulator != nullptr && !d.modulator->routes.empty())
            amount = d.modulator->routes[0].amount;
    CHECK(amount == Catch::Approx(1.0f));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        shell.updateInspectorModule();
        dumpPng(dir, "inspector-routes-crop@2x.png",
                shell.createComponentSnapshot(inspector.getBounds().expanded(24, 0), true, 2.0f));
    }

    // The arrangement used last: MODULE goes.
    REQUIRE(arrangement.onActivated);
    arrangement.onActivated();
    shell.updateInspectorModule();
    CHECK_FALSE(inspector.module().has_value());
    CHECK((inspector.sections().empty() ||
           inspector.sections().front() != fd::model::InspectorSection::Module));
}

TEST_CASE(
    "reduced latency when monitoring: the Options tick, Settings > Recording, a bypassed module's header, "
    "the top bar's reminder (C7, recording.md §22.1)",
    "[integration]")
{
    ShellFixture f;
    tracktion::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 2;
    io.outputChannels = 2;
    tracktion::test_utilities::EnginePlayer player(f.engine, io);
    const auto pump = [&](double seconds)
    {
        juce::AudioBuffer<float> in(2, 480);
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            in.clear();
            player.process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    auto& s = *f.session;
    auto& shell = *f.shell;
    shell.setSize(1800, 1000);
    pump(0.1);
    const auto t0 = s.snapshot()->tracks[0].id;
    REQUIRE_FALSE(s.audioInputs().empty());
    s.setAudioInput(t0, s.audioInputs().front().id);
    s.setMonitor(t0, fd::model::Monitor::In);
    // A Limiter (3 ms ahead), a linear-phase EQ, and a Saturator (oversampled, under a millisecond).
    const auto limiter = s.addDevice(t0, "fd:limiter");
    const auto eq = s.addDevice(t0, "fd:eq");
    {
        auto handle = s.openDevice(eq);
        REQUIRE(handle != nullptr);
        auto& params = handle->context().params;
        const auto& specs = handle->context().info.params;
        const auto phase = static_cast<size_t>(
            std::distance(specs.begin(), std::find_if(specs.begin(), specs.end(),
                                                      [](const auto& p) { return p.id == "phase"; })));
        params.beginGesture(phase);
        params.setPlain(phase, 1.0f);
        params.endGesture(phase);
    }
    const auto saturator = s.addDevice(t0, "fd:saturator");
    s.updateSnapshotNow();
    shell.arrangement().selectTrack(t0);
    pump(0.3);
    auto& chain = shell.detail().chain();
    std::map<std::uint64_t, fd::ui::DevicePanel*> panels;
    for (const auto id : {limiter, eq, saturator})
    {
        chain.selectDevice(id);
        panels[id] = chain.selectedPanel();
        REQUIRE(panels[id] != nullptr);
    }
    REQUIRE(panels.size() == 3);
    fd::app::TransportBar* bar = nullptr;
    for (auto* child : shell.getChildren())
        if (auto* b = dynamic_cast<fd::app::TransportBar*>(child))
            bar = b;
    REQUIRE(bar != nullptr);
    const auto reminder = [&]() -> fd::ui::HeaderBadge*
    {
        for (auto* b : findAll<fd::ui::HeaderBadge>(*bar))
            if (b->isVisible() && b->text() == "Reduced latency")
                return b;
        return nullptr;
    };
    const auto frame = [&]
    {
        pump(0.2);
        shell.detail().frame(0.0);
        bar->frame(0.0, s.snapshot()->tempo);
    };

    // Off: nothing said.
    frame();
    for (const auto& [id, p] : panels)
        CHECK(p->hostNotice() == nullptr);
    CHECK(reminder() == nullptr);

    // Options > Reduced latency when monitoring: the EQ and the Limiter say so, their power as set;
    // the Saturator's fraction of a millisecond stays in; the top bar reminds.
    auto& actions = shell.actions();
    CHECK_FALSE(actions.isToggled("project.reducedLatency"));
    REQUIRE(actions.perform("project.reducedLatency"));
    CHECK(s.reducedLatency());
    CHECK(actions.isToggled("project.reducedLatency"));
    frame();
    for (const auto id : {limiter, eq})
    {
        INFO(id);
        REQUIRE(panels[id]->hostNotice() != nullptr);
        CHECK(panels[id]->hostNotice()->text() == fd::app::DeviceChain::bypassedText);
        CHECK(const_cast<fd::ui::HeaderBadge*>(panels[id]->hostNotice())->getTooltip() ==
              fd::app::DeviceChain::bypassedTooltip);
    }
    CHECK(panels[saturator]->hostNotice() == nullptr);
    s.updateSnapshotNow();
    for (const auto& d : s.snapshot()->tracks[0].devices)
        CHECK(d.enabled);
    REQUIRE(reminder() != nullptr);
    CHECK(reminder()->getHeight() == fd::ui::Theme::current().deviceGrid.row);
    CHECK(bar->getLocalBounds().contains(reminder()->getBounds()));
    for (auto* c : bar->getChildren())
        if (c != reminder() && c->isVisible())
            CHECK_FALSE(c->getBounds().intersects(reminder()->getBounds()));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dir, "reduced-latency-chain@2x.png",
                shell.createComponentSnapshot(shell.detail().getBounds(), true, 2.0f));
        dumpPng(dir, "reduced-latency-eq-header@2x.png",
                panels[eq]->createComponentSnapshot(
                    panels[eq]->getLocalBounds().removeFromTop(fd::ui::Theme::current().deviceHeaderHeight),
                    true, 3.0f));
        dumpPng(dir, "reduced-latency-bar@2x.png",
                shell.createComponentSnapshot(bar->getBounds(), true, 2.0f));
    }

    // Settings > Recording has it too: off there, off everywhere.
    shell.showSettings("recording");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // it builds its rows
    auto& view = shell.settingsView();
    auto* toggle = dynamic_cast<fd::ui::Toggle*>(view.controlsOf("recording.reducedLatency").front());
    REQUIRE(toggle != nullptr);
    CHECK(toggle->getToggleState());
    CHECK(toggle->getButtonText() == "On");
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        if (auto* content = toggle->getParentComponent())
            dumpPng(dir, "reduced-latency-settings@2x.png",
                    content->createComponentSnapshot(
                        juce::Rectangle<int>(0, toggle->getY() - 120, content->getWidth(), 300), true, 2.0f));
    toggle->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the click is posted
    CHECK_FALSE(s.reducedLatency());
    CHECK(toggle->getButtonText() == "Off");
    shell.closeSettings();
    frame();
    for (const auto& [id, p] : panels)
        CHECK(p->hostNotice() == nullptr);
    CHECK(reminder() == nullptr);
}
