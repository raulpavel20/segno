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
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
{
    return {juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged};
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
