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
        REQUIRE(session->addDevice(track, "fd:subtractive-synth") !=
                0);                                        // previews need something to play them
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
    bool press(int code, int mods = 0)
    {
        return shell->keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0));
    }

    /// The detail panel, for review.
    void dump(const char* name, juce::Rectangle<int> area = {})
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            auto& detail = shell->detail();
            detail.frame(0.0); // the grid's step in the header, as a frame shows it
            dumpPng(
                dir, name,
                detail.createComponentSnapshot(area.isEmpty() ? detail.getLocalBounds() : area, true, 2.0f));
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
    for (const auto& [pitches, start] :
         {std::pair{std::vector<int>{57, 60, 64}, 0.0}, std::pair{std::vector<int>{53, 57, 60}, 4.0},
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

TEST_CASE("piano roll: In key shows only the key's rows, a click draws on the row clicked, the arrows "
          "move by its steps; the whole roll is a toggle away, remembered for the clip (§6, §21)",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    auto& detail = f.shell->detail();
    REQUIRE_FALSE(roll.inKey()); // no key: the whole roll, chromatic, as before
    CHECK(roll.shownRows().empty());
    CHECK_FALSE(detail.inKeyOption().isVisible());
    CHECK_FALSE(f.shell->actions().isEnabled("view.clipInKey"));
    const Key aMinor{9, "minor"};
    REQUIRE(f.session->setKey(aMinor));
    f.refresh();
    REQUIRE(roll.inKey()); // on by default with a key
    CHECK(detail.inKeyOption().isVisible());
    CHECK(detail.inKeyOption().getToggleState());
    for (int p : roll.shownRows())
        CHECK(fd::music::contains(aMinor, p));

    // A double-click draws on the row clicked: every row is the key's.
    const double step = roll.timeline().gridStepBeats(f.session->snapshot()->tempo);
    doubleClick(roll, roll.pointFor(0.1, 67));
    REQUIRE(f.notes().size() == 1);
    CHECK(f.notes()[0].pitch == 67);

    // ↑ moves by a step of the scale (G → A); Ctrl+↑ by a semitone, out of the key, and the note
    // keeps a row of its own; Shift+↑ by an octave.
    click(roll, roll.pointFor(step * 0.5, 67));
    REQUIRE(roll.selection().size() == 1);
    CHECK(f.press(juce::KeyPress::upKey));
    CHECK(f.notes()[0].pitch == 69);
    CHECK(f.press(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier));
    CHECK(f.notes()[0].pitch == 70);
    const auto has = [&roll](int pitch)
    {
        const auto& rows = roll.shownRows();
        return std::find(rows.begin(), rows.end(), pitch) != rows.end();
    };
    CHECK(has(70)); // A♯: outside the key, its row stays while it has notes
    CHECK(f.press(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
    CHECK(f.notes()[0].pitch == 82);
    CHECK_FALSE(has(70));
    CHECK(f.press(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier));
    f.dump("piano-roll-in-key@2x.png", detail.getLocalBounds().removeFromLeft(760));

    // Fit to key: A♯ goes to the nearer of A and B; a tie with nothing before it: down. Its row goes.
    REQUIRE(f.shell->actions().perform("notes.fitToKey"));
    CHECK(f.notes()[0].pitch == 69);
    CHECK_FALSE(has(70));
    // Dragged, a note moves by the rows: the key's steps.
    drag(roll, roll.pointFor(step * 0.5, 69), roll.pointFor(step * 0.5, 71));
    CHECK(f.notes()[0].pitch == 71);

    // A preview's notes outside the key show their rows while it's shown.
    auto preview = f.notes();
    preview.push_back({66, 4.0, 1.0, 0.8f});
    roll.previewEdit(fd::model::NoteEdit{preview, {}});
    CHECK(has(66));
    roll.previewEdit(std::nullopt);
    CHECK_FALSE(has(66));

    // The whole roll: every row, chromatic. A click draws on the row clicked; ↑ is a semitone.
    detail.inKeyOption().onClick(); // the header's toggle
    REQUIRE_FALSE(roll.inKey());
    CHECK(roll.shownRows().empty());
    CHECK_FALSE(detail.inKeyOption().getToggleState());
    CHECK_FALSE(f.shell->actions().isToggled("view.clipInKey"));
    doubleClick(roll, roll.pointFor(2.1, 68));
    REQUIRE(f.notes().size() == 2);
    CHECK(f.notes()[1].pitch == 68);
    CHECK(f.press(juce::KeyPress::upKey));
    CHECK(f.notes()[1].pitch == 69);
    f.dump("piano-roll-whole-roll@2x.png", detail.getLocalBounds().removeFromLeft(760));

    // Remembered for the clip: another clip opens In key; this one opens whole again.
    const auto first = f.clipId;
    f.clipId = f.session->createMidiClip(f.track, 8.0, 16.0);
    REQUIRE(f.clipId != 0);
    f.open();
    CHECK(roll.inKey());
    f.clipId = first;
    f.open();
    CHECK_FALSE(roll.inKey());
    REQUIRE(f.shell->actions().perform("view.clipInKey"));
    CHECK(roll.inKey());
}

TEST_CASE("piano roll: the chord lane names the chords; a click selects one; the Chord popover writes and "
          "re-voices",
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
    {
        return juce::Point<float>(roll.pointFor(beat, 60).x,
                                  static_cast<float>(lane.getY() + lane.getHeight() / 2));
    };
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
    // Its kind starts as the chord there is (§20.4): G7, so sevenths.
    CHECK(inKey ==
          juce::StringArray{"Am7", juce::String::fromUTF8("Bm7♭5"), "Cmaj7", "Dm7", "Em7", "Fmaj7", "G7"});
    // The chord there is lit; the popover sits beside the place, inside the window, nothing touching.
    const auto buttons = popover.chordButtons();
    const auto lit =
        std::count_if(buttons.begin(), buttons.end(), [](auto* b) { return b->getToggleState(); });
    CHECK(lit >= 1); // G7, wherever it's offered
    CHECK(f.shell->getLocalBounds().contains(
        popover.getBounds().reduced(juce::roundToInt(fd::ui::Theme::current().shell.shadowRadius))));
    CHECK(crowded(popover, fd::ui::Theme::current().shell.itemGap).isEmpty());

    // Hovering E plays it and shows its notes in place; nothing is kept.
    const fd::music::Chord e{.root = 4, .kind = 0};
    popover.hover(e);
    CHECK(f.notes() == progression());
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(
            dir, "piano-roll-chord-popover@2x.png",
            f.shell->createComponentSnapshot(
                f.shell->getLocalArea(&f.shell->detail(), f.shell->detail().getLocalBounds()), true, 2.0f));
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

TEST_CASE("piano roll: chords drawn on a click, other tracks' notes faint, time handles, a velocity line, "
          "muting, Ctrl+drag",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{9, "minor"}));
    f.refresh();

    // Another track's melody over the clip: shown faint, never selectable.
    const auto lead = f.session->addTrack("Lead");
    const auto leadClip = f.session->createMidiClip(lead, 0.0, 8.0);
    f.session->setNotes(
        leadClip, {{76, 0.0, 1.5, 0.8f}, {79, 1.5, 0.5, 0.8f}, {81, 2.0, 2.0, 0.8f}, {77, 4.0, 4.0, 0.8f}});
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
    CHECK(fd::music::nameChord(std::vector<int>{f.notes()[0].pitch, f.notes()[1].pitch, f.notes()[2].pitch})
              ->root == 9);
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

    // Quantise…: the Tools pane at Quantise (§25); the notes move live as it's set; Apply keeps them.
    REQUIRE(f.shell->actions().perform("notes.quantiseSettings"));
    auto& pane = detail.toolsPane();
    REQUIRE(pane.isVisible());
    REQUIRE(pane.chosen() == "quantise");
    pane.form().setValue("strength", 50.0);
    CHECK(f.notes()[0].start == Catch::Approx(0.1)); // only previewed
    CHECK(roll.notes()[0].start == Catch::Approx(0.1));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-quantise@2x.png",
                f.shell->createComponentSnapshot(f.shell->getLocalArea(&detail, detail.getLocalBounds()),
                                                 true, 2.0f));
    CHECK(crowded(pane, 4).isEmpty());
    pane.apply();
    CHECK(pane.chosen().empty());
    CHECK(f.notes()[0].start == Catch::Approx(0.05));
    CHECK(roll.toolValues(*fd::model::findNoteTool("quantise")).at("strength") == 50.0);
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
        CHECK(detail.inKeyOption().isVisible());
        // Nothing touches: the options, the commands and the tabs at least the item gap apart.
        CHECK(crowded(header, fd::ui::Theme::current().shell.itemGap).isEmpty());
        for (auto* c : header.getChildren())
            if (c->isVisible() && !c->getBounds().isEmpty())
                CHECK(header.getLocalBounds().contains(c->getBounds()));
        // Others gives way first, then Draw; In key stays.
        const bool others = header.isDropped(detail.othersOption()),
                   draw = header.isDropped(detail.drawChoice());
        CHECK((!draw || others));
        CHECK_FALSE(header.isDropped(detail.inKeyOption()));
        // The Tools pane's Generate, then Transform, last of all (§25).
        const bool generate = header.isDropped(detail.generateButton()),
                   transform = header.isDropped(detail.transformButton());
        CHECK((!generate || draw));
        CHECK((!transform || generate));
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            dumpPng(dir, "piano-roll-header-" + juce::String(width) + "@2x.png",
                    detail.createComponentSnapshot(header.getBounds(), true, 2.0f));
        if (width == 1800)
            CHECK_FALSE(others);
    }
}

TEST_CASE("piano roll: the inspector's NOTES section shows the selected notes; typed sets all, dragged moves "
          "together",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    auto& inspector = f.shell->inspector();
    const Key aMinor{9, "minor"};
    REQUIRE(f.session->setKey(aMinor));
    f.setNotes(progression());
    using S = fd::model::InspectorSection;
    const auto hasNotes = [&]
    {
        return std::find(inspector.sections().begin(), inspector.sections().end(), S::Notes) !=
               inspector.sections().end();
    };
    CHECK_FALSE(hasNotes()); // nothing selected

    // The G7 chord, selected from the lane: four notes, their ranges, the chord they make.
    const auto lane = roll.chordLaneArea();
    click(roll, {roll.pointFor(13.0, 60).x, static_cast<float>(lane.getCentreY())});
    REQUIRE(roll.selection().size() == 4);
    REQUIRE(hasNotes());
    const auto clipAt = std::find(inspector.sections().begin(), inspector.sections().end(), S::Clip);
    REQUIRE(clipAt != inspector.sections().end());
    CHECK(*(clipAt + 1) == S::Notes); // right under the clip
    using F = fd::model::NoteField;
    auto& pitch = inspector.noteField(F::Pitch);
    CHECK(pitch.isVisible());
    CHECK(pitch.getValue() == 55.0);
    CHECK(pitch.textFromValue(pitch.getValue()) == juce::String::fromUTF8("G2 – F3"));
    CHECK(inspector.noteField(F::Velocity).textFromValue(inspector.noteField(F::Velocity).getValue()) ==
          "96");
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-inspector-notes@2x.png",
                inspector.createComponentSnapshot(
                    inspector.getLocalBounds().withHeight(std::min(inspector.getHeight(), 420)), true, 2.0f));

    // A velocity typed sets them all.
    auto& velocity = inspector.noteField(F::Velocity);
    velocity.setValue(*velocity.valueFromText("64"), juce::sendNotificationSync);
    for (const auto& n : f.notes())
        if (n.start >= 12.0)
            CHECK(juce::roundToInt(n.velocity * 127.0f) == 64);
    // A drag moves the pitches together, one undo step.
    pitch.onGestureStart();
    for (double v : {56.0, 57.0})
        pitch.setValue(v, juce::sendNotificationSync);
    pitch.onGestureEnd();
    std::vector<int> moved;
    for (const auto& n : f.notes())
        if (n.start >= 12.0)
            moved.push_back(n.pitch);
    std::sort(moved.begin(), moved.end());
    CHECK(moved == std::vector<int>{57, 61, 64, 67});
    f.session->undo();
    f.refresh();
    moved.clear();
    for (const auto& n : f.notes())
        if (n.start >= 12.0)
            moved.push_back(n.pitch);
    std::sort(moved.begin(), moved.end());
    CHECK(moved == std::vector<int>{55, 59, 62, 65});
    // A note typed by name.
    CHECK(pitch.valueFromText("F#4") == 78.0);
    CHECK(pitch.valueFromText("C-2") == 0.0);
    CHECK_FALSE(pitch.valueFromText("H2"));
}

TEST_CASE("piano roll: 2 000 notes, the chord lane and other tracks' notes drawn within a frame",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{9, "minor"}));
    // Chords on every beat of four bars, and a fast line over them: 2 000 notes.
    std::vector<fd::model::Note> notes;
    for (int i = 0; notes.size() < 2000; ++i)
    {
        const double at = (i % 64) * 0.25;
        for (int p : {57 + (i % 7), 60 + (i % 5), 64 + (i % 3)})
            notes.push_back({p, at, 0.25, 0.7f});
        notes.push_back({72 + (i % 12), at + 0.125, 0.125, 0.6f});
    }
    notes.resize(2000);
    std::sort(notes.begin(), notes.end(), fd::model::noteOrder);
    const auto other = f.session->addTrack("Pads");
    const auto otherClip = f.session->createMidiClip(other, 0.0, 8.0);
    f.session->setNotes(otherClip, progression());
    const auto setStart = juce::Time::getMillisecondCounterHiRes();
    f.setNotes(notes);
    const auto setMs = juce::Time::getMillisecondCounterHiRes() - setStart;
    REQUIRE(roll.notes().size() >= 1900); // overlaps of a pitch resolved
    REQUIRE(roll.showsChordLane());
    REQUIRE(roll.ghostCount() > 0);
    roll.selectAll();

    juce::Image image(juce::Image::ARGB, roll.getWidth(), roll.getHeight(), true);
    double best = 1e9;
    for (int pass = 0; pass < 5; ++pass)
    {
        juce::Graphics g(image);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        roll.paintEntireComponent(g, false);
        best = std::min(best, juce::Time::getMillisecondCounterHiRes() - start);
    }
    UNSCOPED_INFO("paint " << best << " ms, notes changed " << setMs << " ms");
#if defined(NDEBUG)
    CHECK(best < 16.0); // a frame at 60 Hz
#else
    CHECK(best < 120.0); // a Debug build: an order of magnitude slower, still no stall
#endif
    WARN("2 000 notes: paint " << best << " ms; taking them in " << setMs << " ms");
}

TEST_CASE("piano roll revised again: the whole roll is the piano, plain; In key hatches the rows of "
          "notes outside the key (§21)",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{0, "minor"})); // C minor: C D E♭ F G A♭ B♭
    f.setNotes(progression());                   // in A minor: A, E and B are outside C minor
    const auto& t = fd::ui::Theme::current();
    const auto paint = [&roll]
    {
        juce::Image image(juce::Image::ARGB, roll.getWidth(), roll.getHeight(), true);
        juce::Graphics g(image);
        roll.paintEntireComponent(g, false);
        return image;
    };
    // How many pixels across the row's middle, between two beats with no notes on it, aren't its
    // shade: the grid's lines, and hatching.
    const auto marks = [&roll](const juce::Image& image, int pitch, juce::Colour shade)
    {
        const auto from = roll.pointFor(4.2, pitch).toInt(), to = roll.pointFor(7.8, pitch).toInt();
        int n = 0;
        for (int x = from.x; x < to.x; ++x)
            n += image.getPixelAt(x, from.y) != shade ? 1 : 0;
        return n;
    };

    // The whole roll: the piano's rows, whatever the key.
    roll.setInKey(false);
    auto image = paint();
    const auto rowColour = [&](int pitch)
    {
        // Late in a bar with no note on the row, clear of the grid's lines.
        const auto at = roll.pointFor(pitch == 65 ? 7.9 : 15.9, pitch).toInt();
        return image.getPixelAt(at.x - 3, at.y);
    };
    CHECK(rowColour(64) == t.roles.keyRowLight); // E: outside the key
    CHECK(rowColour(65) == t.roles.keyRowLight); // F: in the key, the same
    CHECK(rowColour(61) == t.roles.keyRowDark);  // D♭: outside the key
    CHECK(rowColour(63) == t.roles.keyRowDark);  // E♭: in the key, the same
    CHECK(marks(image, 64, t.roles.keyRowLight) == marks(image, 65, t.roles.keyRowLight));

    // In key: E's row stays (its notes), hatched; E♭'s, the key's, is plain; F♯'s, no notes, isn't shown.
    roll.setInKey(true);
    image = paint();
    const auto& rows = roll.shownRows();
    CHECK(std::find(rows.begin(), rows.end(), 64) != rows.end());
    CHECK(std::find(rows.begin(), rows.end(), 66) == rows.end());
    CHECK(marks(image, 64, t.roles.keyRowLight) > marks(image, 65, t.roles.keyRowLight) + 20);
    CHECK(marks(image, 63, t.roles.keyRowDark) == marks(image, 65, t.roles.keyRowLight));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        f.shell->detail().frame(0.0);
        dumpPng(dir, "piano-roll-outside-key@2x.png",
                f.shell->detail().createComponentSnapshot(f.shell->detail().getLocalBounds(), true, 2.0f));
    }
}

TEST_CASE("piano roll revised: the chord kind and voicing in the header while drawing chords (§20.2)",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    auto& detail = f.shell->detail();
    REQUIRE(f.session->setKey(Key{9, "minor"}));
    f.refresh();
    CHECK_FALSE(detail.drawKind().isVisible()); // drawing notes: no room taken
    roll.setDraw(fd::model::DrawMode::Chord);
    REQUIRE(detail.drawKind().isVisible());
    REQUIRE(detail.drawVoicing().isVisible());
    CHECK(detail.drawKind().selected() == 0);
    CHECK(detail.drawVoicing().value() == "Smooth");
    // One click on 7th: the next chord drawn is a seventh.
    const auto seventh = detail.drawKind().segmentArea(1).getCentre().toFloat();
    detail.drawKind().mouseDown(mouseEvent(detail.drawKind(), seventh, seventh, false));
    CHECK(roll.options().chord.shape == fd::music::ChordShape::Seventh);
    doubleClick(roll, roll.pointFor(0.1, 69));
    CHECK(f.notes().size() == 4);
    // The View menu has them too.
    REQUIRE(f.shell->actions().perform("view.drawNinths"));
    CHECK(detail.drawKind().selected() == 2);
    REQUIRE(f.shell->actions().perform("view.voicingDrop2"));
    CHECK(detail.drawVoicing().value() == "Drop 2");
    CHECK(crowded(detail.header(), fd::ui::Theme::current().shell.itemGap).isEmpty());
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        detail.frame(0.0);
        dumpPng(dir, "piano-roll-header-chords@2x.png",
                detail.createComponentSnapshot(detail.header().getBounds(), true, 2.0f));
    }
    // Narrow: the voicing gives way before Draw and the kinds.
    RollFixture narrow(760, 900);
    narrow.roll().setDraw(fd::model::DrawMode::Chord);
    auto& header = narrow.shell->detail().header();
    CHECK((!header.isDropped(narrow.shell->detail().drawKind()) ||
           header.isDropped(narrow.shell->detail().drawVoicing())));
    CHECK(crowded(header, fd::ui::Theme::current().shell.itemGap).isEmpty());
}

TEST_CASE("piano roll: Make chords… lays out its cards on every open, the same size as before or not",
          "[integration]")
{
    // The product owner's report: 9 notes, then 10, showed no cards, only the foot. The popover
    // came out the same size (two rows of cards as wide), so it was never laid out again.
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{0, "minor"}));
    std::vector<fd::model::Note> notes;
    for (int k = 0; k < 10; ++k)
        notes.push_back(
            {std::array{48, 50, 51, 53, 55, 56, 58}[static_cast<std::size_t>(k % 7)] + 12 * (k / 7),
             static_cast<double>(k), 1.0, 0.8f});
    f.setNotes(notes);
    click(roll, roll.pointFor(12.5, 72)); // the clip view used last
    auto& pane = f.shell->detail().toolsPane();
    auto& popover = pane.makeChords();
    for (const std::size_t count : {9u, 10u, 9u, 10u})
    {
        INFO(count << " notes");
        std::set<std::size_t> selection;
        for (std::size_t i = 0; i < count; ++i)
            selection.insert(i);
        roll.setSelection(selection);
        REQUIRE(f.shell->actions().perform("notes.makeChords"));
        REQUIRE(popover.cardCount() == count);
        std::set<std::pair<int, int>> places;
        for (std::size_t k = 0; k < count; ++k)
        {
            const auto* card = popover.card(k);
            REQUIRE(card != nullptr);
            CHECK(card->isVisible());
            CHECK(card->getWidth() > 0);
            CHECK(card->getHeight() > 0);
            places.insert({card->getX(), card->getY()});
        }
        CHECK(places.size() == count); // each in a place of its own
        pane.disarm();
    }
}

TEST_CASE(
    "piano roll revised: Make chords… chord by chord; Change chord… for the selected chord (§20.3, §20.4)",
    "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{0, "minor"}));
    // A line of roots: C, A♭, F, G.
    f.setNotes({{60, 0.0, 1.0, 0.8f}, {56, 1.0, 1.0, 0.8f}, {53, 2.0, 1.0, 0.8f}, {55, 3.0, 1.0, 0.8f}});
    click(roll, roll.pointFor(5.0, 72)); // the clip view used last
    REQUIRE(f.shell->actions().perform("notes.makeChords"));
    auto& pane = f.shell->detail().toolsPane();
    REQUIRE(pane.isVisible());
    REQUIRE(pane.chosen() == "makeChords"); // the Tools pane at Make chords (§25)
    auto& popover = pane.makeChords();
    REQUIRE(popover.cardCount() == 4);
    CHECK(popover.chordText(0) == "Cm");
    CHECK(popover.chordText(1) == juce::String::fromUTF8("A♭"));
    // Each its own: a seventh on F, G left as it is; previewed, not kept yet.
    auto f7 = popover.choices()[2];
    f7.chord = fd::music::keyChord(&*roll.key(), 53, fd::music::ChordShape::Seventh);
    popover.setChoice(2, f7);
    auto leave = popover.choices()[3];
    leave.mode = fd::model::NoteChord::Mode::Leave;
    popover.setChoice(3, leave);
    CHECK(popover.chordText(2) == "Fm7");
    CHECK(popover.chordText(3) == "No chord");
    CHECK(f.notes().size() == 4); // only shown
    CHECK(itemsOf(popover.chordMenu(0)).contains("Cm7"));
    CHECK(itemsOf(popover.chordMenu(0)).contains("Leave the note"));
    CHECK(crowded(popover, fd::ui::Theme::current().shell.itemGap).isEmpty());
    CHECK(f.shell->detail().getLocalBounds().contains(pane.getBounds()));
    // The All row: every note at once; it says what they share.
    CHECK(popover.allKind() == std::nullopt); // a seventh and a note left: mixed
    CHECK(itemsOf(popover.allKindsMenu()).contains("7th"));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(
            dir, "piano-roll-make-chords@2x.png",
            f.shell->createComponentSnapshot(
                f.shell->getLocalArea(&f.shell->detail(), f.shell->detail().getLocalBounds()), true, 2.0f));
    pane.apply();
    CHECK(pane.chosen().empty());
    const auto notes = f.notes();
    CHECK(notes.size() == 3 + 3 + 4 + 1);
    f.session->undo(); // one undo step
    f.refresh();
    CHECK(f.notes().size() == 4);

    // Change chord…: dimmed until a chord is selected; its choices are its own.
    f.setNotes(progression());
    roll.setSelection({});
    CHECK_FALSE(f.shell->actions().isEnabled("notes.changeChord"));
    const auto lane = roll.chordLaneArea();
    click(roll, {roll.pointFor(13.0, 60).x, static_cast<float>(lane.getCentreY())}); // G7
    REQUIRE(f.shell->actions().isEnabled("notes.changeChord"));
    const auto drawStyle = roll.options().chord;
    REQUIRE(f.shell->actions().perform("notes.changeChord"));
    auto& chord = f.shell->chordPopover();
    REQUIRE(chord.isOpen());
    CHECK(chord.kind().selected() == 1); // G7: a seventh
    chord.kind().setSelected(0);
    chord.kind().onChange(0);
    CHECK(roll.options().chord == drawStyle); // Draw's choices untouched
    chord.close();
}

TEST_CASE("piano roll: Make chords on one note applies every kind, voicing and inversion, each one undo step",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{0, "minor"}));
    f.setNotes({{60, 0.0, 1.0, 0.8f}});
    const auto one = f.notes();          // as kept: the velocity in MIDI's steps
    click(roll, roll.pointFor(5.0, 72)); // the clip view used last
    auto& pane = f.shell->detail().toolsPane();
    using fd::music::ChordShape;
    using fd::music::Inversion;
    using fd::music::Voicing;
    for (const auto shape : {ChordShape::Triad, ChordShape::Seventh, ChordShape::Ninth, ChordShape::Sus2,
                             ChordShape::Sus4, ChordShape::Add9})
        for (const auto voicing : {Voicing::Smooth, Voicing::Close, Voicing::Open, Voicing::Drop2,
                                   Voicing::Drop3, Voicing::Spread})
            for (const auto inversion :
                 {Inversion::Auto, Inversion::Root, Inversion::First, Inversion::Second, Inversion::Third})
            {
                INFO("shape " << static_cast<int>(shape) << ", voicing " << static_cast<int>(voicing)
                              << ", inversion " << static_cast<int>(inversion));
                pane.choose("makeChords");
                auto& form = pane.makeChords();
                REQUIRE(form.cardCount() == 1);
                auto choice = form.choices()[0];
                choice.chord = fd::music::keyChord(&*roll.key(), 60, shape);
                choice.voicing = voicing;
                choice.inversion = inversion;
                form.setChoice(0, choice);
                pane.apply();
                f.refresh();
                const auto made = f.notes();
                CHECK(made.size() >= 3);
                for (const auto& n : made)
                    CHECK((n.pitch >= 0 && n.pitch <= 127));
                f.session->undo();
                f.refresh();
                REQUIRE(f.notes() == one);
            }
}

TEST_CASE(
    "piano roll (§25): the Apply buttons apply as Return does: the tool let go, nothing left reading what "
    "it applied (the product owner's crash: Make chords' Apply on one note)",
    "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{0, "minor"}));
    f.setNotes({{60, 0.0, 1.0, 0.8f}});
    click(roll, roll.pointFor(5.0, 72)); // the clip view used last
    auto& detail = f.shell->detail();
    auto& pane = detail.toolsPane();

    // Make chords: a sus4 on the note, in second inversion, kept by its Apply button.
    REQUIRE(f.shell->actions().perform("notes.makeChords"));
    auto& form = pane.makeChords();
    auto choice = form.choices()[0];
    choice.chord = fd::music::keyChord(&*roll.key(), 60, fd::music::ChordShape::Sus4);
    choice.inversion = fd::music::Inversion::Second;
    form.setChoice(0, choice);
    form.applyButton().onClick();
    (void)pane.createComponentSnapshot(pane.getLocalBounds()); // painted before the next frame
    CHECK(pane.chosen().empty());
    CHECK_FALSE(form.isVisible());
    CHECK(form.cardCount() == 0);
    CHECK(f.session->undoDescription() == "Make chords");
    detail.frame(0.0); // a frame after: nothing shown again on the notes made
    (void)pane.createComponentSnapshot(pane.getLocalBounds()); // and the pane paints
    f.refresh();
    CHECK(f.notes().size() == 3);
    CHECK_FALSE(roll.isPreviewing());

    // A tool's Apply button too: kept once, not shown again on what it made.
    REQUIRE(f.shell->actions().perform("notes.tool.strum"));
    pane.form().applyButton().onClick();
    CHECK(pane.chosen().empty());
    CHECK_FALSE(pane.form().isVisible());
    CHECK(f.session->undoDescription() == "Strum");
    detail.frame(0.0);
    CHECK_FALSE(roll.isPreviewing());
}

// ---- P4b: chance, release velocity, controller lanes, Others track by track ----------------------

TEST_CASE("piano roll (P4b): the bottom lane shows velocity, chance or release velocity; each drawn the same "
          "way; notes show their chance; Re-roll decides again",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    f.setNotes(progression());
    auto& lane = roll.laneChoice();
    CHECK(lane.isVisible());
    CHECK(lane.value() == "VELOCITY");
    CHECK(itemsOf(roll.laneMenu()).contains("Chance"));
    CHECK(itemsOf(roll.laneMenu()).contains("Pitch bend"));
    CHECK_FALSE(f.shell->actions().isEnabled("notes.reroll")); // nothing is left to chance yet

    // Chance: a click in the lane sets the note under it, as velocity's does.
    roll.setLane({.kind = fd::model::BottomLane::Kind::Chance});
    CHECK(lane.value() == "CHANCE");
    const auto area = roll.laneArea();
    const auto stemX = roll.pointFor(0.0, 57).x;
    const auto yFor = [&](float v)
    {
        return static_cast<float>(area.reduced(0, 5).getBottom()) -
               v * static_cast<float>(area.reduced(0, 5).getHeight());
    };
    click(roll, {stemX, yFor(0.5f)});
    int halved = 0;
    for (const auto& n : f.notes())
        if (n.start == 0.0)
            halved += std::abs(n.chance - 0.5f) < 0.02f ? 1 : 0;
    CHECK(halved == 3); // the Am chord's three notes start there
    // It rolls again each pass (§26): Re-roll waits for Keep chance as played.
    CHECK_FALSE(f.shell->actions().isEnabled("notes.reroll"));
    CHECK(f.shell->actions().isEnabled("notes.keepChance"));
    // A line across the rest (Shift+drag), as velocity's.
    drag(roll, {roll.pointFor(3.9, 57).x, yFor(1.0f)}, {roll.pointFor(12.1, 57).x, yFor(0.2f)},
         juce::ModifierKeys::shiftModifier);
    for (const auto& n : f.notes())
        if (n.start == 4.0)
            CHECK(n.chance > 0.6f);
        else if (n.start == 12.0)
            CHECK(n.chance < 0.4f);
    f.dump("piano-roll-chance@2x.png");

    // Keep chance as played: the lane's head and the menu say so; then Re-roll gives it a new seed.
    CHECK_FALSE(lane.isLocked());
    REQUIRE(f.shell->actions().perform("notes.keepChance"));
    f.refresh();
    CHECK(f.clip().chanceKept);
    CHECK(lane.isLocked());
    CHECK(f.shell->actions().find("notes.keepChance")->label() == "Let chance roll");
    f.dump("piano-roll-chance-kept@2x.png");
    const auto seed = f.clip().chanceSeed;
    REQUIRE(f.shell->actions().perform("notes.reroll"));
    f.refresh();
    CHECK(f.clip().chanceSeed != seed);
    CHECK(f.clip().chanceKept);

    // Release velocity: its own lane, drawn the same way.
    roll.setLane({.kind = fd::model::BottomLane::Kind::Release});
    CHECK(lane.value() == "RELEASE");
    click(roll, {stemX, yFor(1.0f)});
    for (const auto& n : f.notes())
        if (n.start == 0.0)
            CHECK(juce::roundToInt(n.releaseVelocity * 127.0f) == 127);

    // The inspector: Spread, Release and Chance for the selected notes (the F chord's).
    auto& inspector = f.shell->inspector();
    roll.setSelection({3, 4, 5});
    using F = fd::model::NoteField;
    auto& chance = inspector.noteField(F::Chance);
    REQUIRE(chance.isVisible());
    chance.setValue(*chance.valueFromText("75 %"), juce::sendNotificationSync);
    auto& spread = inspector.noteField(F::Spread);
    spread.setValue(*spread.valueFromText("12"), juce::sendNotificationSync);
    CHECK(spread.textFromValue(spread.getValue()) == juce::String::fromUTF8("± 12"));
    for (const auto& n : f.notes())
        if (n.start == 4.0)
        {
            CHECK(n.chance == 0.75f);
            CHECK(juce::roundToInt(n.velocitySpread * 127.0f) == 12);
        }
    // The clip keeps its roll: a lock by the Chance row's name (§26).
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-inspector-chance-kept@2x.png",
                inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));
    roll.setLane({.kind = fd::model::BottomLane::Kind::Velocity});
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        f.shell->detail().frame(0.0);
        dumpPng(dir, "piano-roll-spread@2x.png",
                f.shell->detail().createComponentSnapshot(
                    f.shell->detail().getLocalBounds().removeFromLeft(900), true, 2.0f));
        dumpPng(dir, "piano-roll-inspector-chance@2x.png",
                inspector.createComponentSnapshot(
                    inspector.getLocalBounds().withHeight(std::min(inspector.getHeight(), 520)), true, 2.0f));
        // Hi-hats left to chance, each too narrow for its name: the bar alone says how likely.
        auto notes = progression();
        for (int k = 0; k < 16; ++k)
        {
            fd::model::Note hat{72, k * 0.5 + 0.25, 0.25, 0.7f};
            hat.chance = 0.2f + 0.05f * static_cast<float>(k);
            notes.push_back(hat);
        }
        f.setNotes(notes);
        roll.setLane({.kind = fd::model::BottomLane::Kind::Chance});
        f.shell->detail().frame(0.0);
        dumpPng(dir, "piano-roll-chance-hats@2x.png",
                f.shell->detail().createComponentSnapshot(
                    f.shell->detail().getLocalBounds().removeFromLeft(900), true, 2.0f));
    }
}

TEST_CASE("piano roll (P4b): a controller lane is edited as automation lanes are; Delete deletes its points; "
          "pedals are on or off",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    f.setNotes(progression());
    namespace cc = fd::model::controllers;
    using K = fd::model::BottomLane::Kind;
    roll.setLane({.kind = K::Controller, .controller = cc::modWheel});
    CHECK(roll.laneChoice().value() == "MOD");

    // Double-click adds a point (on the grid), one undo step each.
    doubleClick(roll, roll.lanePointFor(1.0, 0.25f));
    doubleClick(roll, roll.lanePointFor(3.0, 1.0f));
    f.refresh();
    auto lane = roll.controllerLane();
    REQUIRE(lane.points.size() == 2);
    CHECK(lane.points[0].time == Catch::Approx(1.0));
    CHECK(lane.points[0].value == Catch::Approx(0.25f).margin(0.02f));
    CHECK(f.session->undoDescription() == "Add point");
    CHECK(f.clip().controllers.size() == 1);

    // A drag moves a point, snapped in time.
    drag(roll, roll.lanePointFor(1.0, 0.25f), roll.lanePointFor(2.02, 0.5f));
    f.refresh();
    lane = roll.controllerLane();
    REQUIRE(lane.points.size() == 2);
    CHECK(lane.points[0].time == Catch::Approx(2.0));
    CHECK(lane.points[0].value == Catch::Approx(0.5f).margin(0.03f));
    CHECK(f.session->undoDescription() == "Move points");

    // A click selects a point; Delete deletes it; undo brings it back.
    click(roll, roll.lanePointFor(3.0, 1.0f));
    CHECK(roll.pointSelection() == std::set<size_t>{1});
    REQUIRE(f.shell->actions().perform("edit.delete"));
    f.refresh();
    CHECK(roll.controllerLane().points.size() == 1);
    CHECK(f.notes().size() == progression().size()); // the notes stay
    f.session->undo();
    f.refresh();
    CHECK(roll.controllerLane().points.size() == 2);

    // Ctrl+drag draws.
    drag(roll, roll.lanePointFor(8.0, 0.1f), roll.lanePointFor(11.0, 0.9f),
         juce::ModifierKeys::commandModifier);
    f.refresh();
    CHECK(roll.controllerLane().points.size() > 3);
    CHECK(f.session->undoDescription() == "Draw controller");
    f.dump("piano-roll-controller@2x.png");

    // Sustain: on or off.
    roll.setLane({.kind = K::Controller, .controller = cc::sustain});
    CHECK(roll.controllerLane().points.empty());
    doubleClick(roll, roll.lanePointFor(1.0, 0.7f));
    f.refresh();
    lane = roll.controllerLane();
    REQUIRE(lane.points.size() == 1);
    CHECK(lane.points[0].value == 1.0f);
    CHECK(f.clip().controllers.size() == 2);
    CHECK(itemsOf(roll.laneMenu()).contains("Sustain"));

    // Pitch bend rests in the middle: drawn dashed there while it has no points.
    roll.setLane({.kind = K::Controller, .controller = cc::pitchBend});
    CHECK(roll.laneChoice().value() == "BEND");
    f.dump("piano-roll-pitch-bend@2x.png");
}

TEST_CASE(
    "piano roll (P4b): a controller lane's points are cut, copied, pasted and duplicated as an automation "
    "lane's, from the Edit menu",
    "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    f.setNotes(progression());
    namespace cc = fd::model::controllers;
    using K = fd::model::BottomLane::Kind;
    roll.setLane({.kind = K::Controller, .controller = cc::modWheel});
    f.session->setControllers(
        f.clipId, {{.controller = cc::modWheel, .points = {{1.0, 0.25f, 0.0f}, {2.0, 0.75f, 0.0f}}}});
    f.refresh();
    REQUIRE(roll.controllerLane().points.size() == 2);
    auto& actions = f.shell->actions();
    const auto selectBoth = [&]
    {
        click(roll, roll.lanePointFor(1.0, 0.25f));
        click(roll, roll.lanePointFor(2.0, 0.75f), 1, juce::ModifierKeys::shiftModifier);
        REQUIRE(roll.pointSelection().size() == 2);
    };

    // Copy, then paste at the playhead (bar 3): the two again, two bars on.
    selectBoth();
    REQUIRE(actions.perform("edit.copy"));
    f.session->setPosition(f.session->snapshot()->tempo.secondsForBeats(8.0));
    REQUIRE(actions.perform("edit.paste"));
    f.refresh();
    auto points = roll.controllerLane().points;
    REQUIRE(points.size() == 4);
    CHECK(points[2].time == Catch::Approx(8.0));
    CHECK(points[3].time == Catch::Approx(9.0));
    CHECK(points[3].value == Catch::Approx(0.75f).margin(0.01f));
    CHECK(f.session->undoDescription() == "Paste");
    CHECK(f.notes().size() == progression().size()); // the notes are left alone

    // Duplicate, as an automation lane's: moved on by their span rounded up to the grid (a beat
    // here); the first copy lands on the second point, and both stay, a jump.
    selectBoth();
    REQUIRE(actions.perform("edit.duplicate"));
    f.refresh();
    points = roll.controllerLane().points;
    REQUIRE(points.size() == 6);
    CHECK(points[1].time == Catch::Approx(2.0));
    CHECK(points[1].value == Catch::Approx(0.75f).margin(0.01f));
    CHECK(points[2].time == Catch::Approx(2.0));
    CHECK(points[2].value == Catch::Approx(0.25f).margin(0.01f));
    CHECK(points[3].time == Catch::Approx(3.0));
    CHECK(f.session->undoDescription() == "Duplicate points");

    // Cut the pasted two: gone from the lane, ready to paste; the notes stay.
    click(roll, roll.lanePointFor(8.0, 0.25f));
    click(roll, roll.lanePointFor(9.0, 0.75f), 1, juce::ModifierKeys::shiftModifier);
    REQUIRE(roll.pointSelection().size() == 2);
    REQUIRE(actions.perform("edit.cut"));
    f.refresh();
    CHECK(roll.controllerLane().points.size() == 4);
    CHECK(f.notes().size() == progression().size());

    // Into the sustain lane: its steps, on or off.
    roll.setLane({.kind = K::Controller, .controller = cc::sustain});
    click(roll, roll.lanePointFor(12.0, 0.5f)); // the lane used last
    f.session->setPosition(0.0);
    REQUIRE(actions.perform("edit.paste"));
    f.refresh();
    points = roll.controllerLane().points;
    REQUIRE(points.size() == 2);
    CHECK(points[0].value == 0.0f);
    CHECK(points[1].value == 1.0f);
    CHECK(points[0].curve == fd::model::kStepCurve);
}

TEST_CASE(
    "piano roll (P4b): Others leaves out the tracks chosen one by one, from the header and the View menu",
    "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    f.setNotes(progression());
    // Another track with notes under the clip.
    const auto tracks = f.session->snapshot()->tracks;
    REQUIRE(tracks.size() >= 2);
    const auto bass = tracks[1].id;
    const auto bassClip = f.session->createMidiClip(bass, 0.0, 8.0);
    f.session->setNotes(bassClip, {{36, 0.0, 4.0, 0.8f}, {41, 4.0, 4.0, 0.8f}});
    f.refresh();
    roll.setOthers(true);
    REQUIRE(roll.ghostCount() == 2);
    const auto items = itemsOf(roll.othersMenu());
    REQUIRE(items.size() == 1);
    CHECK(items[0] == juce::String::fromUTF8(tracks[1].name.c_str()));
    roll.setOthersHidden(bass, true);
    CHECK(roll.ghostCount() == 0);
    CHECK(roll.options().othersHidden.contains(bass));
    // Kept with the project's view state.
    CHECK(fd::model::pianoRollOptionsFromJson(fd::model::pianoRollOptionsToJson(roll.options())) ==
          roll.options());
    roll.setOthersHidden(bass, false);
    CHECK(roll.ghostCount() == 2);
    // The View menu has them too.
    const auto menus = fd::model::menuBar(fd::model::MenuPlatform::Windowed);
    const auto actions = fd::model::menuActions(menus);
    CHECK(std::find(actions.begin(), actions.end(), "view.clipOthers") != actions.end());
}

TEST_CASE("piano roll (P4b): the lane's head fits every lane's label in the keys' column", "[integration]")
{
    const int width = fd::ui::Theme::current().shell.keyboardWidth;
    using K = fd::model::BottomLane::Kind;
    for (const auto kind : {K::Velocity, K::Chance, K::Release})
        CHECK(fd::ui::LaneChoice::fits(fd::model::bottomLaneLabel({.kind = kind}), width));
    for (int c = 0; c <= fd::model::controllers::aftertouch; ++c)
    {
        INFO("controller " << c);
        CHECK(fd::ui::LaneChoice::fits(fd::model::bottomLaneLabel({.kind = K::Controller, .controller = c}),
                                       width));
    }
}

// ---- P4c: the tools that transform and generate ---------------------------------------------------

TEST_CASE("piano roll (P4c, §25): the Tools pane: Transform and Generate on the bar, every tool a tile, each "
          "tool's settings from its description; previews, applies as one named step, lets go on Escape",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{9, "minor"}));
    f.setNotes(progression());
    auto& detail = f.shell->detail();
    auto& pane = detail.toolsPane();
    const auto& t = fd::ui::Theme::current();
    CHECK(detail.transformButton().isVisible());
    CHECK(detail.generateButton().isVisible());
    CHECK_FALSE(pane.isVisible());

    // Transform on the bar: the pane docked at the right, the notes beside it, every transform a tile.
    detail.transformButton().onClick();
    REQUIRE(pane.isVisible());
    CHECK(detail.transformButton().getToggleState());
    CHECK_FALSE(pane.isFloating());
    CHECK(roll.getRight() <= pane.getX()); // nothing covered
    CHECK(roll.options().toolsOpen);
    int tiles = 0;
    for (const auto& tool : fd::model::noteTools())
        if (const auto* tile = pane.tile(tool.id); tile != nullptr && tile->isVisible())
            ++tiles;
    CHECK(tiles == 9);
    REQUIRE(pane.tile("makeChords") != nullptr);
    CHECK(pane.tile("makeChords")->isVisible());
    CHECK_FALSE(pane.tile("rhythm")->isVisible());
    CHECK(crowded(pane, t.shell.itemGap).isEmpty());

    // Every tool, chosen from its tile: its settings, nothing touching; the notes back when let go.
    for (const auto& tool : fd::model::noteTools())
    {
        INFO(tool.id);
        auto* tile = pane.tile(tool.id);
        REQUIRE(tile != nullptr);
        if (tool.kind != pane.tab())
            pane.tabs().onChange(tool.kind == fd::model::ToolKind::Generate ? 1 : 0);
        REQUIRE(tile->isEnabled());
        tile->onClick();
        REQUIRE(pane.chosen() == tool.id);
        CHECK(tile->getToggleState());
        CHECK(pane.form().tool() == &tool);
        CHECK(crowded(pane, t.shell.itemGap).isEmpty());
        CHECK(crowded(pane.form(), t.shell.itemGap).isEmpty());
        CHECK(pane.getLocalBounds().contains(pane.form().getBounds()));
        for (const auto& s : tool.settings)
            CHECK((pane.form().control(s.id) != nullptr) ==
                  fd::model::settingShown(tool, s, pane.form().values()));
        if (const char* dir = std::getenv("FD_DUMP_DIR");
            dir != nullptr &&
            (tool.id == "strum" || tool.id == "velocity" || tool.id == "chords" || tool.id == "rhythm"))
        {
            detail.frame(0.0);
            dumpPng(dir, "piano-roll-tool-" + juce::String(std::string(tool.id)) + "@2x.png",
                    f.shell->createComponentSnapshot(f.shell->getLocalArea(&detail, detail.getLocalBounds()),
                                                     true, 2.0f));
        }
        tile->onClick(); // chosen again: none
        CHECK(pane.chosen().empty());
        CHECK(f.notes() == progression());
        CHECK(roll.notes() == progression());
    }
    // The tabs and the bar's buttons go together.
    CHECK(detail.generateButton().getToggleState());
    CHECK_FALSE(detail.transformButton().getToggleState());
    pane.tabs().onChange(0);
    CHECK(detail.transformButton().getToggleState());

    // Strum from the menu: chosen in the pane, previewed, kept as one step named for it, remembered.
    auto& actions = f.shell->actions();
    REQUIRE(actions.perform("notes.tool.strum"));
    REQUIRE(pane.chosen() == "strum");
    pane.form().setValue("time", 80.0);
    CHECK(roll.notes() == progression()); // only shown so far
    pane.apply();
    CHECK(pane.chosen().empty());
    CHECK(f.session->undoDescription() == "Strum");
    CHECK(f.notes() != progression());
    REQUIRE(actions.perform("notes.tool.strum"));
    CHECK(pane.form().values().at("time") == 80.0);
    // Working on the notes lets go of it.
    click(roll, roll.pointFor(15.5, 72));
    CHECK(pane.chosen().empty());
    f.session->undo();
    f.refresh();
    CHECK(f.notes() == progression());

    // The Velocity tool shows each shape's settings.
    REQUIRE(actions.perform("notes.tool.velocity"));
    CHECK(pane.form().control("from") != nullptr);
    CHECK(pane.form().control("cycles") == nullptr);
    const auto height = pane.form().getHeight();
    pane.form().setValue("shape", 2.0); // Range: one setting
    CHECK(pane.form().control("from") == nullptr);
    CHECK(pane.form().control("range") != nullptr);
    CHECK(pane.form().getHeight() < height); // as tall as what it shows
    CHECK(crowded(pane.form(), t.shell.itemGap).isEmpty());

    // Escape: the tool lets go, then the pane closes.
    pane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    CHECK(pane.chosen().empty());
    CHECK(f.notes() == progression());
    pane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    CHECK_FALSE(pane.isVisible());
    CHECK_FALSE(detail.transformButton().getToggleState());
    CHECK_FALSE(roll.options().toolsOpen);

    // In the View menu too.
    REQUIRE(actions.perform("view.toolsGenerate"));
    CHECK(pane.isVisible());
    CHECK(pane.tab() == fd::model::ToolKind::Generate);
    CHECK(actions.isToggled("view.toolsGenerate"));
}

TEST_CASE("piano roll (P4c, §25): a tall Tools pane names its tiles; a short one shows their icons in a row, "
          "the settings scrolling under Apply where they still don't fit",
          "[integration]")
{
    RollFixture f;
    f.setNotes(progression());
    auto& detail = f.shell->detail();
    auto& pane = detail.toolsPane();
    const auto& t = fd::ui::Theme::current();
    detail.showTool("rhythm");
    const auto width = detail.getWidth();
    // Tall: the names, two columns; the tool's rows all in view.
    detail.setSize(width, 640);
    CHECK_FALSE(pane.isCompact());
    CHECK_FALSE(pane.tile("rhythm")->isCompact());
    CHECK(pane.tile("rhythm")->getWidth() > pane.tile("rhythm")->getHeight());
    CHECK_FALSE(pane.form().scrolls());
    CHECK(crowded(pane, t.shell.itemGap).isEmpty());
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-tools-tall@2x.png",
                detail.createComponentSnapshot(detail.getLocalBounds(), true, 2.0f));
    // As the clip view opens by default: icons, square, in a row; named in their tooltips.
    detail.setSize(width, 320);
    CHECK(pane.isCompact());
    CHECK(pane.tile("rhythm")->getWidth() == pane.tile("rhythm")->getHeight());
    CHECK(pane.tile("melody")->getY() == pane.tile("rhythm")->getY());
    CHECK(pane.tile("melody")->getTooltip().startsWith("Melody: "));
    CHECK(crowded(pane, t.shell.itemGap).isEmpty());
    // Shorter still: the rows scroll, the title and Apply stay.
    detail.setSize(width, 220);
    CHECK(pane.form().scrolls());
    CHECK(pane.form().applyButton().isVisible());
    CHECK(pane.getLocalBounds().contains(pane.form().getBounds()));
    pane.disarm();
}

TEST_CASE("piano roll (P4c, §25): in a narrow clip view the Tools pane floats over the notes",
          "[integration]")
{
    RollFixture f(820, 900);
    f.setNotes(progression());
    auto& detail = f.shell->detail();
    auto& pane = detail.toolsPane();
    detail.showTools(fd::model::ToolKind::Transform);
    REQUIRE(pane.isVisible());
    CHECK(pane.isFloating());
    CHECK(f.roll().getRight() > pane.getX()); // over the notes' right side
    CHECK(detail.getLocalBounds().contains(pane.getBounds()));
    detail.showTool("makeChords");
    CHECK(pane.chosen() == "makeChords");
    CHECK(crowded(pane.makeChords(), fd::ui::Theme::current().shell.itemGap).isEmpty());
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "piano-roll-tools-narrow@2x.png",
                f.shell->createComponentSnapshot(f.shell->getLocalArea(&detail, detail.getLocalBounds()),
                                                 true, 2.0f));
    pane.disarm();
    CHECK(f.notes() == progression());
}

TEST_CASE("piano roll (P4c): Generate writes into an empty clip; the bassline follows its chords; the menus",
          "[integration]")
{
    RollFixture f;
    auto& roll = f.roll();
    REQUIRE(f.session->setKey(Key{0, "major"}));
    auto& actions = f.shell->actions();
    auto& pane = f.shell->detail().toolsPane();
    CHECK_FALSE(actions.isEnabled("notes.tool.strum"));    // no notes to transform
    CHECK_FALSE(actions.isEnabled("notes.tool.bassline")); // no chords to follow
    CHECK(fd::model::toolUnavailable(*fd::model::findNoteTool("bassline"), roll.notes()).find("chords") !=
          std::string::npos); // what the dimmed item says
    REQUIRE(actions.isEnabled("notes.tool.chords"));

    REQUIRE(actions.perform("notes.tool.chords"));
    CHECK(pane.form().title() == juce::String::fromUTF8("Chords · 4 bars"));
    CHECK_FALSE(pane.tile("bassline")->isEnabled()); // no chords yet: dimmed, saying why
    CHECK(pane.tile("bassline")->getTooltip().contains("chords"));
    pane.apply();
    CHECK(f.session->undoDescription() == "Chords");
    f.refresh();
    REQUIRE(roll.chordSpans().size() == 4);
    pane.refresh();
    CHECK(pane.tile("bassline")->isEnabled());
    REQUIRE(actions.isEnabled("notes.tool.bassline"));
    REQUIRE(actions.perform("notes.tool.bassline"));
    pane.apply();
    f.refresh();
    int low = 0;
    for (const auto& n : f.notes())
        low += n.pitch < 48 ? 1 : 0;
    CHECK(low == 16); // a root a beat, four bars
    f.dump("piano-roll-generated@2x.png");

    // In the Notes menu and the notes' right-click menu.
    const auto menus = fd::model::menuBar(fd::model::MenuPlatform::Windowed);
    const auto ids = fd::model::menuActions(menus);
    for (const auto& tool : fd::model::noteTools())
        CHECK(std::find(ids.begin(), ids.end(),
                        tool.id == "quantise" ? std::string("notes.quantiseSettings") // Quantise…
                                              : "notes.tool." + std::string(tool.id)) != ids.end());
    CHECK(fd::model::menuProblems(menus).empty());
}
