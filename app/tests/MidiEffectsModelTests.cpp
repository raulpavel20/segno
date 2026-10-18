#include "model/MidiEffects.h"
#include "model/SongHarmony.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fd::model;

TEST_CASE("MIDI effects: placed before the first module that plays notes", "[fast]")
{
    using R = ChainRole;
    const std::vector<R> chain{R::MidiEffect, R::Player, R::Other, R::Other};
    CHECK(placeInChain(chain, -1, R::MidiEffect) == 1); // added at the end: before the instrument
    CHECK(placeInChain(chain, 3, R::MidiEffect) == 1);  // dropped after it: just before it
    CHECK(placeInChain(chain, 0, R::MidiEffect) == 0);  // before it already: where it's asked
    CHECK(placeInChain(chain, 1, R::MidiEffect) == 1);
    CHECK(placeInChain(chain, 3, R::Other) == 3); // an audio effect: anywhere
    CHECK(placeInChain(chain, -1, R::Other) == -1);
    const std::vector<R> noInstrument{R::Other, R::Other};
    CHECK(placeInChain(noInstrument, -1, R::MidiEffect) == -1); // none: at the end
    CHECK(placeInChain(noInstrument, 1, R::MidiEffect) == 1);
    const std::vector<R> twoPlayers{R::Other, R::Player, R::Player};
    CHECK(placeInChain(twoPlayers, 2, R::MidiEffect) == 1); // the first one that plays
}

namespace
{
/// A song at 120 bpm (half a second a beat): a track of chords, C for a bar, a beat of G, then F;
/// a bass track; a section "Verse", then "Chorus" and "Chorus 2".
Snapshot song()
{
    Snapshot s;
    s.tempo.bpm = 120.0;
    TrackInfo chords{.id = 1, .name = "Keys"};
    ClipInfo c{.id = 10, .kind = ClipInfo::Kind::Midi, .start = 0.0, .length = 6.0};
    const auto triad = [&](double at, double length, std::initializer_list<int> pitches)
    {
        for (const int p : pitches)
            c.notes.push_back({.pitch = p, .start = at, .length = length});
    };
    triad(0.0, 4.0, {60, 64, 67});
    triad(4.0, 1.0, {62, 67, 71});
    triad(5.0, 4.0, {60, 65, 69});
    chords.clips.push_back(c);
    TrackInfo bass{.id = 2, .name = "Bass"};
    ClipInfo b{.id = 20, .kind = ClipInfo::Kind::Midi, .start = 0.0, .length = 6.0};
    b.notes.push_back({.pitch = 36, .start = 0.0, .length = 4.0});
    b.notes.push_back({.pitch = 41, .start = 5.0, .length = 4.0});
    bass.clips.push_back(b);
    s.tracks = {chords, bass};
    s.sections = {{.id = 1, .name = "Verse", .start = 0.0, .end = 2.0},
                  {.id = 2, .name = "Chorus", .start = 2.0, .end = 4.0},
                  {.id = 3, .name = "Chorus 2", .start = 4.0, .end = 6.0}};
    return s;
}
} // namespace

TEST_CASE("MIDI effects: the song's harmony from the tracks that make it, as fast as it may change", "[fast]")
{
    auto s = song();
    auto chords = songHarmony(s);
    REQUIRE(chords.size() == 3);
    CHECK(chords[0].root == 0);
    CHECK(chords[0].startBeat == 0.0);
    CHECK(chords[0].endBeat == 4.0);
    CHECK(chords[0].bass == 60); // the bass part has one note at a time: Auto leaves it out
    CHECK(chords[1].root == 7);  // a beat of G: chords may change each beat
    CHECK(chords[2].root == 5);
    CHECK(chords[2].notes == 0b001000100001); // F A C

    s.chordChange = ChordChange::Bar;
    chords = songHarmony(s);
    REQUIRE(chords.size() == 2); // the G is shorter than a bar: part of the C
    CHECK(chords[0].endBeat == 5.0);

    s.chordChange = ChordChange::Beat;
    s.tracks[1].harmony = HarmonyUse::On; // the bass counts: the chords' bass is its
    chords = songHarmony(s);
    REQUIRE(chords.size() == 3);
    CHECK(chords[0].bass == 36);
    CHECK(chords[2].bass == 41);

    s.tracks[0].harmony = HarmonyUse::Off;
    s.tracks[1].harmony = HarmonyUse::Auto;
    CHECK(songHarmony(s).empty());
}

TEST_CASE("MIDI effects: a track's song view, in beats", "[fast]")
{
    auto s = song();
    s.songChords = songHarmony(s);
    const auto v = songView(s, 2);
    REQUIRE(v.chords.size() == 3);
    CHECK(v.chords[1].start == 4.0);
    REQUIRE(v.sections.size() == 3);
    CHECK(v.sections[1].start == 4.0);
    CHECK(v.sections[1].name == v.sections[2].name); // "Chorus 2" is the chorus
    CHECK(v.sections[1].name != v.sections[0].name);
    CHECK(v.sections[1].time == 1);
    CHECK(v.sections[2].time == 2);
    REQUIRE(v.clips.size() == 1);
    CHECK(v.clips[0].end == 12.0);
    REQUIRE(v.notes.size() == 2); // the bass track's own notes
    CHECK(v.notes[1].key == 41);
    CHECK(v.notes[1].start == 5.0);
    CHECK(v.chordAt(4.5)->root == 7);
    CHECK(v.sectionAt(9.0)->time == 2);
}

TEST_CASE("MIDI effects: their settings' IDs", "[fast]")
{
    for (const auto use : {HarmonyUse::Auto, HarmonyUse::On, HarmonyUse::Off})
        CHECK(harmonyUseOf(idOf(use)) == use);
    for (const auto change : {ChordChange::Bar, ChordChange::HalfBar, ChordChange::Beat, ChordChange::Any})
        CHECK(chordChangeOf(idOf(change)) == change);
    CHECK_FALSE(harmonyUseOf("sometimes"));
    CHECK(shortestChord(ChordChange::Bar, 3, 4) == 3.0);
    CHECK(shortestChord(ChordChange::HalfBar, 6, 8) == 1.5);
    CHECK(shortestChord(ChordChange::Any, 4, 4) == 0.0);
}
