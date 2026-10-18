#include "Chord.h"
#include "fd/testkit/DeviceRender.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/music/Following.h>
#include <fd/music/Key.h>
#include <map>
#include <random>
#include <set>

using namespace fd;
using namespace fd::testkit;
using C = fd::devices::Chord;

namespace
{
constexpr double sr = 48000.0;
constexpr int beat = 24000; // samples a beat at 120 bpm

KeyInfo keyOf(int root, std::string_view scale)
{
    return {.root = root, .notes = music::notes(music::Key{.root = root, .scale = std::string(scale)})};
}

TimedEvent on(std::int64_t at, int key, float velocity = 0.8f)
{
    return {at, {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = velocity}};
}
TimedEvent off(std::int64_t at, int key)
{
    return {at, {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}};
}

/// The notes started at a sample, low to high.
std::vector<int> chordAt(const std::vector<TimedEvent>& events, std::int64_t at)
{
    std::vector<int> out;
    for (const auto& e : events)
        if (e.event.type == EventType::NoteOn && e.samplePosition == at)
            out.push_back(e.event.key);
    std::sort(out.begin(), out.end());
    return out;
}

bool balanced(const std::vector<TimedEvent>& events)
{
    std::map<int, int> sounding;
    for (const auto& e : events)
    {
        const int k = e.event.channel * 128 + e.event.key;
        if (e.event.type == EventType::NoteOn)
            ++sounding[k];
        else if (e.event.type == EventType::NoteOff && --sounding[k] < 0)
            return false;
    }
    for (const auto& [k, n] : sounding)
        if (n != 0)
            return false;
    return true;
}

RenderOptions options(KeyInfo key, std::vector<TimedEvent> events)
{
    RenderOptions o;
    o.sampleRate = sr;
    o.transport.key = key;
    o.events = std::move(events);
    return o;
}
} // namespace

TEST_CASE("Chord: Add puts notes over each note, at Level", "[fast]")
{
    C c; // +4, +7
    c.params().set(C::Level, 50);
    const auto out = renderNotes(c, beat, options(keyOf(0, "major"), {on(0, 62), off(1000, 62)}));
    CHECK(chordAt(out, 0) == std::vector<int>{62, 66, 69});
    for (const auto& e : out)
        if (e.event.type == EventType::NoteOn)
            CHECK(e.event.value == (e.event.key == 62 ? 0.8f : 0.4f));
    CHECK(balanced(out));
}

TEST_CASE("Chord: Add in key counts steps, so each third fits where it lands", "[fast]")
{
    C c;
    c.params().set(C::InKey, 1);
    c.params().set(C::Shift1, 2);
    c.params().set(C::Shift1 + 1, 4);
    const auto out = renderNotes(
        c, beat, options(keyOf(0, "major"), {on(0, 62), off(1000, 62), on(2000, 60), off(3000, 60)}));
    CHECK(chordAt(out, 0) == std::vector<int>{62, 65, 69});    // D F A: minor
    CHECK(chordAt(out, 2000) == std::vector<int>{60, 64, 67}); // C E G: major
}

TEST_CASE("Chord: Song plays the song's chord around the note, voice-led", "[fast]")
{
    SongView song;
    song.chords = {{.start = 0.0, .end = 4.0, .root = 9, .notes = music::rotated(0b000010001001, 9)},  // Am
                   {.start = 4.0, .end = 8.0, .root = 5, .notes = music::rotated(0b000010010001, 5)}}; // F
    C c;
    c.params().set(C::Mode, C::Song);
    auto o = options(keyOf(9, "minor"), {on(0, 64), off(beat, 64), on(4 * beat, 64), off(5 * beat, 64)});
    o.song = &song;
    const auto out = renderNotes(c, 6 * beat, o);
    const auto first = chordAt(out, 0), second = chordAt(out, 4 * beat);
    CHECK(std::set<int>(first.begin(), first.end()).contains(64)); // the note played sounds
    for (const int k : first)
        CHECK(music::inSet(song.chords[0].notes, k));
    for (const int k : second)
        CHECK((music::inSet(song.chords[1].notes, k) || k == 64));
    CHECK(music::movement(first, second) <= 4); // led smoothly: a common tone, steps
    CHECK(balanced(out));
}

TEST_CASE("Chord: Song's Kind adds the key's 7th to a plain triad", "[fast]")
{
    SongView song;
    song.chords = {{.start = 0.0, .end = 8.0, .root = 2, .notes = music::rotated(0b000010001001, 2)}}; // Dm
    C c;
    c.params().set(C::Mode, C::Song);
    c.params().set(C::Kind, 1);
    auto o = options(keyOf(0, "major"), {on(0, 62), off(1000, 62)});
    o.song = &song;
    const auto chord = chordAt(renderNotes(c, beat, o), 0);
    std::set<int> classes;
    for (const int k : chord)
        classes.insert(k % 12);
    CHECK(classes == std::set<int>{2, 5, 9, 0}); // D F A C
}

TEST_CASE("Chord: Keys plays the key's chord on each note's step, around Around", "[fast]")
{
    C c;
    c.params().set(C::Mode, C::Keys);
    const auto out = renderNotes(
        c, beat, options(keyOf(0, "major"), {on(0, 38), off(1000, 38), on(2000, 61), off(3000, 61)}));
    const auto d = chordAt(out, 0);
    REQUIRE(d.size() == 3);
    std::set<int> classes;
    for (const int k : d)
    {
        classes.insert(k % 12);
        CHECK(std::abs(k - 60) <= 12); // around C3, wherever the key was played
    }
    CHECK(classes == std::set<int>{2, 5, 9});          // D minor
    CHECK(chordAt(out, 2000) == std::vector<int>{61}); // outside the key: as it is
    CHECK(balanced(out));
}

TEST_CASE("Chord: a strum spreads the notes over its time, the same for any block size", "[fast]")
{
    const auto run = [](std::vector<int> pattern)
    {
        C c;
        c.params().set(C::Shift1 + 2, 12);
        c.params().set(C::Strum, 100); // 4800 samples
        auto o = options(keyOf(0, "major"), {on(0, 60), off(beat, 60)});
        o.blockPattern = std::move(pattern);
        return renderNotes(c, 2 * beat, o);
    };
    const auto out = run({512});
    std::vector<std::pair<std::int64_t, int>> starts;
    for (const auto& e : out)
        if (e.event.type == EventType::NoteOn)
            starts.emplace_back(e.samplePosition, e.event.key);
    REQUIRE(starts.size() == 4);
    CHECK(starts[0] == std::pair<std::int64_t, int>{0, 60});
    CHECK(starts[1] == std::pair<std::int64_t, int>{1600, 64});
    CHECK(starts[2] == std::pair<std::int64_t, int>{3200, 67});
    CHECK(starts[3] == std::pair<std::int64_t, int>{4800, 72});
    for (const auto& pattern : std::vector<std::vector<int>>{{1}, {64}, {4096}, {3, 100, 31}})
    {
        const auto other = run(pattern);
        REQUIRE(other.size() == out.size());
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            CHECK(other[i].samplePosition == out[i].samplePosition);
            CHECK(other[i].event.key == out[i].event.key);
        }
    }
    CHECK(balanced(out));
}

TEST_CASE("Chord: a note let go before its strum ends cuts the notes still to come", "[fast]")
{
    C c;
    c.params().set(C::Strum, 400); // 19200 samples
    c.params().set(C::Direction, 1);
    const auto out = renderNotes(c, beat, options(keyOf(0, "major"), {on(0, 60), off(1000, 60)}));
    int ons = 0;
    for (const auto& e : out)
        ons += e.event.type == EventType::NoteOn ? 1 : 0;
    CHECK(ons == 1); // down: the top note first, the rest never start
    CHECK(balanced(out));
}

TEST_CASE("Chord: a chord held across the song's chord change moves at the change", "[fast]")
{
    SongView song;
    song.chords = {{.start = 0.0, .end = 4.0, .root = 0, .notes = music::rotated(0b000010010001, 0)},  // C
                   {.start = 4.0, .end = 8.0, .root = 5, .notes = music::rotated(0b000010010001, 5)}}; // F
    C c;
    c.params().set(C::Mode, C::Song);
    auto o = options(keyOf(0, "major"), {on(0, 60), off(6 * beat, 60)});
    o.song = &song;
    o.blockPattern = {300};
    const auto out = renderNotes(c, 7 * beat, o);
    const auto moved = chordAt(out, 4 * beat);
    REQUIRE(!moved.empty());
    for (const int k : moved)
        CHECK((music::inSet(song.chords[1].notes, k) || k == 60));
    CHECK(balanced(out));
}

TEST_CASE("Chord: under random notes and settings, every note started ends once", "[fast]")
{
    SongView song;
    for (int i = 0; i < 8; ++i)
        song.chords.push_back(
            {.start = i * 2.0,
             .end = i * 2.0 + 2.0,
             .root = (i * 5) % 12,
             .notes = music::rotated(i % 2 != 0 ? 0b000010001001 : 0b000010010001, (i * 5) % 12)});
    std::mt19937 rng(11);
    for (int round = 0; round < 20; ++round)
    {
        C c;
        for (std::size_t i = 0; i < c.params().size(); ++i)
            c.params().setNormalized(i, std::uniform_real_distribution<float>(0, 1)(rng));
        std::vector<TimedEvent> events;
        std::vector<int> down;
        for (int t = 0; t < 300; ++t)
        {
            const int key = std::uniform_int_distribution<int>(36, 84)(rng);
            const std::int64_t at = t * 600;
            if (std::find(down.begin(), down.end(), key) == down.end() && rng() % 2 == 0)
            {
                events.push_back(on(at, key));
                down.push_back(key);
            }
            else if (!down.empty())
            {
                events.push_back(off(at, down.front()));
                down.erase(down.begin());
            }
        }
        for (const int k : down)
            events.push_back(off(300 * 600, k));
        auto o = options(keyOf(round % 12, "major"), events);
        o.song = &song;
        o.blockPattern = {7, 128, 31};
        INFO("round " << round);
        CHECK(balanced(renderNotes(c, 300 * 600 + 24000, o)));
    }
}

TEST_CASE("Chord: Learn reads the notes held", "[fast]")
{
    C c;
    c.prepare({.sampleRate = sr, .maxBlockSize = 512});
    auto o = options(keyOf(0, "major"), {on(0, 60), on(0, 63), on(0, 67)});
    o.skipPrepare = true;
    renderNotes(c, 512, o);
    std::array<int, 8> held{};
    const auto n = c.heldNotes(held);
    CHECK(std::vector<int>(held.begin(), held.begin() + static_cast<std::ptrdiff_t>(n)) ==
          std::vector<int>{60, 63, 67});
}
