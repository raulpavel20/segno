// Groups through the real shell (ADR 0072, docs/design/groups.md §5, §6, §9–§12, step 2): Ctrl+G
// and Ctrl+Shift+G shared with racks; the fold triangle, Alt+click and Alt+U in each view on its
// own, saved with the project; a header dragged into and out of groups by the pointer's depth and
// onto a group's header; a strip dragged into a group; the header's menus; the inspector's Group
// row; the toasts saying what went; Add track and browser drops on a group; screenshots for review.

#include "ShellTestKit.h"
#include "fd/testkit/WavIO.h"
#include "model/ArrangementRows.h"
#include "model/Groups.h"
#include "ui/BrowserDrops.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::testing;

namespace
{
/// Kick, Snare, Bass and Lead, each with a clip; Kick and Snare in Drums.
struct Grouped : ShellFixture
{
    juce::File root;
    std::uint64_t kick = 0, snare = 0, bass = 0, lead = 0, drums = 0;

    Grouped()
    {
        shell->setSize(1800, 1000);
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("fd-group-shell-" + juce::Uuid().toString());
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
        std::uint64_t* ids[] = {&kick, &snare, &bass, &lead};
        const char* names[] = {"Kick", "Snare", "Bass", "Lead"};
        for (size_t i = 0; i < 4; ++i)
        {
            *ids[i] = tracks[i].id;
            session->renameTrack(tracks[i].id, names[i]);
            REQUIRE(session->createMidiClip(tracks[i].id, 0.0, 2.0) != 0);
        }
        drums = session->groupTracks({kick, snare}, "Drums");
        REQUIRE(drums != 0);
        sync();
    }

    ~Grouped()
    {
        shell.reset();
        session.reset();
        root.deleteRecursively();
    }

    void sync()
    {
        session->updateSnapshotNow();
        shell->arrangement().setSnapshot(session->snapshot());
        shell->mixer().setSnapshot(session->snapshot());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    const fd::model::TrackInfo* info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        return session->snapshot()->channel(id);
    }

    [[nodiscard]] int rowOf(std::uint64_t channel)
    {
        const auto& rows = shell->arrangement().rows();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (!rows[static_cast<size_t>(i)].isLane() && rows[static_cast<size_t>(i)].channel == channel)
                return i;
        return -1;
    }

    /// A key, as pressed in the shell.
    bool press(int key, int mods)
    {
        return shell->keyPressed(juce::KeyPress(key, juce::ModifierKeys(mods), 0));
    }

    /// Every component of type T under `c`.
    template <typename T> static std::vector<T*> findAll(juce::Component& c)
    {
        std::vector<T*> out;
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*>(child))
                out.push_back(t);
            const auto inner = findAll<T>(*child);
            out.insert(out.end(), inner.begin(), inner.end());
        }
        return out;
    }

    fd::app::ChannelStrip* strip(std::uint64_t channel)
    {
        for (auto* s : findAll<fd::app::ChannelStrip>(shell->mixer()))
            if (s->channel() == channel)
                return s;
        return nullptr;
    }
};

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
} // namespace

TEST_CASE("groups in the shell: Ctrl+G groups the tracks chosen, Ctrl+Shift+G ungroups saying what went; the "
          "chain used last, the keys are the rack's",
          "[integration]")
{
    Grouped g;
    auto& view = g.shell->arrangement();
    // Bass and Lead chosen, the headers used last: Ctrl+G groups them, the group selected.
    view.setSelection(fd::model::Selection{.channel = g.lead, .rowTracks = {g.bass, g.lead}});
    REQUIRE(g.press('g', kCmd));
    g.sync();
    const auto made = view.selection().channel;
    REQUIRE(made);
    const auto* group = g.info(*made);
    REQUIRE(group != nullptr);
    CHECK(group->isGroup());
    CHECK(fd::model::tracksIn(*g.session->snapshot(), *made) == 2);
    CHECK_FALSE(view.foldedGroups().contains(*made));
    // Ctrl+Shift+G with a module on it: the toast says the module is gone.
    REQUIRE(g.session->addDevice(*made, "fd:compressor") != 0);
    g.sync();
    REQUIRE(g.press('g', kCmd | kShift));
    g.sync();
    CHECK(g.info(*made) == nullptr);
    CHECK(g.shell->lastWarning() == "Ungrouped 1 Group: its module is gone");
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{g.bass, g.lead});

    // The keys go by what was used last: the chain, the rack's (its own menu item stays its own).
    const auto* rackGroup = g.shell->actions().find("module.groupIntoRack");
    REQUIRE(rackGroup != nullptr);
    REQUIRE(rackGroup->keyGoesTo);
    CHECK(rackGroup->keyGoesTo() == "track.group");
    CHECK(g.shell->actions().shortcut("track.group") == g.shell->actions().shortcut("module.groupIntoRack"));
    CHECK(g.shell->actions().shortcut("track.ungroup") == g.shell->actions().shortcut("module.ungroupRack"));
}

TEST_CASE(
    "groups in the shell: the fold triangle folds in its view alone; Alt+click, Alt+U and View fold every "
    "group or one; each view's folds are saved",
    "[integration]")
{
    Grouped g;
    auto& view = g.shell->arrangement();
    auto& mixer = g.shell->mixer();
    const auto rows = view.rows().size();
    const auto fold = view.foldArea(g.rowOf(g.drums));
    REQUIRE(fold);
    CHECK_FALSE(view.foldArea(g.rowOf(g.kick)));
    click(view, fold->getCentre().toFloat());
    CHECK(view.foldedGroups() == std::set<std::uint64_t>{g.drums});
    CHECK(view.rows().size() == rows - 2);
    CHECK(mixer.foldedGroups().empty()); // the mixer folds on its own
    click(view, fold->getCentre().toFloat());
    CHECK(view.foldedGroups().empty());

    // A second group: Alt+click folds both.
    const auto keys = g.session->groupTracks({g.bass}, "Keys");
    g.sync();
    click(view, view.foldArea(g.rowOf(g.drums))->getCentre().toFloat(), 1, kAlt);
    CHECK(view.foldedGroups() == std::set<std::uint64_t>{g.drums, keys});
    // Alt+U on the selected group, ticked in the Track menu while folded.
    view.selectTrack(keys);
    REQUIRE(g.press('u', kAlt));
    CHECK(view.foldedGroups() == std::set<std::uint64_t>{g.drums});
    CHECK_FALSE(g.shell->actions().isToggled("track.fold"));
    REQUIRE(g.shell->actions().perform("view.unfoldGroups"));
    CHECK(view.foldedGroups().empty());
    // In the mixer view, the mixer's.
    REQUIRE(g.shell->actions().perform("view.mixer"));
    REQUIRE(g.shell->actions().perform("view.foldGroups"));
    CHECK(mixer.foldedGroups() == std::set<std::uint64_t>{g.drums, keys});
    CHECK(view.foldedGroups().empty());
    REQUIRE(g.shell->actions().perform("view.mixer"));

    // Saved with the project, each view's own; never an undo step.
    const auto undo = g.session->undoDescription();
    view.setFoldedGroups({g.drums});
    CHECK(g.session->undoDescription() == undo);
    const auto file = g.root.getChildFile("Folds").getChildFile("Folds.segno");
    REQUIRE(file.getParentDirectory().createDirectory());
    REQUIRE(g.session->saveAs(file));
    view.setFoldedGroups({});
    mixer.setFoldedGroups({});
    REQUIRE(g.session->open(file));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(view.foldedGroups() == std::set<std::uint64_t>{g.drums});
    CHECK(mixer.foldedGroups() == std::set<std::uint64_t>{g.drums, keys});
    g.session->newProject(); // none folded
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(view.foldedGroups().empty());
    CHECK(mixer.foldedGroups().empty());
}

TEST_CASE(
    "groups in the shell: a header dragged in by the pointer's depth, out of a group, onto a group's header; "
    "a group left empty goes, the toast saying so",
    "[integration]")
{
    Grouped g;
    auto& view = g.shell->arrangement();
    const auto rowArea = [&](std::uint64_t channel) { return view.nameArea(g.rowOf(channel))->toFloat(); };
    // Rows: Drums, Kick, Snare, Bass, Lead. Lead dropped at Drums' end (above Bass): the pointer
    // far right lands it inside, at the far left after Drums.
    const auto top = [&](std::uint64_t channel)
    { return static_cast<float>(view.nameArea(g.rowOf(channel))->getY()); };
    CHECK_FALSE(view.rowDrop()); // none yet
    const auto from = rowArea(g.lead).getCentre();
    const float bassTop = top(g.bass) - 6.0f; // just above its name: nearest its top
    view.mouseDown(mouseEvent(view, from, from, false));
    view.mouseDrag(mouseEvent(view, {200.0f, bassTop}, from, true));
    auto drop = view.rowDrop();
    REQUIRE(drop);
    CHECK(drop->group == g.drums);
    CHECK(drop->depth == 1);
    view.mouseDrag(mouseEvent(view, {2.0f, bassTop}, from, true));
    drop = view.rowDrop();
    REQUIRE(drop);
    CHECK(drop->group == 0);
    view.mouseDrag(mouseEvent(view, {200.0f, bassTop}, from, true));
    view.mouseUp(mouseEvent(view, {200.0f, bassTop}, from, true));
    CHECK(g.info(g.lead)->group == g.drums);
    g.sync();

    // Bass onto Drums' header: at its end.
    // The row's middle: halfway from its name to the next row's.
    const float drumsMiddle = (top(g.drums) + top(g.kick)) * 0.5f;
    drag(view, rowArea(g.bass).getCentre(), {rowArea(g.drums).getCentreX(), drumsMiddle});
    CHECK(g.info(g.bass)->group == g.drums);
    g.sync();
    const auto members = fd::model::membersOf(*g.session->snapshot(), g.drums, false);
    REQUIRE(members.size() == 4);
    CHECK(members.back()->id == g.bass);

    // Every track out of Drums: it goes in the same step, and the toast says so.
    REQUIRE(g.session->removeFromGroup({g.kick, g.snare, g.lead, g.bass}));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(g.info(g.drums) == nullptr);
    CHECK(g.shell->lastWarning() == "Drums went too: nothing was left in it");
}

TEST_CASE("groups in the shell: a drag across a folded group's row selects that time on everything in it; "
          "Ctrl+click takes it out",
          "[integration]")
{
    Grouped g;
    auto& view = g.shell->arrangement();
    view.setFoldedGroups({g.drums});
    g.sync();
    const auto name = view.nameArea(g.rowOf(g.drums));
    REQUIRE(name);
    const float y = static_cast<float>(name->getCentreY());
    const auto x = [&](double seconds)
    {
        return static_cast<float>(fd::app::ArrangementView::headerWidth()) +
               static_cast<float>(view.timeline().xForTime(seconds));
    };
    const float x0 = x(2.0);
    drag(view, {x0, y}, {x(6.0), y});
    const auto& sel = view.selection();
    REQUIRE(sel.hasTime());
    CHECK(sel.rowTracks == std::set<std::uint64_t>{g.drums, g.kick, g.snare});
    // Ctrl+click on the row: out with everything in it, the selection then empty.
    click(view, {x(4.0), y}, 1, kCmd);
    CHECK_FALSE(view.selection().hasTime());
}

TEST_CASE("groups in the shell: a strip dragged into a group, its middle taking it in; the mixer's triangle",
          "[integration]")
{
    Grouped g;
    REQUIRE(g.shell->actions().perform("view.mixer"));
    g.sync();
    auto* lead = g.strip(g.lead);
    auto* drums = g.strip(g.drums);
    REQUIRE(lead != nullptr);
    REQUIRE(drums != nullptr);
    // Onto the Drums strip's middle: at its end.
    const auto grab = lead->nameArea().getCentre().toFloat();
    const auto onDrums = drums->getBounds().getCentre().toFloat() - lead->getPosition().toFloat();
    drag(*lead, grab, onDrums);
    CHECK(g.info(g.lead)->group == g.drums);
    g.sync();
    // The triangle folds the mixer's Drums alone.
    drums = g.strip(g.drums);
    REQUIRE(drums != nullptr);
    REQUIRE_FALSE(drums->foldArea().isEmpty());
    click(*drums, drums->foldArea().getCentre().toFloat());
    CHECK(g.shell->mixer().foldedGroups() == std::set<std::uint64_t>{g.drums});
    CHECK(g.shell->arrangement().foldedGroups().empty());
    g.sync();
    CHECK((g.strip(g.kick) == nullptr || !g.strip(g.kick)->isVisible()));
    CHECK(g.shell->mixer().shownStrips() == std::vector<std::uint64_t>{g.drums, g.bass});
}

TEST_CASE(
    "groups in the shell: the header's menus; Delete a group saying what went; Add track in a group; the "
    "inspector's Group row selects it; live MIDI stays put",
    "[integration]")
{
    Grouped g;
    auto& view = g.shell->arrangement();
    REQUIRE(view.onHeaderMenu);
    juce::PopupMenu groupMenu, memberMenu, trackMenu;
    view.selectTrack(g.drums);
    view.onHeaderMenu(groupMenu, g.drums);
    CHECK(itemsOf(groupMenu).joinIntoString("|").startsWith(juce::String::fromUTF8(
        "Rename\xE2\x80\xA6|Fold group|Group tracks|Ungroup tracks|Duplicate track|Delete track")));
    CHECK(itemsOf(groupMenu).contains("Give tracks the group's colour"));
    view.selectTrack(g.kick);
    view.onHeaderMenu(memberMenu, g.kick);
    CHECK(itemsOf(memberMenu).contains("Remove from group"));
    view.selectTrack(g.bass);
    view.onHeaderMenu(trackMenu, g.bass);
    CHECK(itemsOf(trackMenu).contains("Group tracks"));
    CHECK_FALSE(itemsOf(trackMenu).contains("Remove from group"));

    // Live MIDI follows Bass, with an instrument; selecting Drums leaves it there.
    REQUIRE(g.session->addDevice(g.bass, "fd:sampler") != 0);
    g.sync();
    view.selectTrack(g.bass);
    g.sync();
    CHECK(g.session->midiInputTarget() == std::optional(g.bass));
    view.selectTrack(g.drums);
    g.sync();
    CHECK(g.session->midiInputTarget() == std::optional(g.bass));

    // The inspector's Group row on Kick: a click selects Drums.
    view.selectTrack(g.kick);
    g.sync();
    auto& link = g.shell->inspector().groupLink();
    CHECK(link.isVisible());
    CHECK(link.getButtonText() == "Drums");
    link.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(view.selection().channel == g.drums);

    // Add track with Kick selected: at Drums' end.
    view.selectTrack(g.kick);
    REQUIRE(g.shell->actions().perform("track.add"));
    g.sync();
    CHECK(fd::model::membersOf(*g.session->snapshot(), g.drums, false).size() == 3);

    // Delete Drums: everything in it, the toast says so.
    view.selectTrack(g.drums);
    REQUIRE(g.shell->actions().perform("track.delete"));
    CHECK(g.info(g.drums) == nullptr);
    CHECK(g.info(g.kick) == nullptr);
    CHECK(g.shell->lastWarning() == "Deleted Drums and its 3 tracks");
}

TEST_CASE(
    "groups in the shell: from the browser, a sample or an instrument on a group's row makes a track in "
    "it; an effect goes on its chain; an instrument in its chain is refused",
    "[integration]")
{
    Grouped g;
    namespace browser = fd::app::browser;
    fd::testkit::AudioBuffer tone(2, 4800);
    for (int i = 0; i < tone.numSamples(); ++i)
        tone.at(0, i) = tone.at(1, i) = 0.1f;
    const auto wav = g.root.getChildFile("Hat.wav");
    REQUIRE(fd::testkit::writeWav(wav.getFullPathName().toStdString(), tone, 48000.0));
    const auto sample = "file:" + wav.getFullPathName().toStdString();

    auto r = browser::onChannel(*g.session, sample, g.drums, 1.0);
    REQUIRE(r.done);
    REQUIRE(r.track != 0);
    CHECK(g.info(r.track)->group == g.drums);
    CHECK(g.info(r.track)->clips.size() == 1);
    r = browser::onChannel(*g.session, "module:fd:sampler", g.drums, 0.0);
    REQUIRE(r.done);
    CHECK(g.info(r.track)->group == g.drums);
    r = browser::onChannel(*g.session, "module:fd:compressor", g.drums, 0.0);
    REQUIRE(r.done);
    CHECK(r.track == 0);
    CHECK(g.info(g.drums)->devices.size() == 1);
    r = browser::inChain(*g.session, "module:fd:sampler", g.drums, -1, std::nullopt);
    CHECK_FALSE(r.done);
    CHECK(r.refused.isNotEmpty());
    CHECK(g.info(g.drums)->devices.size() == 1);
}

TEST_CASE("groups in the shell: screenshots of real groups for review", "[integration]")
{
    const char* dump = std::getenv("FD_DUMP_DIR");
    if (dump == nullptr)
        return;
    Grouped g;
    const auto keys = g.session->groupTracks({g.bass}, "Keys");
    REQUIRE(g.session->addDevice(g.drums, "fd:compressor") != 0);
    g.sync();
    auto& view = g.shell->arrangement();
    view.setFoldedGroups({keys});
    view.selectTrack(g.kick);
    g.sync();
    for (const int width : {1800, 1100})
    {
        g.shell->setSize(width, 1000);
        g.sync();
        dumpPng(dump, "group-shell-arrangement-" + juce::String(width) + ".png",
                g.shell->createComponentSnapshot(g.shell->getLocalBounds(), true, 2.0f));
    }
    g.shell->setSize(1800, 1000);
    REQUIRE(g.shell->actions().perform("view.mixer"));
    g.sync();
    dumpPng(dump, "group-shell-mixer.png",
            g.shell->createComponentSnapshot(g.shell->getLocalBounds(), true, 2.0f));
}
