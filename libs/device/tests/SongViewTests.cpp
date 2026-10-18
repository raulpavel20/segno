#include "fd/device/NoteLedger.h"
#include "fd/device/SongView.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd;

TEST_CASE("song view: the chord at a beat, and the next one", "[fast]")
{
    SongView v;
    v.chords = {{.start = 0.0, .end = 4.0, .root = 0},
                {.start = 4.0, .end = 8.0, .root = 9},
                {.start = 10.0, .end = 12.0, .root = 5}};
    REQUIRE(v.chordAt(0.0) != nullptr);
    CHECK(v.chordAt(0.0)->root == 0);
    CHECK(v.chordAt(3.99)->root == 0);
    CHECK(v.chordAt(4.0)->root == 9);
    CHECK(v.chordAt(9.0) == nullptr); // a gap
    CHECK(v.chordAt(12.0) == nullptr);
    CHECK(v.chordAt(-1.0) == nullptr);
    REQUIRE(v.chordAfter(2.0) != nullptr);
    CHECK(v.chordAfter(2.0)->root == 9);
    CHECK(v.chordAfter(4.0)->root == 5); // one starting exactly here isn't "after"
    CHECK(v.chordAfter(10.0) == nullptr);
}

TEST_CASE("song view: sections, clips and the notes ahead", "[fast]")
{
    SongView v;
    v.sections = {{.start = 0.0, .end = 16.0, .name = 7, .time = 1}, {.start = 16.0, .end = 32.0, .name = 9}};
    v.clips = {{0.0, 8.0}, {12.0, 16.0}};
    v.notes = {{.start = 0.0, .key = 36},
               {.start = 1.0, .key = 38},
               {.start = 1.0, .key = 42},
               {.start = 3.5, .key = 36}};
    CHECK(v.sectionAt(15.9)->name == 7);
    CHECK(v.sectionAt(16.0)->name == 9);
    CHECK(v.inClip(7.9));
    CHECK_FALSE(v.inClip(8.0));
    CHECK(v.inClip(12.0));
    const auto ahead = v.notesFrom(1.0, 3.5);
    REQUIRE(ahead.size() == 2);
    CHECK(ahead[0].key == 38);
    CHECK(ahead[1].key == 42);
    CHECK(v.notesFrom(4.0, 8.0).empty());
}

TEST_CASE("note ledger: ends what was started, as often as it was started", "[fast]")
{
    EventList out;
    out.reserve(16);
    NoteLedger ledger;
    CHECK(ledger.write(out, {.type = EventType::NoteOn, .channel = 0, .key = 60, .value = 0.8f}));
    CHECK(ledger.write(out, {.type = EventType::NoteOn, .channel = 0, .key = 60, .value = 0.8f}));
    CHECK(ledger.write(out, {.type = EventType::NoteOn, .channel = 2, .key = 64, .value = 0.8f}));
    CHECK(ledger.sounding(0, 60));
    // A note-off for a note it never started is dropped, not written.
    CHECK(ledger.write(out, {.type = EventType::NoteOff, .channel = 0, .key = 61}));
    CHECK(out.size() == 3);
    CHECK(ledger.write(out, {.sampleOffset = 5, .type = EventType::NoteOff, .channel = 0, .key = 60}));
    CHECK(ledger.sounding(0, 60)); // started twice
    out.clear();
    ledger.endAll(out, 7);
    REQUIRE(out.size() == 2);
    for (const auto& e : out)
    {
        CHECK(e.type == EventType::NoteOff);
        CHECK(e.sampleOffset == 7);
    }
    CHECK_FALSE(ledger.any());
}
