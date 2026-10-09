// Smoke tests of the DAW shell (docs/design/daw-shell.md, C1): it builds offscreen around a
// Session, every action has a unique id and shortcut, and keys reach the registry.

#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "model/Editing.h"
#include "ui/DeviceChain.h"
#include "ui/Shell.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
struct ShellFixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    tracktion::Engine engine{"fd-integration-tests"};
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
                const juce::Point<float> header(40.0f, fd::app::ArrangementView::rulerHeight + 20.0f);
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
    const float laneX = fd::app::ArrangementView::headerWidth;
    const float top = fd::app::ArrangementView::rulerHeight;

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
    auto xAt = [&](double seconds) { return fd::app::ArrangementView::headerWidth + static_cast<float>(tl.xForTime(seconds)); };
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
    auto clipInfo = [&]() -> std::optional<fd::model::ClipInfo> {
        f.session->updateSnapshotNow();
        const auto& clips = f.session->snapshot()->tracks.front().clips;
        return clips.empty() ? std::nullopt : std::optional(clips.front());
    };

    // Double-click an empty spot of the first track at 2.1 s: a one-bar MIDI clip from the grid
    // line before it, an instrument for the track, and the clip view showing it.
    const auto& tl = view.timeline();
    const juce::Point<float> spot(fd::app::ArrangementView::headerWidth + static_cast<float>(tl.xForTime(2.1)),
                                  fd::app::ArrangementView::rulerHeight + 20.0f);
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
