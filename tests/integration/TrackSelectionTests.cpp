// Several tracks chosen together (docs/design/groups.md §8, step 1; mixer.md §8): Ctrl+click and
// Shift+click on the headers and strips, a click on one of several selecting it alone; one's
// volume, pan, send, mute, solo and arm moving them all, one step each; Delete, Duplicate, Alt+M
// and dragging acting on them all; TRACKS in the inspector; the mixer's strips.

#include "ShellTestKit.h"
#include "model/Selection.h"
#include "model/Taper.h"
#include "ui/ChannelControls.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::testing;
using Catch::Approx;

namespace
{
/// Kick, Snare, Hats and Bass, each with a clip, and a return.
struct Several : ShellFixture
{
    juce::File root;
    std::uint64_t kick = 0, snare = 0, hats = 0, bass = 0, ret = 0;

    Several()
    {
        shell->setSize(1800, 1000);
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("fd-several-" + juce::Uuid().toString());
        REQUIRE(root.createDirectory());
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->updateSnapshotNow();
        while (session->snapshot()->tracks.size() < 4)
        {
            session->addTrack();
            session->updateSnapshotNow();
        }
        while (session->snapshot()->tracks.size() > 4)
        {
            REQUIRE(session->deleteTrack(session->snapshot()->tracks.back().id));
            session->updateSnapshotNow();
        }
        const auto tracks = session->snapshot()->tracks;
        std::uint64_t* ids[] = {&kick, &snare, &hats, &bass};
        const char* names[] = {"Kick", "Snare", "Hats", "Bass"};
        for (size_t i = 0; i < 4; ++i)
        {
            *ids[i] = tracks[i].id;
            session->renameTrack(tracks[i].id, names[i]);
            REQUIRE(session->createMidiClip(tracks[i].id, 0.0, 2.0) != 0);
        }
        ret = session->addReturnTrack("A Return");
        REQUIRE(ret != 0);
        sync();
    }

    ~Several()
    {
        shell.reset();
        session.reset();
        root.deleteRecursively();
    }

    void sync()
    {
        session->updateSnapshotNow();
        shell->arrangement().setSnapshot(session->snapshot());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    [[nodiscard]] juce::Point<float> header(std::uint64_t channel)
    {
        auto& view = shell->arrangement();
        for (int i = 0; i < static_cast<int>(view.rows().size()); ++i)
            if (!view.rows()[static_cast<size_t>(i)].isLane() &&
                view.rows()[static_cast<size_t>(i)].channel == channel)
                return view.nameArea(i)->getCentre().toFloat();
        FAIL("no row for the channel");
        return {};
    }

    [[nodiscard]] const fd::model::TrackInfo& info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }

    [[nodiscard]] std::vector<std::string> order()
    {
        session->updateSnapshotNow();
        std::vector<std::string> out;
        for (const auto& t : session->snapshot()->tracks)
            out.push_back(t.name);
        return out;
    }

    void choose(std::set<std::uint64_t> tracks, std::uint64_t channel)
    {
        shell->arrangement().setSelection({.channel = channel, .rowTracks = std::move(tracks)});
        sync();
    }
};

constexpr int kCtrl = juce::ModifierKeys::commandModifier, kShift = juce::ModifierKeys::shiftModifier;
} // namespace

TEST_CASE(
    "several tracks: Ctrl+click and Shift+click choose them in the headers and strips; a click on one of "
    "them selects it alone; TRACKS in the inspector",
    "[integration]")
{
    Several f;
    auto& view = f.shell->arrangement();
    click(view, f.header(f.kick));
    CHECK(view.selection().channel == f.kick);
    CHECK(view.selection().rowTracks.empty());

    // Ctrl+click: Hats with Kick, Hats the channel; the gang follows.
    click(view, f.header(f.hats), 1, kCtrl);
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.kick, f.hats});
    CHECK(view.selection().channel == f.hats);
    CHECK(f.session->gangedWith(f.kick) == std::vector<std::uint64_t>{f.hats});
    const auto ctx =
        fd::model::panelContext(view.selection(), *f.session->snapshot(), fd::model::CentreView::Arrange);
    CHECK(ctx.trackCount == 2);
    CHECK(ctx.inspector.front() == fd::model::InspectorSection::Tracks);
    // Shift+click: the run from Hats to Bass.
    click(view, f.header(f.bass), 1, kShift);
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.hats, f.bass});
    // Ctrl+click Snare in, then out again.
    click(view, f.header(f.snare), 1, kCtrl);
    CHECK(view.selection().rowTracks.size() == 3);
    click(view, f.header(f.snare), 1, kCtrl);
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.hats, f.bass});
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "several-tracks@2x.png",
                f.shell->createComponentSnapshot(f.shell->getLocalBounds(), true, 2.0f));
    // A click on one of them, released without a drag: it alone.
    click(view, f.header(f.bass));
    CHECK(view.selection().rowTracks.empty());
    CHECK(view.selection().channel == f.bass);
    CHECK(f.session->gangedWith(f.bass).empty());

    // The mixer's strips: the same.
    REQUIRE(f.shell->actions().perform("view.mixer"));
    auto& mixer = f.shell->mixer();
    REQUIRE(mixer.onChoose);
    CHECK_FALSE(mixer.onChoose(f.kick, {}));
    mixer.onChoose(f.snare, juce::ModifierKeys(kCtrl));
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.kick, f.snare});
    mixer.onChoose(f.bass, juce::ModifierKeys(kShift));
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.snare, f.hats, f.bass});
    CHECK(mixer.onChoose(f.hats, {})); // one of them: kept, for a drag
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "several-strips@2x.png",
                f.shell->createComponentSnapshot(f.shell->getLocalBounds(), true, 2.0f));
    REQUIRE(f.shell->actions().perform("view.mixer"));
}

TEST_CASE("several tracks: one's volume, pan, send, mute, solo and arm move them all, one step each",
          "[integration]")
{
    Several f;
    f.session->setVolume(f.snare, -6.0f);
    REQUIRE(f.session->setSend(f.kick, f.ret, -12.0f, std::nullopt));
    REQUIRE(f.session->setSend(f.snare, f.ret, -20.0f, std::nullopt));
    f.sync();
    f.choose({f.kick, f.snare, f.hats}, f.kick);

    // Volume dragged on the Kick: each moves by as many dB; the Bass stays.
    fd::app::VolumeBar volume(*f.session, f.kick);
    volume.onGestureStart();
    volume.onValueChange(static_cast<float>(fd::model::volumePosition(-3.0)));
    volume.onValueChange(static_cast<float>(fd::model::volumePosition(-4.0)));
    volume.onGestureEnd();
    CHECK(f.info(f.kick).mix.volumeDb == Approx(-4.0f).margin(0.01f));
    CHECK(f.info(f.snare).mix.volumeDb == Approx(-10.0f).margin(0.01f));
    CHECK(f.info(f.hats).mix.volumeDb == Approx(-4.0f).margin(0.01f));
    CHECK(f.info(f.bass).mix.volumeDb == Approx(0.0f).margin(0.01f));
    CHECK(f.session->undoDescription() == "Volume");
    f.session->undo();
    CHECK(f.info(f.snare).mix.volumeDb == Approx(-6.0f).margin(0.01f));
    CHECK(f.info(f.kick).mix.volumeDb == Approx(0.0f).margin(0.01f));
    // Typed: the same, from where they are.
    volume.onValueChange(static_cast<float>(fd::model::volumePosition(-12.0)));
    CHECK(f.info(f.snare).mix.volumeDb == Approx(-18.0f).margin(0.01f));
    f.session->undo();
    CHECK(f.info(f.hats).mix.volumeDb == Approx(0.0f).margin(0.01f));

    // Pan, and a send.
    fd::app::PanBar pan(*f.session, f.hats);
    pan.onGestureStart();
    pan.onValueChange(0.75f); // 50R
    pan.onGestureEnd();
    CHECK(f.info(f.kick).mix.pan == Approx(0.5f));
    CHECK(f.info(f.snare).mix.pan == Approx(0.5f));
    CHECK(f.info(f.bass).mix.pan == Approx(0.0f).margin(1e-4f));
    fd::app::SendBar send(*f.session, f.kick, f.ret);
    send.onGestureStart();
    send.onValueChange(static_cast<float>(fd::model::sendPosition(-6.0)));
    send.onGestureEnd();
    const auto level = [&](std::uint64_t t) { return f.info(t).mix.sends.at(0).levelDb; };
    CHECK(level(f.kick) == Approx(-6.0f).margin(0.05f));
    CHECK(level(f.snare) == Approx(-14.0f).margin(0.05f));

    // Mute, solo and arm: the same state on each, one step.
    fd::app::ChannelToggle mute(*f.session, fd::app::ChannelToggle::Kind::Mute, f.snare);
    mute.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(f.info(f.kick).mix.mute);
    CHECK(f.info(f.hats).mix.mute);
    CHECK_FALSE(f.info(f.bass).mix.mute);
    CHECK(f.session->undoDescription() == "Mute");
    f.session->undo();
    CHECK_FALSE(f.info(f.kick).mix.mute);
    fd::app::ChannelToggle solo(*f.session, fd::app::ChannelToggle::Kind::Solo, f.kick);
    solo.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(f.info(f.snare).mix.solo);
    CHECK(f.info(f.hats).mix.solo);
    CHECK_FALSE(f.info(f.bass).mix.solo);
    fd::app::ChannelToggle arm(*f.session, fd::app::ChannelToggle::Kind::Arm, f.kick);
    arm.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(f.info(f.snare).input.armed);
    CHECK(f.info(f.hats).input.armed);
    CHECK_FALSE(f.info(f.bass).input.armed);

    // Alt+M: every one chosen, as the selected one's new state; its label says so.
    const auto& actions = f.shell->actions();
    CHECK(actions.label("track.mute") == "Mute selected tracks");
    REQUIRE(actions.perform("track.mute"));
    CHECK(f.info(f.kick).mix.mute);
    CHECK(f.info(f.snare).mix.mute);
    CHECK_FALSE(f.info(f.bass).mix.mute);
}

TEST_CASE(
    "several tracks: Delete and Duplicate act on them all in one step; a dragged header moves them together",
    "[integration]")
{
    Several f;
    auto& view = f.shell->arrangement();
    const auto& actions = f.shell->actions();
    const std::vector<std::string> start{"Kick", "Snare", "Hats", "Bass"};
    REQUIRE(f.order() == start);

    // Delete: both go, the toast says how many, one undo brings them back.
    f.choose({f.kick, f.hats}, f.hats);
    CHECK(actions.label("track.delete") == "Delete tracks");
    REQUIRE(actions.perform("track.delete"));
    CHECK(f.order() == std::vector<std::string>{"Snare", "Bass"});
    CHECK(f.session->undoDescription() == "Delete tracks");
    CHECK(f.shell->lastWarning() == "Deleted 2 tracks");
    f.session->undo();
    CHECK(f.order() == start);

    // Duplicate: each copy after its original, the copies chosen together.
    f.sync();
    f.choose({f.kick, f.hats}, f.hats);
    REQUIRE(actions.perform("track.duplicate"));
    CHECK(f.order() == std::vector<std::string>{"Kick", "Kick 2", "Snare", "Hats", "Hats 2", "Bass"});
    CHECK(f.session->undoDescription() == "Duplicate tracks");
    CHECK(view.selection().rowTracks.size() == 2);
    f.session->undo();
    CHECK(f.order() == start);

    // Dragged by the Kick's header below the Bass: both move, in their order, in one step.
    f.sync();
    f.choose({f.kick, f.hats}, f.hats);
    const auto from = f.header(f.kick);
    const auto below = juce::Point<float>(from.x, f.header(f.bass).y + 40.0f);
    view.mouseDown(mouseEvent(view, from, from, false));
    CHECK(view.selection().rowTracks.size() == 2); // the press kept them
    view.mouseDrag(mouseEvent(view, below, from, true));
    REQUIRE(view.rowDrop().has_value());
    view.mouseUp(mouseEvent(view, below, from, true));
    CHECK(f.order() == std::vector<std::string>{"Snare", "Bass", "Kick", "Hats"});
    CHECK(f.session->undoDescription() == "Move tracks");
    f.session->undo();
    CHECK(f.order() == start);
}
