// JUCE-free tests of the piano roll's tools (app/src/model/NoteTools, docs/design/piano-roll.md).

#include "model/AudioToolkit.h"
#include "model/NoteTools.h"
#include "model/PianoRollOptions.h"

#include <algorithm>
#include <bitset>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
Note note(int pitch, double start, double length = 1.0, float velocity = 100.0f / 127.0f)
{
    return {pitch, start, length, velocity};
}

const fd::music::Key cMajor{0, "major"};
const BeatGrid sixteenths{.step = 0.25, .origin = 0.0, .bar = 4.0};

std::vector<int> pitchesOf(const std::vector<Note>& notes)
{
    std::vector<int> out;
    for (const auto& n : notes)
        out.push_back(n.pitch);
    return out;
}

std::vector<int> sortedPitches(const std::vector<Note>& notes, double start)
{
    std::vector<int> out;
    for (const auto& n : notes)
        if (std::abs(n.start - start) < 1e-9)
            out.push_back(n.pitch);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Note> chord(std::vector<int> pitches, double start, double length = 1.0)
{
    std::vector<Note> out;
    for (int p : pitches)
        out.push_back(note(p, start, length));
    return out;
}

std::vector<Note> concat(std::vector<Note> a, const std::vector<Note>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end(), noteOrder);
    return a;
}
} // namespace

TEST_CASE("In key: the key's rows, and those of notes outside it", "[fast]")
{
    const std::vector<Note> notes{note(61, 0.0), note(64, 1.0)};
    const auto has = [](const std::vector<int>& rows, int pitch)
    { return std::find(rows.begin(), rows.end(), pitch) != rows.end(); };
    const auto rows = keyRows(&cMajor, notes);
    CHECK(has(rows, 60));
    CHECK(has(rows, 61)); // a note outside the key keeps its row
    CHECK_FALSE(has(rows, 63));
    CHECK(std::is_sorted(rows.begin(), rows.end()));
    CHECK(rows.size() == 75 + 1); // the key's 75 pitches in 0..127, and C♯4
    // A preview's notes outside the key show too.
    const std::vector<Note> preview{note(70, 0.0)};
    CHECK(has(keyRows(&cMajor, notes, &preview), 70));
    CHECK(keyRows(nullptr, notes).size() == 128); // no key: every pitch
}

TEST_CASE("Folded geometry maps rows to the pitches shown", "[fast]")
{
    const std::vector<int> rows{60, 62, 64, 65};
    const NoteGeometry g{.topEdge = 4.0, .rowHeight = 10.0, .rows = &rows};
    CHECK(g.yForPitch(65) == Approx(0.0));
    CHECK(g.yForPitch(60) == Approx(30.0));
    CHECK(g.pitchForY(5.0) == 65);
    CHECK(g.pitchForY(35.0) == 60);
    CHECK(g.pitchForY(500.0) == 60); // past the end: the last row
    CHECK(g.rowCount() == 4);
    const std::vector<Note> notes{note(62, 0.0)};
    CHECK(hitNote(notes, g, 10.0, 25.0)->index == 0);
}

TEST_CASE("Dragging through the rows shown", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(64, 0.0), note(67, 0.0)};
    const std::set<std::size_t> all{0, 1, 2};
    SECTION("In key: by the key's rows, every note as many")
    {
        const auto rows = keyRows(&cMajor, notes);
        auto out = dragNotes(notes, all, {.anchor = 0, .deltaPitch = 1, .rows = &rows}, sixteenths);
        CHECK(pitchesOf(out.notes) == std::vector<int>{62, 65, 69}); // C E G up a row: D F A
        out = dragNotes(notes, all, {.anchor = 0, .deltaPitch = -1, .rows = &rows}, sixteenths);
        CHECK(pitchesOf(out.notes) == std::vector<int>{59, 62, 65}); // down a row: B D F
    }
    SECTION("By any rows shown")
    {
        const std::vector<int> rows{60, 64, 67, 72};
        const auto out = dragNotes(notes, {0}, {.anchor = 0, .deltaPitch = 2, .rows = &rows}, sixteenths);
        CHECK(std::count_if(out.notes.begin(), out.notes.end(),
                            [](const Note& n) { return n.pitch == 67; }) == 1);
        const auto limited = dragNotes(notes, all, {.anchor = 0, .deltaPitch = 5, .rows = &rows}, sixteenths);
        CHECK(pitchesOf(limited.notes) == std::vector<int>{64, 67, 72}); // the top row stops it
    }
    SECTION("Ctrl+drag copies")
    {
        const auto out = dragNotes(notes, {0}, {.anchor = 0, .deltaBeats = 2.0, .copy = true}, sixteenths);
        REQUIRE(out.notes.size() == 4);
        CHECK(out.notes[3].start == Approx(2.0));
        CHECK(out.selection == std::set<std::size_t>{3});
    }
}

TEST_CASE("The arrows move by steps of the scale", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(64, 1.0)};
    auto out = transposeInKey(notes, {0, 1}, cMajor, 1);
    CHECK(pitchesOf(out.notes) == std::vector<int>{62, 65});
    CHECK(out.selection == std::set<std::size_t>{0, 1});
    out = transposeInKey(notes, {}, cMajor, -2); // nothing selected: every note
    CHECK(pitchesOf(out.notes) == std::vector<int>{57, 60});
    CHECK(out.selection.empty());
    const std::vector<Note> top{note(127, 0.0)};
    CHECK(pitchesOf(transposeInKey(top, {0}, cMajor, 1).notes) == std::vector<int>{127});
}

TEST_CASE("The pads: a row for each pad with a sound, and one for each pitch with notes and no pad", "[fast]")
{
    std::bitset<128> pads;
    for (int p : {36, 38, 42})
        pads.set(static_cast<std::size_t>(p));
    const std::vector<Note> notes{note(36, 0.0), note(40, 1.0)}; // E1 has no pad
    CHECK(padRows(pads, notes) == std::vector<int>{36, 38, 40, 42});
    const std::vector<Note> preview{note(45, 0.0)};
    CHECK(padRows(pads, notes, &preview) == std::vector<int>{36, 38, 40, 42, 45});
    CHECK(padRows({}, {}).empty()); // an empty kit, an empty clip: no rows
}

TEST_CASE("The pads: the arrows move a row, as a group, kept on the rows", "[fast]")
{
    const std::vector<int> rows{36, 38, 42, 46};
    const std::vector<Note> notes{note(36, 0.0), note(38, 1.0)};
    auto out = transposeInRows(notes, {0, 1}, rows, 1);
    CHECK(pitchesOf(out.notes) == std::vector<int>{38, 42}); // kick to snare, snare to hat
    CHECK(out.selection == std::set<std::size_t>{0, 1});
    out = transposeInRows(notes, {}, rows, 2); // nothing selected: every note
    CHECK(pitchesOf(out.notes) == std::vector<int>{42, 46});
    // The top row stops the group as far as it can go.
    CHECK(pitchesOf(transposeInRows(notes, {0, 1}, rows, 5).notes) == std::vector<int>{42, 46});
    CHECK(pitchesOf(transposeInRows(notes, {0}, rows, -1).notes) == std::vector<int>{36, 38}); // stays
}

TEST_CASE("Fit to key", "[fast]")
{
    // F♯ after a rising step (from E) goes up to G; after a falling one (from A), down to F.
    auto out = fitToKey({note(64, 0.0), note(66, 1.0)}, {}, cMajor);
    CHECK(pitchesOf(out.notes) == std::vector<int>{64, 67});
    out = fitToKey({note(69, 0.0), note(66, 1.0)}, {}, cMajor);
    CHECK(pitchesOf(out.notes) == std::vector<int>{69, 65});
    out = fitToKey({note(66, 0.0)}, {}, cMajor); // no note before: down
    CHECK(pitchesOf(out.notes) == std::vector<int>{65});
    const fd::music::Key pentatonic{0, "major-pentatonic"};
    out = fitToKey({note(65, 0.0)}, {}, pentatonic); // F: E is nearer than G
    CHECK(pitchesOf(out.notes) == std::vector<int>{64});
}

TEST_CASE("The chord lane reads chords from the notes", "[fast]")
{
    const auto notes = concat(concat(chord({57, 60, 64}, 0.0, 4.0), chord({53, 57, 60}, 4.0, 4.0)),
                              concat(chord({60, 64, 67}, 8.0, 4.0), {note(72, 8.0, 0.5)}));
    const auto spans = chordSpans(notes);
    REQUIRE(spans.size() == 3);
    CHECK(fd::music::chordName(spans[0].chord, &cMajor) == "Am");
    CHECK(spans[0].start == Approx(0.0));
    CHECK(spans[0].end == Approx(4.0));
    CHECK(fd::music::chordName(spans[1].chord, &cMajor) == "F");
    CHECK(spans[1].notes.size() == 3);
    CHECK(fd::music::chordName(spans[2].chord, &cMajor) == "C"); // the doubled C is the same chord
    CHECK(spans[2].end == Approx(12.0));
    SECTION("Stabs of one chord are one chord")
    {
        std::vector<Note> stabs;
        for (int k = 0; k < 4; ++k)
            stabs = concat(stabs, chord({60, 64, 67}, k * 1.0, 0.5));
        const auto s = chordSpans(stabs);
        REQUIRE(s.size() == 1);
        CHECK(s[0].notes.size() == 12);
        CHECK(s[0].end == Approx(3.5));
    }
    SECTION("A melody, two notes, or muted notes make none")
    {
        CHECK(chordSpans({note(60, 0.0), note(62, 1.0), note(64, 2.0)}).empty());
        CHECK(chordSpans(chord({60, 67}, 0.0)).empty());
        auto muted = chord({60, 64, 67}, 0.0);
        muted[1].muted = true;
        CHECK(chordSpans(muted).empty());
    }
}

TEST_CASE("Drawing chords", "[fast]")
{
    const ChordStyle style{};
    SECTION("Chord: the key's chord on the note, which stays the root")
    {
        const auto out = drawChord({}, note(62, 0.0), &cMajor, style, false);
        CHECK(sortedPitches(out.notes, 0.0) == std::vector<int>{62, 65, 69}); // Dm
        CHECK(out.selection.size() == 3);
    }
    SECTION("Voice-led from the chord before, keeping the clicked note")
    {
        const auto before = chord({60, 64, 67}, 0.0);
        const auto out = drawChord(before, note(53, 1.0), &cMajor, style, false); // F under C
        const auto f = sortedPitches(out.notes, 1.0);
        CHECK(std::find(f.begin(), f.end(), 53) != f.end());
        CHECK(f.size() == 3);
        const auto closeOut =
            drawChord(before, note(53, 1.0), &cMajor, {.voicing = fd::music::Voicing::Close}, false);
        CHECK(sortedPitches(closeOut.notes, 1.0) == std::vector<int>{53, 57, 60});
    }
    SECTION("Chord under: the note on top of the key's chord with it")
    {
        const auto out = drawChord({}, note(64, 0.0), &cMajor, style, true); // E: C major under it
        CHECK(sortedPitches(out.notes, 0.0) == std::vector<int>{55, 60, 64});
    }
    SECTION("Sevenths")
    {
        const auto out =
            drawChord({}, note(67, 0.0), &cMajor, {.shape = fd::music::ChordShape::Seventh}, false);
        CHECK(sortedPitches(out.notes, 0.0) == std::vector<int>{67, 71, 74, 77});
    }
}

TEST_CASE("Writing a chord over a place, or in place of one", "[fast]")
{
    const auto notes = concat(chord({60, 64, 67}, 0.0, 2.0), chord({60, 64, 67}, 2.0, 2.0));
    SECTION("Replacing keeps the notes' timing and velocities")
    {
        auto replaced = notes;
        replaced[3].velocity = 50.0f / 127.0f; // the second chord, lowest note (indices 3..5)
        const auto out = writeChord(replaced, 2.0, 4.0, {.root = 7, .kind = 0}, {3, 4, 5}, {}, 1.0f);
        CHECK(sortedPitches(out.notes, 2.0) == std::vector<int>{59, 62, 67}); // G/B: least movement from C
        CHECK(out.selection.size() == 3);
        for (auto i : out.selection)
            CHECK(out.notes[i].length == Approx(2.0));
        const auto low =
            std::find_if(out.notes.begin(), out.notes.end(), [](const Note& n) { return n.pitch == 59; });
        CHECK(low->velocity == Approx(50.0f / 127.0f));
    }
    SECTION("A seventh in place of a triad gets its fourth note")
    {
        const auto out = writeChord(notes, 2.0, 4.0, {.root = 7, .kind = 6}, {3, 4, 5}, {}, 1.0f);
        CHECK(sortedPitches(out.notes, 2.0).size() == 4);
    }
    SECTION("In an empty place: new notes")
    {
        const auto out = writeChord(notes, 4.0, 5.0, {.root = 9, .kind = 1}, {}, {}, 0.5f);
        CHECK(sortedPitches(out.notes, 4.0) == std::vector<int>{60, 64, 69}); // Am/C after C
        CHECK(out.notes.size() == 9);
    }
    SECTION("The voicing it would write")
    {
        CHECK(chordVoicing(notes, 4.0, {.root = 5, .kind = 0}, {}, {}) == std::vector<int>{60, 65, 69});
        CHECK(chordVoicing(notes, 4.0, {.root = 5, .kind = 0}, {}, {.voicing = fd::music::Voicing::Close}) ==
              std::vector<int>{65, 69, 72});
    }
}

TEST_CASE("Make chords: a line becomes a progression", "[fast]")
{
    const std::vector<Note> line{note(57, 0.0), note(53, 1.0), note(60, 2.0), note(55, 3.0)};
    const auto out = makeChords(line, {}, &cMajor, {});
    CHECK(out.notes.size() == 12);
    CHECK(out.selection.empty()); // made from every note: nothing was selected
    for (int k = 0; k < 4; ++k)
    {
        const auto pitches = sortedPitches(out.notes, k);
        INFO(k);
        REQUIRE(pitches.size() == 3);
        CHECK(std::find(pitches.begin(), pitches.end(), line[static_cast<std::size_t>(k)].pitch) !=
              pitches.end());
        const auto named = fd::music::nameChord(pitches);
        REQUIRE(named);
        CHECK(named->root ==
              line[static_cast<std::size_t>(k)].pitch % 12); // Am F C G, inverted as Smooth likes
    }
    const auto close = makeChords(line, {0}, &cMajor, {.voicing = fd::music::Voicing::Close});
    CHECK(sortedPitches(close.notes, 0.0) == std::vector<int>{57, 60, 64});
    CHECK(close.selection.size() == 3);
}

TEST_CASE("Inverting, voicing and a bass", "[fast]")
{
    const auto c = chord({60, 64, 67}, 0.0);
    CHECK(sortedPitches(invertChords(c, {}, true).notes, 0.0) == std::vector<int>{64, 67, 72});
    CHECK(sortedPitches(invertChords(c, {}, false).notes, 0.0) == std::vector<int>{55, 60, 64});
    CHECK(sortedPitches(invertChords({note(60, 0.0)}, {}, true).notes, 0.0) ==
          std::vector<int>{60}); // not a chord
    SECTION("Voicings keep the count and register")
    {
        const auto cmaj7 = chord({60, 64, 67, 71}, 0.0);
        CHECK(sortedPitches(voiceChords(cmaj7, {}, fd::music::Voicing::Drop2).notes, 0.0) ==
              std::vector<int>{55, 60, 64, 71});
        const auto inverted = chord({64, 67, 72}, 0.0);
        CHECK(sortedPitches(voiceChords(inverted, {}, fd::music::Voicing::Close).notes, 0.0) ==
              std::vector<int>{60, 64, 67}); // root position, near where it was
    }
    SECTION("Smooth voicing keeps the first chord and moves least after it")
    {
        const auto progression = concat(chord({60, 64, 67}, 0.0), chord({65, 69, 72}, 1.0));
        const auto out = voiceChords(progression, {}, fd::music::Voicing::Smooth);
        CHECK(sortedPitches(out.notes, 0.0) == std::vector<int>{60, 64, 67});
        CHECK(sortedPitches(out.notes, 1.0) == std::vector<int>{60, 65, 69});
    }
    SECTION("Add bass: the root an octave or more under")
    {
        const auto out = addBass(chord({64, 67, 72}, 0.0, 2.0), {});
        REQUIRE(out.notes.size() == 4);
        CHECK(out.notes[0].pitch == 48); // C/E: its root, C, under the E
        CHECK(out.notes[0].length == Approx(2.0));
        CHECK(out.selection == std::set<std::size_t>{0});
    }
}

TEST_CASE("Join, split, legato, fixed length", "[fast]")
{
    auto out = joinNotes({note(60, 0.0, 1.0), note(60, 1.0, 1.0), note(60, 3.0, 1.0), note(62, 1.0)}, {});
    REQUIRE(out.notes.size() == 3);
    CHECK(out.notes[0].length == Approx(2.0));
    CHECK(out.notes[2].start == Approx(3.0));

    out = splitNotes({note(60, 0.0, 2.0), note(62, 1.5, 0.5)}, {0, 1}, 1.0);
    REQUIRE(out.notes.size() == 3);
    CHECK(out.notes[0].length == Approx(1.0));
    CHECK(out.notes[1].start == Approx(1.0));
    CHECK(out.selection.size() == 3);

    out =
        legatoNotes({note(60, 0.0, 0.25), note(64, 0.0, 0.25), note(62, 1.5, 0.25), note(65, 3.0, 0.5)}, {});
    CHECK(out.notes[0].length == Approx(1.5));
    CHECK(out.notes[1].length == Approx(1.5));
    CHECK(out.notes[2].length == Approx(1.5));
    CHECK(out.notes[3].length == Approx(0.5)); // the last keeps its own

    out = fixedLength({note(60, 0.0, 3.0), note(62, 1.0, 0.1)}, {}, 0.25);
    CHECK(out.notes[0].length == Approx(0.25));
    CHECK(out.notes[1].length == Approx(0.25));
}

TEST_CASE("Stretching, reversing and mirroring", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0, 1.0), note(62, 1.0, 1.0), note(64, 2.0, 2.0)};
    auto out = stretchNotes(notes, {}, 0.0, 0.5, true); // double speed
    CHECK(out.notes[1].start == Approx(0.5));
    CHECK(out.notes[2].length == Approx(1.0));
    out = stretchNotes(notes, {}, 0.0, 2.0, true); // half speed
    CHECK(out.notes[2].start == Approx(4.0));
    out = stretchNotes(notes, {}, 0.0, 1.5, false); // a time handle without Alt: lengths stay
    CHECK(out.notes[2].start == Approx(3.0));
    CHECK(out.notes[2].length == Approx(2.0));

    out = reverseNotes(notes, {});
    CHECK(pitchesOf(out.notes) == std::vector<int>{64, 62, 60});
    CHECK(out.notes[0].start == Approx(0.0));
    CHECK(out.notes[1].start == Approx(2.0));
    CHECK(out.notes[2].start == Approx(3.0));

    out = stretchNotes(notes, {}, 4.0, -1.0, true); // a handle dragged past the other end
    CHECK(pitchesOf(out.notes) == std::vector<int>{64, 62, 60});

    out = mirrorPitches({note(60, 0.0), note(62, 1.0), note(67, 2.0)}, {}, nullptr);
    CHECK(pitchesOf(out.notes) == std::vector<int>{67, 65, 60});
    out = mirrorPitches({note(60, 0.0), note(62, 1.0), note(67, 2.0)}, {}, &cMajor); // C D G: steps 0 1 4
    CHECK(pitchesOf(out.notes) == std::vector<int>{67, 65, 60});                     // → 4 3 0: G F C
    out = mirrorPitches({note(60, 0.0), note(64, 1.0), note(65, 2.0)}, {}, &cMajor); // C E F: 0 2 3
    CHECK(pitchesOf(out.notes) == std::vector<int>{65, 62, 60});                     // → 3 1 0: F D C
}

TEST_CASE("A velocity line", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(62, 1.0), note(64, 2.0), note(65, 5.0)};
    const auto out = lineLevel(notes, 0.0, 0.0f, 2.0, 1.0f);
    CHECK(out[0].velocity == Approx(1.0f / 127.0f)); // at least the lowest step
    CHECK(out[1].velocity == Approx(64.0f / 127.0f));
    CHECK(out[2].velocity == Approx(1.0f));
    CHECK(out[3].velocity == notes[3].velocity); // past the line
    const std::set<std::size_t> only{1};
    CHECK(lineLevel(notes, 0.0, 0.0f, 2.0, 1.0f, &only)[2].velocity == notes[2].velocity);
}

TEST_CASE("Muting notes", "[fast]")
{
    std::vector<Note> notes{note(60, 0.0), note(62, 1.0)};
    auto out = muteNotes(notes, {0});
    CHECK(out.notes[0].muted);
    CHECK_FALSE(out.notes[1].muted);
    out = muteNotes(out.notes, {0});
    CHECK_FALSE(out.notes[0].muted); // all muted already: unmuted
    out = muteNotes(notes, {});
    CHECK((out.notes[0].muted && out.notes[1].muted));
}

TEST_CASE("Quantise with strength, swing and ends", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.1, 0.3), note(62, 0.3, 0.3)};
    auto out = quantiseNotes(notes, {}, sixteenths, {.strength = 0.5});
    CHECK(out.notes[0].start == Approx(0.05));
    CHECK(out.notes[1].start == Approx(0.275));
    out = quantiseNotes(notes, {}, sixteenths, {.swing = 1.0});
    CHECK(out.notes[0].start == Approx(0.0));
    CHECK(out.notes[1].start == Approx(0.25 + 0.25 / 3.0)); // the off line, a third of a step later
    out = quantiseNotes(notes, {}, sixteenths, {.starts = false, .ends = true});
    CHECK(out.notes[0].start == Approx(0.1));
    CHECK(out.notes[0].start + out.notes[0].length == Approx(0.5));
    CHECK(quantiseNotes(notes, {}, sixteenths).notes ==
          quantiseNotes(notes, {}, sixteenths, QuantiseSettings{}).notes);
}

TEST_CASE("Selecting", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0), note(61, 1.0), note(60, 2.0), note(64, 4.0)};
    CHECK(selectSamePitch(notes, {0}) == std::set<std::size_t>{0, 2});
    CHECK(selectOutsideKey(notes, cMajor) == std::set<std::size_t>{1});
    CHECK(selectStartingIn(notes, 1.0, 4.0) == std::set<std::size_t>{1, 2});
    CHECK(invertSelection(notes, {0, 1}) == std::set<std::size_t>{2, 3});
    CHECK(notesSpan(notes, {1, 3}) == std::pair{1.0, 5.0});
    CHECK_FALSE(notesSpan(notes, {}));
}

TEST_CASE("The clip view's options round-trip as view state", "[fast]")
{
    PianoRollOptions o;
    CHECK(pianoRollOptionsFromJson(pianoRollOptionsToJson(o)) == o);
    o.wholeRoll = {12, 0x7fff'ffff'ffffULL};
    o.others = false;
    o.draw = DrawMode::ChordUnder;
    o.chordLabels = ChordLabels::Steps;
    o.chord = {.shape = fd::music::ChordShape::Ninth,
               .voicing = fd::music::Voicing::Drop3,
               .inversion = fd::music::Inversion::Second};
    o.tools["quantise"] = toolValues(*findNoteTool("quantise"), {{"strength", 50.0}, {"moves", 1.0}});
    // The audio editor's Quantise, kept apart: its settings aren't the notes'.
    o.tools["audio.quantise"] =
        toolValues(*findAudioTool("quantise"), {{"strength", 75.0}, {"sensitivity", 30.0}});
    o.toolsOpen = true;
    o.toolsTab = ToolKind::Generate;
    CHECK(pianoRollOptionsFromJson(pianoRollOptionsToJson(o)) == o);
    // Kept to each tool's description: a value out of range, a tool or setting gone.
    const auto read = pianoRollOptionsFromJson(nlohmann::json{
        {"wholeRoll", "sideways"},
        {"tools", {{"strum", {{"time", 9000.0}, {"gone", 1.0}}}, {"nonsense", {{"x", 1.0}}}}}});
    CHECK(read.tools.at("strum").at("time") == 250.0);
    CHECK_FALSE(read.tools.at("strum").contains("gone"));
    CHECK_FALSE(read.tools.contains("nonsense"));
    CHECK(pianoRollOptionsFromJson(nlohmann::json()) == PianoRollOptions{});
    CHECK(std::string(drawModeName(DrawMode::Chord)) == "Draw chord");
}

TEST_CASE("The NOTES inspector: a value typed sets every note; dragged, they move together", "[fast]")
{
    const std::vector<Note> notes{note(60, 0.0, 1.0, 100.0f / 127.0f), note(72, 1.0, 0.5, 90.0f / 127.0f),
                                  note(64, 2.0)};
    const std::set<std::size_t> sel{0, 1};
    CHECK(fieldRange(notes, sel, NoteField::Pitch) == std::pair{60.0, 72.0});
    CHECK(fieldRange(notes, sel, NoteField::Velocity) == std::pair{90.0, 100.0});
    CHECK_FALSE(fieldRange(notes, {}, NoteField::Pitch));

    auto out = setNotesField(notes, sel, NoteField::Velocity, 64.0);
    CHECK(out.notes[0].velocity == Approx(64.0f / 127.0f));
    CHECK(out.notes[1].velocity == Approx(64.0f / 127.0f));
    CHECK(out.notes[2].velocity == notes[2].velocity);
    CHECK(out.selection == sel);
    out = setNotesField(notes, sel, NoteField::Length, 0.0);
    CHECK(out.notes[0].length == Approx(minNoteLength));

    out = offsetNotesField(notes, sel, NoteField::Pitch, 100.0); // as far as the top note can go
    CHECK(out.notes[0].pitch == 115);
    CHECK(out.notes[1].pitch == 127);
    out = offsetNotesField(notes, sel, NoteField::Start, -5.0);
    CHECK(out.notes[0].start == Approx(0.0));
    CHECK(out.notes[1].start == Approx(1.0));
    out = offsetNotesField(notes, sel, NoteField::Velocity, 40.0);
    CHECK(out.notes[0].velocity == Approx(127.0f / 127.0f));
    CHECK(out.notes[1].velocity == Approx(117.0f / 127.0f));
}

TEST_CASE("Make chords…: each note its own chord and voicing", "[fast]")
{
    using fd::music::Chord;
    const std::vector<Note> line{note(57, 0.0), note(53, 1.0), note(60, 2.0), note(55, 3.0)};
    const auto order = notesInTimeOrder(line, {});
    REQUIRE(order == std::vector<std::size_t>{0, 1, 2, 3});
    auto choices = startingChords(line, order, &cMajor, fd::music::ChordShape::Triad);
    REQUIRE(choices.size() == 4);
    CHECK(choices[0].chord == Chord{.root = 9, .kind = 1}); // Am
    // The second a seventh, the third C with its note on top, the last left as it is.
    choices[1].chord = fd::music::keyChord(&cMajor, 5, fd::music::ChordShape::Seventh);  // Fmaj7
    choices[2] = {.mode = NoteChord::Mode::Under, .chord = Chord{.root = 5, .kind = 0}}; // F, C on top
    choices[3].mode = NoteChord::Mode::Leave;
    choices[0].voicing = fd::music::Voicing::Close;
    const auto out = makeChords(line, order, choices);
    CHECK(sortedPitches(out.notes, 0.0) == std::vector<int>{57, 60, 64}); // Am, close, from A
    const auto f = sortedPitches(out.notes, 1.0);
    REQUIRE(f.size() == 4);
    CHECK(fd::music::nameChord(f)->kind == 7); // maj7
    CHECK(std::find(f.begin(), f.end(), 53) != f.end());
    const auto under = sortedPitches(out.notes, 2.0);
    REQUIRE(under.size() == 3);
    CHECK(under.back() == 60);
    CHECK(fd::music::nameChord(under)->root == 5);
    CHECK(sortedPitches(out.notes, 3.0) == std::vector<int>{55});
    CHECK(out.selection.size() == out.notes.size()); // the notes made are selected

    CHECK(chordsUnder(&cMajor, 4).size() == 2); // E: in C and Am, besides Em
    CHECK(shapeOf(Chord{.root = 0, .kind = 7}) == fd::music::ChordShape::Seventh);
    CHECK(shapeOf(Chord{.root = 0, .kind = 0}) == fd::music::ChordShape::Triad);
    CHECK(shapeOf(Chord{.root = 0, .kind = 17}) == fd::music::ChordShape::Add9);
}
