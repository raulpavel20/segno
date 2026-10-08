// Smoke tests of the DAW shell (docs/design/daw-shell.md, C1): it builds offscreen around a
// Session, every action has a unique id and shortcut, and keys reach the registry.

#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "model/Editing.h"
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
    f.session->updateSnapshotNow();
    f.session->setPosition(3.0);

    for (int width : {1800, 1200})
    {
        f.shell->setSize(width, 900);
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

namespace
{
juce::MouseEvent mouseEvent(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged,
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
{
    return {juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged};
}
} // namespace

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
