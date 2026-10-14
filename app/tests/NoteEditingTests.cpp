// JUCE-free tests of the piano roll's note editing (app/src/model/NoteEditing, C2).

#include "model/NoteEditing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
Note note(int pitch, double start, double length = 0.25, float velocity = 100.0f / 127.0f)
{
    return {pitch, start, length, velocity};
}

const BeatGrid sixteenths{.step = 0.25, .origin = 0.0};
const BeatGrid noGrid{.step = 0.0, .origin = 0.0};
} // namespace

TEST_CASE("the beat grid snaps on the song's grid, not the clip's", "[fast]")
{
    CHECK(sixteenths.round(0.3) == Approx(0.25));
    CHECK(sixteenths.floor(0.49) == Approx(0.25));
    CHECK(sixteenths.floor(0.5) == Approx(0.5)); // on a line: that line
    CHECK(sixteenths.next(0.5) == Approx(0.75));
    CHECK(noGrid.round(0.3) == 0.3);

    // A clip whose content starts at song beat 0.1: lines at content beats -0.1, 0.15, 0.4...
    const BeatGrid offset{.step = 0.25, .origin = 0.1};
    CHECK(offset.round(0.2) == Approx(0.15));
    CHECK(offset.floor(0.39) == Approx(0.15));
}

TEST_CASE("note geometry and hit zones", "[fast]")
{
    const NoteGeometry g{.originX = 100.0, .pixelsPerBeat = 80.0, .topEdge = 72.0, .rowHeight = 10.0};
    CHECK(g.xForBeat(1.0) == Approx(180.0));
    CHECK(g.beatForX(180.0) == Approx(1.0));
    CHECK(g.yForPitch(71) == Approx(0.0));
    CHECK(g.pitchForY(0.0) == 71);
    CHECK(g.pitchForY(9.99) == 71);
    CHECK(g.pitchForY(10.0) == 70);
    CHECK(g.pitchForY(-5.0) == 72);

    const std::vector<Note> notes{note(70, 0.0, 1.0), note(71, 1.0, 0.05)};
    const double y70 = g.yForPitch(70) + 5.0;
    auto hit = hitNote(notes, g, 140.0, y70);
    REQUIRE(hit);
    CHECK(hit->index == 0);
    CHECK(hit->zone == NoteHit::Zone::Body);
    CHECK(hitNote(notes, g, 101.0, y70)->zone == NoteHit::Zone::StartEdge);
    CHECK(hitNote(notes, g, 178.0, y70)->zone == NoteHit::Zone::EndEdge);
    CHECK_FALSE(hitNote(notes, g, 181.0, y70));
    CHECK_FALSE(hitNote(notes, g, 140.0, y70 + 10.0)); // the row below

    // A 4 px note: edges shrink to a third so its body can still be grabbed.
    hit = hitNote(notes, g, 182.0, g.yForPitch(71) + 5.0);
    REQUIRE(hit);
    CHECK(hit->index == 1);
    CHECK(hit->zone == NoteHit::Zone::Body);
}

TEST_CASE("moving notes snaps the grabbed note; the others follow", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(64, 0.5), note(67, 1.1)};
    auto out = dragNotes(notes, {0, 2}, {.kind = NoteDragKind::Move, .anchor = 0, .deltaBeats = 0.6, .deltaPitch = 2},
                         sixteenths);
    // Anchor 0.0 + 0.6 snaps to 0.5; the other note moves by the same 0.5 (kept off the grid).
    REQUIRE(out.notes.size() == 3);
    CHECK(out.notes[0] == note(62, 0.5));
    CHECK(out.notes[1] == note(64, 0.5));
    CHECK(out.notes[2].pitch == 69);
    CHECK(out.notes[2].start == Approx(1.6));
    CHECK(out.selection == std::set<std::size_t>{0, 2});

    SECTION("never before beat 0 or outside 0..127")
    {
        const std::vector<Note> edge{note(126, 0.25), note(120, 1.0)};
        out = dragNotes(edge, {0, 1}, {.kind = NoteDragKind::Move, .anchor = 1, .deltaBeats = -3.0, .deltaPitch = 5}, sixteenths);
        CHECK(out.notes[0].start == Approx(0.0));
        CHECK(out.notes[0].pitch == 127);
        CHECK(out.notes[1].pitch == 121);
        CHECK(out.notes[1].start == Approx(0.75));
    }
    SECTION("an anchor outside the selection does nothing")
    {
        out = dragNotes(notes, {0}, {.kind = NoteDragKind::Move, .anchor = 1, .deltaBeats = 1.0}, sixteenths);
        CHECK(out.notes == notes);
    }
}

TEST_CASE("resizing notes", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0, 1.0), note(62, 1.0, 0.5)};
    SECTION("the end snaps; the others change by the same amount")
    {
        const auto out = dragNotes(notes, {0, 1}, {.kind = NoteDragKind::ResizeEnd, .anchor = 0, .deltaBeats = 0.4}, sixteenths);
        CHECK(out.notes[0].length == Approx(1.5));
        CHECK(out.notes[1].length == Approx(1.0));
    }
    SECTION("never shorter than a grid step (the anchor) or minNoteLength (the others)")
    {
        const auto out = dragNotes(notes, {0, 1}, {.kind = NoteDragKind::ResizeEnd, .anchor = 0, .deltaBeats = -5.0}, sixteenths);
        CHECK(out.notes[0].length == Approx(0.25));
        CHECK(out.notes[1].length == Approx(minNoteLength));
        const auto loose = dragNotes(notes, {0}, {.kind = NoteDragKind::ResizeEnd, .anchor = 0, .deltaBeats = -5.0}, noGrid);
        CHECK(loose.notes[0].length == Approx(minNoteLength));
    }
    SECTION("the start moves; the end stays")
    {
        auto out = dragNotes(notes, {0}, {.kind = NoteDragKind::ResizeStart, .anchor = 0, .deltaBeats = 0.3}, sixteenths);
        CHECK(out.notes[0].start == Approx(0.25));
        CHECK(out.notes[0].length == Approx(0.75));
        out = dragNotes(notes, {0}, {.kind = NoteDragKind::ResizeStart, .anchor = 0, .deltaBeats = 3.0}, sixteenths);
        CHECK(out.notes[0].start == Approx(0.75)); // one step before its end at most
        out = dragNotes(notes, {1}, {.kind = NoteDragKind::ResizeStart, .anchor = 1, .deltaBeats = -4.0}, sixteenths);
        CHECK(out.notes[1].start == Approx(0.0)); // never before 0 (after the C at 0 in order)
        CHECK(out.notes[1].pitch == 62);
        CHECK(out.notes[1].length == Approx(1.5));
    }
}

TEST_CASE("same-pitch overlaps: the placed note wins", "[fast]")
{
    // C at 0..1 and C at 2..3; E at 0.5..1.5 (another pitch: untouched).
    const std::vector<Note> notes{note(60, 0.0, 1.0), note(64, 0.5, 1.0), note(60, 2.0, 1.0)};
    SECTION("a note placed over the start of another shortens it")
    {
        const auto out = addNote(notes, note(60, 0.5, 0.25));
        REQUIRE(out.notes.size() == 4);
        CHECK(out.notes[0] == note(60, 0.0, 0.5));
        CHECK(out.notes[1] == note(60, 0.5, 0.25));
        CHECK(out.selection == std::set<std::size_t>{1});
    }
    SECTION("a note that starts inside the placed one is removed")
    {
        const auto out = dragNotes(notes, {0}, {.kind = NoteDragKind::ResizeEnd, .anchor = 0, .deltaBeats = 1.5}, sixteenths);
        REQUIRE(out.notes.size() == 2);
        CHECK(out.notes[0] == note(60, 0.0, 2.5));
        CHECK(out.notes[1].pitch == 64);
    }
    SECTION("among placed notes the later one wins")
    {
        const std::vector<Note> two{note(60, 0.0, 1.0), note(60, 1.0, 1.0)};
        const auto out = dragNotes(two, {0, 1}, {.kind = NoteDragKind::ResizeEnd, .anchor = 1, .deltaBeats = 1.0}, sixteenths);
        CHECK(out.notes[0] == note(60, 0.0, 1.0)); // trimmed back to where the next starts
        CHECK(out.notes[1] == note(60, 1.0, 2.0));
        CHECK(out.selection.size() == 2);
    }
    SECTION("overlaps between notes that weren't placed stay")
    {
        const std::vector<Note> imported{note(60, 0.0, 2.0), note(60, 1.0, 2.0)};
        CHECK(resolveOverlaps(imported, {}).notes == imported);
    }
}

TEST_CASE("draw, delete, transpose, nudge", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(64, 0.5), note(67, 1.0)};
    auto out = addNote(notes, note(72, 0.25, 0.25, 0.5f));
    CHECK(out.notes.size() == 4);
    CHECK(out.notes[1].pitch == 72);
    CHECK(out.notes[1].velocity == midiVelocity(0.5f));
    CHECK(out.selection == std::set<std::size_t>{1});

    out = deleteNotes(notes, {0, 2});
    CHECK(out.notes == std::vector<Note>{note(64, 0.5)});
    CHECK(out.selection.empty());

    out = transposeNotes(notes, {1, 2}, 12);
    CHECK(out.notes[1].pitch == 76);
    CHECK(out.notes[2].pitch == 79);
    CHECK(transposeNotes(notes, {2}, 100).notes[2].pitch == 127);
    CHECK(transposeNotes(notes, {0}, -100).notes[0].pitch == 0);

    out = nudgeNotes(notes, {1}, 0.25);
    CHECK(out.notes[1].start == Approx(0.75));
    out = nudgeNotes(notes, {0, 1}, -1.0); // limited by the earliest note
    CHECK(out.notes[0].start == Approx(0.0));
    CHECK(out.notes[1].start == Approx(0.5));
}

TEST_CASE("duplicating notes keeps a phrase on its beats", "[fast]")
{
    SECTION("a bar-long phrase moves by whole bars")
    {
        // Ends at 3.5 (a short last note): copies still start at the next bar.
        const std::vector<Note> phrase{note(60, 0.0), note(62, 1.0), note(64, 3.25)};
        const auto out = duplicateNotes(phrase, {0, 1, 2}, 0.25, 4.0);
        REQUIRE(out.notes.size() == 6);
        CHECK(out.notes[3].start == Approx(4.0));
        CHECK(out.notes[5].start == Approx(7.25));
        CHECK(out.selection == std::set<std::size_t>{3, 4, 5});
    }
    SECTION("a short figure moves by its span on the grid")
    {
        const std::vector<Note> figure{note(60, 0.0, 0.25), note(62, 0.25, 0.2)};
        const auto out = duplicateNotes(figure, {0, 1}, 0.25, 4.0);
        CHECK(out.notes[2].start == Approx(0.5));
        CHECK(out.notes[3].start == Approx(0.75));
    }
}

TEST_CASE("quantise moves starts to the grid", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.1, 0.3), note(62, 0.6), note(64, 1.12)};
    auto out = quantiseNotes(notes, {}, sixteenths); // nothing selected: every note
    CHECK(out.notes[0].start == Approx(0.0));
    CHECK(out.notes[0].length == Approx(0.3)); // lengths kept
    CHECK(out.notes[1].start == Approx(0.5));
    CHECK(out.notes[2].start == Approx(1.0));
    CHECK(out.selection.empty());

    out = quantiseNotes(notes, {2}, sixteenths);
    CHECK(out.notes[0].start == Approx(0.1));
    CHECK(out.notes[2].start == Approx(1.0));
    CHECK(out.selection == std::set<std::size_t>{2});
}

TEST_CASE("box selection and velocity editing", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(64, 0.5), note(67, 1.0)};
    CHECK(notesInBox(notes, 0.4, 2.0, 62, 70) == std::set<std::size_t>{1, 2});
    CHECK(notesInBox(notes, 2.0, 0.4, 70, 62) == std::set<std::size_t>{1, 2}); // any order
    CHECK(notesInBox(notes, 0.26, 0.4, 0, 127).empty());

    auto v = paintLevel(notes, 0.4, 1.0, 0.5f);
    CHECK(v[0].velocity == notes[0].velocity);
    CHECK(v[1].velocity == midiVelocity(0.5f));
    CHECK(v[2].velocity == midiVelocity(0.5f));
    const std::set<std::size_t> only{2};
    v = paintLevel(notes, 0.0, 2.0, 0.25f, &only);
    CHECK(v[0].velocity == notes[0].velocity);
    CHECK(v[2].velocity == midiVelocity(0.25f));

    v = offsetLevel(notes, {0, 1}, 0.5f);
    CHECK(v[0].velocity == 1.0f);
    v = offsetLevel(notes, {0}, -2.0f);
    CHECK(v[0].velocity == 1.0f / 127.0f); // never 0 (a note-on with velocity 0 is a note-off)
}

TEST_CASE("velocities are MIDI steps", "[fast]")
{
    CHECK(midiVelocity(1.0f) == 1.0f);
    CHECK(midiVelocity(0.0f) == 1.0f / 127.0f);
    CHECK(midiVelocity(0.5f) == 64.0f / 127.0f);
    for (int k = 1; k <= 127; ++k)
        CHECK(midiVelocity(static_cast<float>(k) / 127.0f) == static_cast<float>(k) / 127.0f);
}

TEST_CASE("notes past a clip's end lengthen it to that bar, unless a clip is in the way", "[fast]")
{
    const TempoMap tempo; // 120 BPM, 4/4: a bar is 2 s
    ClipInfo clip;
    clip.id = 1;
    clip.kind = ClipInfo::Kind::Midi;
    clip.start = 2.0; // bar 2
    clip.length = 2.0;
    const std::vector<ClipInfo> alone{clip};

    // Inside the clip: nothing to do.
    CHECK_FALSE(lengthenedClipEnd(clip, alone, {note(60, 3.0, 1.0)}, tempo));
    // Ending in the next bar (content beat 5 = 4.5 s, in bar 3, which ends at 6 s): to its end.
    auto end = lengthenedClipEnd(clip, alone, {note(60, 4.5, 0.5)}, tempo);
    REQUIRE(end);
    CHECK(*end == Approx(6.0));
    // A note ending exactly on a bar line (beat 8 = 6 s) ends the clip there, not a bar later.
    end = lengthenedClipEnd(clip, alone, {note(60, 4.0, 4.0)}, tempo);
    REQUIRE(end);
    CHECK(*end == Approx(6.0));
    // A trimmed start (offset) shifts where the notes are.
    ClipInfo trimmed = clip;
    trimmed.offset = 1.0; // content starts at 1 s
    end = lengthenedClipEnd(trimmed, {trimmed}, {note(60, 6.0, 0.5)}, tempo); // ends at 1 + 3.25 = 4.25 s
    REQUIRE(end);
    CHECK(*end == Approx(6.0));

    // Another clip after it, inside the new range: leave the clip as it is.
    ClipInfo next;
    next.id = 2;
    next.start = 5.0;
    next.length = 1.0;
    CHECK_FALSE(lengthenedClipEnd(clip, {clip, next}, {note(60, 4.5, 0.5)}, tempo));
    // ...but one further away doesn't matter.
    next.start = 6.0;
    CHECK(lengthenedClipEnd(clip, {clip, next}, {note(60, 4.5, 0.5)}, tempo));
}
