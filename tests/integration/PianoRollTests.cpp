// The piano roll, stage P4a (ADR 0058, docs/design/piano-roll.md §18): writing in the key, the
// chord lane and the Chord popover, drawing chords, other tracks' notes, the time handles, the
// velocity line, muting, the Notes menu from the header, Quantise…, and the header at standard
// and compact widths; each drawn for review with FD_DUMP_DIR set.

#include "ShellTestKit.h"

#include "model/MenuBar.h"
#include "model/Selection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::testing;
using fd::music::Key;

namespace
{
/// A shell with an instrument track and a four-bar MIDI clip open in the clip view.
struct RollFixture : ShellFixture
{
    std::uint64_t track = 0, clipId = 0;

    explicit RollFixture(int width = 1800, int height = 1000)
    {
        shell->setSize(width, height);
        track = session->snapshot()->tracks.front().id;
        REQUIRE(session->addDevice(track, "fd:subtractive-synth") != 0); // previews need something to play them
        clipId = session->createMidiClip(track, 0.0, 8.0); // four bars at 120 BPM
        REQUIRE(clipId != 0);
        open();
    }

    /// Selects the clip so the clip view shows it, as a click on it does.
    void open()
    {
        session->updateSnapshotNow();
        shell->arrangement().setSelection(fd::model::withClips(*session->snapshot(), {clipId}, track, track));
        shell->finishAnimations();
        refresh();
        REQUIRE(shell->detail().showsPianoRoll());
    }

    /// The snapshot after a change, in the clip view (as the snapshot callback does).
    void refresh()
    {
        session->updateSnapshotNow();
        shell->detail().setClip(clip(), session->snapshot()->tempo);
    }

    [[nodiscard]] fd::model::ClipInfo clip() const
    {
        for (const auto& t : session->snapshot()->tracks)
            for (const auto& c : t.clips)
                if (c.id == clipId)
                    return c;
        FAIL("no clip");
        return {};
    }

    [[nodiscard]] std::vector<fd::model::Note> notes()
    {
        session->updateSnapshotNow();
        return clip().notes;
    }

    void setNotes(const std::vector<fd::model::Note>& notes)
    {
        session->setNotes(clipId, notes);
        refresh();
    }

    fd::app::PianoRoll& roll() { return shell->detail().pianoRoll(); }

    /// Presses a key in the shell, as typed.
    bool press(int code, int mods = 0) { return shell->keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0)); }

    /// The detail panel, for review.
    void dump(const char* name, juce::Rectangle<int> area = {})
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            auto& detail = shell->detail();
            detail.frame(0.0); // the grid's step in the header, as a frame shows it
            dumpPng(dir, name, detail.createComponentSnapshot(area.isEmpty() ? detail.getLocalBounds() : area, true, 2.0f));
        }
    }
};

std::vector<fd::model::Note> chordNotes(std::vector<int> pitches, double start, double length = 4.0)
{
    std::vector<fd::model::Note> out;
    for (int p : pitches)
        out.push_back({p, start, length, 96.0f / 127.0f});
    return out;
}

std::vector<fd::model::Note> progression()
{
    // Am, F, C/G, G7 in A minor, a bar each.
    std::vector<fd::model::Note> notes;
    for (const auto& [pitches, start] : {std::pair{std::vector<int>{57, 60, 64}, 0.0}, std::pair{std::vector<int>{53, 57, 60}, 4.0},
                                         std::pair{std::vector<int>{55, 60, 64}, 8.0}, std::pair{std::vector<int>{55, 59, 62, 65}, 12.0}})
        for (const auto& n : chordNotes(pitches, start))
            notes.push_back(n);
    std::sort(notes.begin(), notes.end(), fd::model::noteOrder);
    return notes;
}

juce::String nameOf(const fd::music::Chord& chord, const Key& key)
{
    return juce::String::fromUTF8(fd::music::chordName(chord, &key).c_str());
}
} // namespace

TEST_CASE("piano roll: in the key, drawing lands on its notes, the arrows move by its steps, Fold shows its rows",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE_FALSE(roll.snapsToKey()); // no key: everything chromatic, as before
    REQUIRE(f.session->setKey(Key{9, "minor"}));
    f.refresh();
    REQUIRE(roll.snapsToKey()); // Key is on by default

    // A double-click on G♯ draws the key's nearest note, G (a tie: down).
    const double step = roll.timeline().gridStepBeats(f.session->snapshot()->tempo);
    doubleClick(roll, roll.pointFor(0.1, 68));
    REQUIRE(f.notes().size() == 1);
    CHECK(f.notes()[0].pitch == 67);
    // Alt draws as clicked.
    doubleClick(roll, roll.pointFor(1.1, 68), juce::ModifierKeys::altModifier);
    REQUIRE(f.notes().size() == 2);
    CHECK(f.notes()[1].pitch == 68);
    f.session->undo();
    f.refresh();

    // ↑ moves by a step of the scale (G → A), Ctrl+↑ by a semitone, Shift+↑ by an octave.
    click(roll, roll.pointFor(step * 0.5, 67));
    REQUIRE(roll.selection().size() == 1);
    CHECK(f.press(juce::KeyPress::upKey));
    CHECK(f.notes()[0].pitch == 69);
    CHECK(f.press(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier));
    CHECK(f.notes()[0].pitch == 70);
    CHECK(f.press(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
    CHECK(f.notes()[0].pitch == 82);
    CHECK(f.press(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier));

    // Fit to key: A♯ goes to the nearer of A and B; a tie with nothing before it: down.
    REQUIRE(f.shell->actions().perform("notes.fitToKey"));
    CHECK(f.notes()[0].pitch == 69);
    // Dragged with Key, a note moves through the key's notes.
    const auto from = roll.pointFor(step * 0.5, 69);
    drag(roll, from, roll.pointFor(step * 0.5, 70));
    CHECK(f.notes()[0].pitch == 69); // A♯ isn't the key's: still nearest A
    drag(roll, from, roll.pointFor(step * 0.5, 71));
    CHECK(f.notes()[0].pitch == 71);

    // Fold to the key: only its rows, each labelled with its step; drawing and moving go by them.
    f.setNotes(progression());
    roll.setFold(fd::model::Fold::Key);
    const auto rows = roll.foldedRows();
    REQUIRE_FALSE(rows.empty());
    for (int p : rows)
        CHECK(fd::music::contains(Key{9, "minor"}, p));
    CHECK(f.shell->detail().foldChoice().value() == "Fold to key");
    f.dump("piano-roll-fold-key@2x.png", f.shell->detail().getLocalBounds().removeFromLeft(760));
    roll.setFold(fd::model::Fold::Notes);
    CHECK(roll.foldedRows() == std::vector<int>{53, 55, 57, 59, 60, 62, 64, 65});
    roll.setFold(fd::model::Fold::All);
    CHECK(roll.foldedRows().empty());
}

TEST_CASE("piano roll: the chord lane names the chords; a click selects one; the Chord popover writes and re-voices",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    const Key aMinor{9, "minor"};
    REQUIRE(f.session->setKey(aMinor));
    f.setNotes(progression());
    REQUIRE(roll.showsChordLane());
    const auto& spans = roll.chordSpans();
    REQUIRE(spans.size() == 4);
    CHECK(nameOf(spans[0].chord, aMinor) == "Am");
    CHECK(nameOf(spans[1].chord, aMinor) == "F");
    CHECK(nameOf(spans[2].chord, aMinor) == "C/G");
    CHECK(nameOf(spans[3].chord, aMinor) == "G7");
    CHECK(juce::String::fromUTF8(fd::music::romanNumeral(spans[3].chord, aMinor).c_str()) == "VII7");

    // A click on a name selects that chord's notes.
    const auto lane = roll.chordLaneArea();
    const auto inLane = [&](double beat)
    { return juce::Point<float>(roll.pointFor(beat, 60).x, static_cast<float>(lane.getY() + lane.getHeight() / 2)); };
    click(roll, inLane(5.0));
    CHECK(roll.selection() == std::set<std::size_t>(spans[1].notes.begin(), spans[1].notes.end()));
    f.dump("piano-roll-chords@2x.png");

    // A double-click on G7 opens the Chord popover for it: the key's chords, those after C/G, borrowed.
    doubleClick(roll, inLane(13.0));
    auto& popover = f.shell->chordPopover();
    REQUIRE(popover.isOpen());
    REQUIRE(popover.place());
    CHECK(popover.place()->replacing.size() == 4);
    const auto& groups = popover.groups();
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].title == "In the key");
    CHECK(groups[1].title == "Next, after C/G");
    CHECK(groups[2].title == "Borrowed");
    juce::StringArray inKey;
    for (const auto& c : groups[0].chords)
        inKey.add(nameOf(c, aMinor));
    CHECK(inKey == juce::StringArray{"Am", juce::String::fromUTF8("B°"), "C", "Dm", "Em", "F", "G"});
    // The chord there is lit; the popover sits beside the place, inside the window, nothing touching.
    const auto buttons = popover.chordButtons();
    const auto lit = std::count_if(buttons.begin(), buttons.end(), [](auto* b) { return b->getToggleState(); });
    CHECK(lit == 0); // G7 isn't offered as a triad
    CHECK(f.shell->getLocalBounds().contains(
        popover.getBounds().reduced(juce::roundToInt(fd::ui::Theme::current().shell.shadowRadius))));
    CHECK(crowded(popover, fd::ui::Theme::current().shell.itemGap).isEmpty());

    // Hovering E plays it and shows its notes in place; nothing is kept.
    const fd::music::Chord e{.root = 4, .kind = 0};
    popover.hover(e);
    CHECK(f.notes() == progression());
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-chord-popover@2x.png",
                f.shell->createComponentSnapshot(f.shell->getLocalArea(&f.shell->detail(), f.shell->detail().getLocalBounds()), true, 2.0f));
    popover.hover(std::nullopt);

    // A click writes it: E (doubled to G7's four notes), voice-led from C/G, one undo step.
    popover.choose(e);
    const auto bar4 = [&]
    {
        std::vector<int> pitches;
        for (const auto& n : f.notes())
            if (n.start >= 12.0)
                pitches.push_back(n.pitch);
        std::sort(pitches.begin(), pitches.end());
        return pitches;
    };
    const auto written = bar4();
    REQUIRE(written.size() == 4);
    CHECK(fd::music::nameChord(written)->root == 4);
    // Kind 7th re-writes it as the key's seventh on E (Em7 in A minor).
    popover.kind().setSelected(1);
    popover.kind().onChange(1);
    CHECK(fd::music::pitchClasses(*fd::music::nameChord(bar4())) ==
          fd::music::pitchClasses(fd::music::Chord{.root = 4, .kind = 8})); // E G B D, in any inversion
    popover.close();
    CHECK_FALSE(popover.isOpen());
    f.session->undo();
    f.session->undo();
    f.refresh();
    CHECK(f.notes() == progression());
}

TEST_CASE("piano roll: chords drawn on a click, other tracks' notes faint, time handles, a velocity line, muting, Ctrl+drag",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{9, "minor"}));
    f.refresh();

    // Another track's melody over the clip: shown faint, never selectable.
    const auto lead = f.session->addTrack("Lead");
    const auto leadClip = f.session->createMidiClip(lead, 0.0, 8.0);
    f.session->setNotes(leadClip, {{76, 0.0, 1.5, 0.8f}, {79, 1.5, 0.5, 0.8f}, {81, 2.0, 2.0, 0.8f}, {77, 4.0, 4.0, 0.8f}});
    f.refresh();
    CHECK(roll.ghostCount() == 4);
    roll.setOthers(false);
    CHECK(roll.ghostCount() == 0);
    roll.setOthers(true);

    // Draw chords: a double-click writes the key's chord on the note, voice-led.
    roll.setDraw(fd::model::DrawMode::Chord);
    CHECK(roll.showsChordLane());
    doubleClick(roll, roll.pointFor(0.1, 69));
    REQUIRE(f.notes().size() == 3);
    CHECK(fd::music::nameChord(std::vector<int>{f.notes()[0].pitch, f.notes()[1].pitch, f.notes()[2].pitch})->root == 9);
    doubleClick(roll, roll.pointFor(4.1, 65));
    REQUIRE(f.notes().size() == 6);
    // Chord under: the note on top (C, after F: the key's C chord, the most usual next with it).
    roll.setDraw(fd::model::DrawMode::ChordUnder);
    doubleClick(roll, roll.pointFor(8.1, 72));
    std::vector<int> under;
    for (const auto& n : f.notes())
        if (n.start >= 8.0)
            under.push_back(n.pitch);
    REQUIRE(under.size() == 3);
    CHECK(*std::max_element(under.begin(), under.end()) == 72);
    CHECK(fd::music::nameChord(under)->root == 0);
    roll.setDraw(fd::model::DrawMode::Note);

    // Time handles over the selection: the end's dragged to twice as far stretches the timing.
    roll.selectAll();
    auto [left, right] = roll.timeHandles();
    REQUIRE_FALSE(right.isEmpty());
    const auto span = fd::model::notesSpan(roll.notes(), roll.selection());
    REQUIRE(span);
    const float twice = roll.pointFor(span->first + 2.0 * (span->second - span->first), 60).x;
    drag(roll, right.getCentre().toFloat(), {twice, static_cast<float>(right.getCentreY())});
    double last = 0.0;
    for (const auto& n : f.notes())
        last = std::max(last, n.start);
    CHECK(last == Catch::Approx(16.0));
    f.session->undo();
    f.refresh();

    // Shift+drag in the velocity lane: a straight line, low to high.
    roll.setSelection({});
    const auto lane = roll.velocityArea();
    drag(roll, {roll.pointFor(0.0, 60).x, static_cast<float>(lane.getBottom() - 6)},
         {roll.pointFor(8.0, 60).x, static_cast<float>(lane.getY() + 6)}, juce::ModifierKeys::shiftModifier);
    float firstVelocity = 1.0f, lastVelocity = 0.0f;
    for (const auto& n : f.notes())
    {
        if (n.start < 1.0)
            firstVelocity = std::min(firstVelocity, n.velocity);
        if (n.start >= 8.0)
            lastVelocity = std::max(lastVelocity, n.velocity);
    }
    CHECK(firstVelocity < 0.3f);
    CHECK(lastVelocity > 0.7f);

    // Mute (0): the selected notes, the menu item reading "Mute notes"; drawn hollow.
    click(roll, roll.pointFor(0.2, f.notes()[0].pitch));
    REQUIRE(roll.selection().size() == 1);
    CHECK(f.shell->actions().label("clip.mute") == "Mute notes");
    CHECK(f.press('0'));
    CHECK(f.notes()[0].muted);

    // Ctrl+drag copies the selection two beats later.
    const auto at = roll.pointFor(0.2, f.notes()[0].pitch);
    drag(roll, at, at + juce::Point<float>(roll.pointFor(2.0, 60).x - roll.pointFor(0.0, 60).x, 0.0f),
         juce::ModifierKeys::commandModifier);
    CHECK(f.notes().size() == 10);

    roll.selectAll();
    f.dump("piano-roll-p4@2x.png");
}

TEST_CASE("piano roll: the Notes menu opens from the header too; Quantise previews, applies as one step",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    auto& detail = f.shell->detail();
    f.setNotes({{60, 0.1, 0.5, 0.8f}, {64, 1.05, 0.5, 0.8f}, {67, 2.2, 0.5, 0.8f}});

    // One builder: the header's Notes button and the menu bar's Notes menu are the same.
    REQUIRE(detail.notesMenu);
    const auto& menus = f.shell->menus();
    int notesMenu = -1;
    for (int i = 0; i < static_cast<int>(menus.menus().size()); ++i)
        if (menus.menus()[static_cast<std::size_t>(i)].title == "Notes")
            notesMenu = i;
    REQUIRE(notesMenu >= 0);
    CHECK(itemsOf(detail.notesMenu()) == itemsOf(menus.menu(notesMenu)));
    CHECK(itemsOf(detail.notesMenu()).contains(juce::String::fromUTF8("Chords ▸ Drop 2 voicing")));
    CHECK(detail.notesButton().isVisible());

    // Quantise…: the notes move live as it's set; Escape puts them back; Apply keeps them.
    REQUIRE(f.shell->actions().perform("notes.quantiseSettings"));
    auto& quantise = f.shell->quantisePopover();
    REQUIRE(quantise.isOpen());
    quantise.setSettings({.strength = 0.5});
    CHECK(f.notes()[0].start == Catch::Approx(0.1)); // only previewed
    CHECK(roll.notes()[0].start == Catch::Approx(0.1));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-quantise@2x.png", f.shell->createComponentSnapshot(quantise.getBounds().expanded(40), true, 2.0f));
    CHECK(crowded(quantise, 4).isEmpty());
    quantise.apply();
    CHECK_FALSE(quantise.isOpen());
    CHECK(f.notes()[0].start == Catch::Approx(0.05));
    CHECK(roll.options().quantise.strength == Catch::Approx(0.5));
    // Ctrl+U quantises with the same settings.
    f.session->undo();
    f.refresh();
    click(roll, roll.pointFor(5.0, 72)); // the clip view used last
    CHECK(f.press('u', juce::ModifierKeys::commandModifier));
    CHECK(f.notes()[0].start == Catch::Approx(0.05));
}

TEST_CASE("piano roll: the clip view's header at standard and compact widths, options giving way in order",
          "[integration]")
{
    for (const int width : {1800, 1280, 1024, 820, 700, 600})
    {
        RollFixture f(width, 900);
        REQUIRE(f.session->setKey(Key{0, "major"}));
        f.setNotes(progression());
        auto& detail = f.shell->detail();
        auto& header = detail.header();
        detail.frame(0.0); // the grid's step in its chip, as a frame shows it
        INFO("width " << width << ", header " << header.getWidth());
        CHECK(detail.keyOption().isVisible());
        // Nothing touches: the options, the commands and the tabs at least the item gap apart.
        CHECK(crowded(header, fd::ui::Theme::current().shell.itemGap).isEmpty());
        for (auto* c : header.getChildren())
            if (c->isVisible() && !c->getBounds().isEmpty())
                CHECK(header.getLocalBounds().contains(c->getBounds()));
        // Others gives way first, then Fold, then Draw.
        const bool others = header.isDropped(detail.othersOption()), fold = header.isDropped(detail.foldChoice()),
                   draw = header.isDropped(detail.drawChoice());
        CHECK((!fold || others));
        CHECK((!draw || fold));
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            dumpPng(dir, "piano-roll-header-" + juce::String(width) + "@2x.png",
                    detail.createComponentSnapshot(header.getBounds(), true, 2.0f));
        if (width == 1800)
            CHECK_FALSE(others);
    }
}
