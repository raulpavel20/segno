#include "Pitch.h"
#include "fd/testkit/DeviceRender.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/music/Following.h>
#include <fd/music/Key.h>
#include <map>
#include <random>

using namespace fd;
using namespace fd::testkit;
using P = fd::devices::Pitch;

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

/// The notes started, in order.
std::vector<int> started(const std::vector<TimedEvent>& events)
{
    std::vector<int> out;
    for (const auto& e : events)
        if (e.event.type == EventType::NoteOn)
            out.push_back(e.event.key);
    return out;
}

/// Every note started is ended once, and nothing is ended that wasn't started.
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

RenderOptions withKey(KeyInfo key, std::vector<TimedEvent> events)
{
    RenderOptions o;
    o.sampleRate = sr;
    o.transport.key = key;
    o.events = std::move(events);
    return o;
}

/// A melody: each note a beat long, from sample 0.
std::vector<TimedEvent> melody(std::initializer_list<int> keys, int length = beat)
{
    std::vector<TimedEvent> out;
    std::int64_t at = 0;
    for (const int k : keys)
    {
        out.push_back(on(at, k));
        out.push_back(off(at + length - 1, k));
        at += length;
    }
    return out;
}
} // namespace

TEST_CASE("Pitch: steps of the key keep a melody in it", "[fast]")
{
    P p;
    p.params().set(P::Steps, 2);
    const auto out = renderNotes(p, 5 * beat, withKey(keyOf(9, "minor"), melody({57, 59, 60, 62, 64})));
    CHECK(started(out) == std::vector<int>{60, 62, 64, 65, 67}); // A B C D E → C D E F G
    CHECK(balanced(out));
}

TEST_CASE("Pitch: semitones, and mirror around the axis in the key", "[fast]")
{
    P p;
    p.params().set(P::Semitones, -12);
    CHECK(started(renderNotes(p, 2 * beat, withKey(keyOf(0, "major"), melody({60, 64})))) ==
          std::vector<int>{48, 52});
    P m;
    m.params().set(P::Mirror, 1);
    m.params().set(P::Axis, 60);
    CHECK(started(renderNotes(m, 3 * beat, withKey(keyOf(0, "major"), melody({60, 64, 67})))) ==
          std::vector<int>{60, 57, 53}); // C E G → C A F
}

TEST_CASE("Pitch: fit to the key, the way the melody moves at a tie", "[fast]")
{
    P p;
    p.params().set(P::Fit, 1);
    // C♯ between C and D: going up it goes up, going down it goes down; F♯ is nearest G.
    const auto out = renderNotes(p, 4 * beat, withKey(keyOf(0, "major"), melody({59, 61, 63, 61})));
    CHECK(started(out) == std::vector<int>{59, 62, 64, 60});
    CHECK(balanced(out));
}

TEST_CASE("Pitch: fit to the song's chords, passing notes off the beat to the key", "[fast]")
{
    SongView song;
    song.chords = {{.start = 0.0, .end = 4.0, .root = 9, .notes = music::rotated(0b000010001001, 9)},  // Am
                   {.start = 4.0, .end = 8.0, .root = 5, .notes = music::rotated(0b000010010001, 5)}}; // F
    P p;
    p.params().set(P::Fit, 2);
    auto o = withKey(keyOf(9, "minor"), {});
    o.song = &song;
    // D on beat 0 → C or E (nearest of Am's, tie down: C); D on the half beat stays (in the key);
    // B on beat 4 (over F) → A or C: A is nearer? B−A = 2, C−B = 1 → C.
    o.events = {on(0, 62),        off(100, 62),           on(beat / 2, 62), off(beat / 2 + 100, 62),
                on(4 * beat, 71), off(4 * beat + 100, 71)};
    CHECK(started(renderNotes(p, 5 * beat, o)) == std::vector<int>{60, 62, 72});
    p.params().set(P::Passing, 0);
    CHECK(started(renderNotes(p, 5 * beat, o)) == std::vector<int>{60, 60, 72});
}

TEST_CASE("Pitch: a note held across a chord change moves to the new chord at the change", "[fast]")
{
    SongView song;
    song.chords = {{.start = 0.0, .end = 4.0, .root = 0, .notes = music::rotated(0b000010010001, 0)},  // C
                   {.start = 4.0, .end = 8.0, .root = 2, .notes = music::rotated(0b000010001001, 2)}}; // Dm
    P p;
    p.params().set(P::Fit, 2);
    auto o = withKey(keyOf(0, "major"), {on(0, 64), off(6 * beat, 64)});
    o.song = &song;
    o.blockPattern = {333};
    const auto out = renderNotes(p, 7 * beat, o);
    REQUIRE(out.size() == 4);
    CHECK(out[0].event.key == 64);
    CHECK(out[1].event.type == EventType::NoteOff);
    CHECK(out[1].samplePosition == 4 * beat);
    CHECK(out[2].event.type == EventType::NoteOn);
    CHECK(out[2].samplePosition == 4 * beat);
    CHECK(out[2].event.key == 65); // E over Dm → F (nearest, tie down... E−D 2, F−E 1)
    CHECK(out[3].samplePosition == 6 * beat);
    CHECK(balanced(out));
    // Hold: it stays.
    p.params().set(P::Change, 1);
    CHECK(started(renderNotes(p, 7 * beat, o)) == std::vector<int>{64});
}

TEST_CASE("Pitch: random moves are the roll's, the same each time it's asked", "[fast]")
{
    std::vector<int> keys(64, 60);
    std::vector<TimedEvent> events;
    for (int i = 0; i < 64; ++i)
    {
        events.push_back(on(i * beat / 4, 60));
        events.push_back(off(i * beat / 4 + 100, 60));
    }
    P p;
    p.params().set(P::Chance, 50);
    p.params().set(P::Spread, 2);
    const auto a = started(renderNotes(p, 17 * beat, withKey(keyOf(0, "major"), events)));
    const auto b = started(renderNotes(p, 17 * beat, withKey(keyOf(0, "major"), events)));
    CHECK(a == b);
    int moved = 0;
    for (const int k : a)
    {
        moved += k != 60 ? 1 : 0;
        CHECK(music::inSet(keyOf(0, "major").notes, k)); // in the key, at most 2 steps away
        CHECK(std::abs(k - 60) <= 4);
    }
    CHECK(moved > 16);
    CHECK(moved < 48);
    // A new pass rolls again.
    P q;
    q.params().set(P::Chance, 50);
    std::vector<TimedEvent> second;
    auto o = withKey(keyOf(0, "major"), events);
    q.prepare({.sampleRate = sr, .maxBlockSize = 512});
    o.skipPrepare = true;
    const auto first = started(renderNotes(q, 17 * beat, o));
    o.transportAt = [](TransportInfo& t, std::int64_t at) { t.playing = at > 0; };
    renderNotes(q, 1024, o); // stops after its first block
    o.transportAt = {};
    const auto again = started(renderNotes(q, 17 * beat, o));
    CHECK(first != again);
}

TEST_CASE("Pitch: the range folds, drops or limits", "[fast]")
{
    P p;
    p.params().set(P::Lowest, 48);
    p.params().set(P::Highest, 59);
    const auto run = [&]
    { return started(renderNotes(p, 3 * beat, withKey(keyOf(0, "major"), melody({36, 64, 50})))); };
    CHECK(run() == std::vector<int>{48, 52, 50});
    p.params().set(P::Outside, 1);
    CHECK(run() == std::vector<int>{50});
    p.params().set(P::Outside, 2);
    CHECK(run() == std::vector<int>{48, 59, 50});
}

TEST_CASE("Pitch: controllers pass, pressure follows its note, all notes off ends them", "[fast]")
{
    P p;
    p.params().set(P::Semitones, 7);
    auto o = withKey(keyOf(0, "major"), {on(0, 60),
                                         {10, {.type = EventType::Controller, .key = 1, .value = 0.5f}},
                                         {20, {.type = EventType::PolyPressure, .key = 60, .value = 0.3f}},
                                         {30, {.type = EventType::AllNotesOff}}});
    const auto out = renderNotes(p, 1000, o);
    REQUIRE(out.size() == 5);
    CHECK(out[1].event.type == EventType::Controller);
    CHECK(out[2].event.type == EventType::PolyPressure);
    CHECK(out[2].event.key == 67);
    CHECK(out[3].event.type == EventType::NoteOff);
    CHECK(out[3].event.key == 67);
    CHECK(out[4].event.type == EventType::AllNotesOff);
}

TEST_CASE("Pitch: under random notes and settings, every note started ends once", "[fast]")
{
    std::mt19937 rng(7);
    for (int round = 0; round < 20; ++round)
    {
        P p;
        for (std::size_t i = 0; i < p.params().size(); ++i)
            p.params().setNormalized(i, std::uniform_real_distribution<float>(0, 1)(rng));
        std::vector<TimedEvent> events;
        std::vector<int> down;
        for (int t = 0; t < 400; ++t)
        {
            const int key = std::uniform_int_distribution<int>(30, 90)(rng);
            const std::int64_t at = t * 300;
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
            events.push_back(off(400 * 300, k));
        auto o = withKey(keyOf(round % 12, "dorian"), events);
        o.blockPattern = {7, 128, 31};
        INFO("round " << round);
        CHECK(balanced(renderNotes(p, 400 * 300 + 10, o)));
    }
}
