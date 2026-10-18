#include "Arpeggiator.h"
#include "fd/testkit/DeviceRender.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fd/music/Following.h>
#include <fd/music/Key.h>
#include <map>
#include <nlohmann/json.hpp>
#include <random>

using namespace fd;
using namespace fd::testkit;
using A = fd::devices::Arpeggiator;

namespace
{
constexpr double sr = 48000.0;
constexpr int beat = 24000;         // samples a beat at 120 bpm
constexpr int sixteenth = beat / 4; // the default step

TimedEvent on(std::int64_t at, int key, float velocity = 0.8f)
{
    return {at, {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = velocity}};
}
TimedEvent off(std::int64_t at, int key)
{
    return {at, {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}};
}

struct Played
{
    std::int64_t at;
    int key;
    std::int64_t length;
    float velocity;
};

/// Each note played, with its length.
std::vector<Played> notesOf(const std::vector<TimedEvent>& events)
{
    std::vector<Played> out;
    std::map<int, std::vector<std::size_t>> open;
    for (const auto& e : events)
        if (e.event.type == EventType::NoteOn)
        {
            open[e.event.key].push_back(out.size());
            out.push_back({e.samplePosition, e.event.key, -1, e.event.value});
        }
        else if (e.event.type == EventType::NoteOff && !open[e.event.key].empty())
        {
            for (const auto i : open[e.event.key])
                out[i].length = e.samplePosition - out[i].at;
            open[e.event.key].clear();
        }
    return out;
}

std::vector<int> keys(const std::vector<Played>& notes)
{
    std::vector<int> out;
    for (const auto& n : notes)
        out.push_back(n.key);
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

RenderOptions held(std::vector<int> chord, std::int64_t from, std::int64_t to)
{
    RenderOptions o;
    o.sampleRate = sr;
    for (const int k : chord)
        o.events.push_back(on(from, k));
    for (const int k : chord)
        o.events.push_back(off(to, k));
    return o;
}
} // namespace

TEST_CASE("Arpeggiator: up at 1/16 on the song's grid, gated", "[fast]")
{
    A a;
    const auto out = renderNotes(a, 2 * beat, held({64, 60, 67}, 0, 6 * sixteenth));
    const auto notes = notesOf(out);
    CHECK(keys(notes) == std::vector<int>{60, 64, 67, 60, 64, 67});
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        CHECK(notes[i].at == static_cast<std::int64_t>(i) * sixteenth);
        CHECK(notes[i].length == sixteenth * 8 / 10); // Gate 80 %
    }
    CHECK(balanced(out));
}

TEST_CASE("Arpeggiator: styles, and steps of Distance", "[fast]")
{
    A a;
    a.params().set(A::Style, 2); // up-down
    a.params().set(A::Steps, 2);
    const auto out = renderNotes(a, 4 * beat, held({60, 64, 67}, 0, 10 * sixteenth));
    CHECK(keys(notesOf(out)) == std::vector<int>{60, 64, 67, 72, 76, 79, 76, 72, 67, 64});
    a.params().set(A::Style, 1); // down, in key: Distance 2 steps of C major
    a.params().set(A::Distance, 2);
    a.params().set(A::InKey, 1);
    auto o = held({60, 64}, 0, 4 * sixteenth);
    o.transport.key = {.root = 0, .notes = music::notes(music::Key{.root = 0, .scale = "major"})};
    CHECK(keys(notesOf(renderNotes(a, 2 * beat, o))) == std::vector<int>{67, 64, 64, 60});
}

TEST_CASE("Arpeggiator: steps land on the grid however the song is cut into blocks, and after a jump",
          "[fast]")
{
    const auto run = [](std::vector<int> pattern, double startBeat)
    {
        A a;
        a.params().set(A::Style, 8); // random: drawn from the step's place
        a.params().set(A::Retrigger, 0);
        a.params().set(A::Swing, 40);
        a.params().set(A::Rate, 11); // 1/16T
        auto o = held({60, 63, 67, 70}, 0, 8 * beat);
        o.transport.ppqPosition = startBeat;
        o.blockPattern = std::move(pattern);
        return notesOf(renderNotes(a, 8 * beat, o));
    };
    const auto through = run({512}, 0.0);
    for (const auto& pattern : std::vector<std::vector<int>>{{1}, {64}, {4096}, {3, 100, 31}})
    {
        const auto other = run(pattern, 0.0);
        REQUIRE(other.size() == through.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            CHECK(other[i].at == through[i].at);
            CHECK(other[i].key == through[i].key);
        }
    }
    // Started at beat 4: the same steps as played through from there.
    const auto jumped = run({512}, 4.0);
    std::vector<int> later;
    for (const auto& n : through)
        if (n.at >= 4 * beat && n.at < 8 * beat)
            later.push_back(n.key);
    std::vector<int> fromFour;
    for (const auto& n : jumped)
        if (n.at < 4 * beat)
            fromFour.push_back(n.key);
    CHECK(fromFour == later);
}

TEST_CASE("Arpeggiator: lanes of their own lengths drift, and line up again", "[fast]")
{
    A a;
    a.params().set(A::Rate, 14); // 1/64: many steps
    a.params().set(A::VelocityLength, 5);
    for (int i = 0; i < 5; ++i)
        a.setStep(A::VelocityLane, i, 40.0f + 20.0f * static_cast<float>(i));
    a.setStep(A::GateLane, 0, 200.0f); // the 16-step lane's first step longer
    const int step = beat / 16;
    const auto notes = notesOf(renderNotes(a, 90 * step, held({60}, 0, 90 * step)));
    REQUIRE(notes.size() >= 81);
    for (std::size_t i = 0; i < 81; ++i)
    {
        INFO("step " << i);
        CHECK(notes[i].velocity == Catch::Approx(0.8 * (40.0 + 20.0 * static_cast<double>(i % 5)) / 100.0));
        CHECK((notes[i].length > step) == (i % 16 == 0));
    }
}

TEST_CASE("Arpeggiator: ratchets split a step, chance thins it by its roll", "[fast]")
{
    A a;
    a.setStep(A::RatchetLane, 0, 2.0f);
    a.params().set(A::RatchetLength, 1);
    const auto notes = notesOf(renderNotes(a, beat, held({60}, 0, 2 * sixteenth)));
    REQUIRE(notes.size() == 4);
    CHECK(notes[1].at == sixteenth / 2);
    CHECK(notes[1].length == sixteenth / 2 * 8 / 10);

    A b;
    b.setStep(A::ChanceLane, 0, 50.0f);
    b.params().set(A::ChanceLength, 1);
    const auto thinned = notesOf(renderNotes(b, 16 * beat, held({60}, 0, 16 * beat)));
    CHECK(thinned.size() > 16);
    CHECK(thinned.size() < 48);
}

TEST_CASE("Arpeggiator: Hold keeps the notes let go until new ones come", "[fast]")
{
    A a;
    a.params().set(A::Hold, 1);
    auto o = held({60, 64}, 0, 100);
    o.events.push_back(on(4 * sixteenth, 67));
    o.events.push_back(off(4 * sixteenth + 200, 67));
    const auto notes = notesOf(renderNotes(a, 2 * beat, o));
    CHECK(keys(notes) == std::vector<int>{60, 64, 60, 64, 67, 67, 67, 67});
}

TEST_CASE("Arpeggiator: with no notes held, the song's chords from From", "[fast]")
{
    SongView song;
    song.chords = {{.start = 0.0, .end = 1.0, .root = 9, .notes = music::rotated(0b000010001001, 9)},  // Am
                   {.start = 1.0, .end = 2.0, .root = 5, .notes = music::rotated(0b000010010001, 5)}}; // F
    A a;
    a.params().set(A::SongChords, 1);
    RenderOptions o;
    o.sampleRate = sr;
    o.song = &song;
    const auto out = renderNotes(a, 2 * beat, o);
    // The pattern runs on through the change (Retrigger on a note: no note came).
    CHECK(keys(notesOf(out)) == std::vector<int>{60, 64, 69, 60, 65, 69, 60, 65});
    CHECK(balanced(out));
}

TEST_CASE("Arpeggiator: Retrigger on a note starts the pattern again", "[fast]")
{
    A a;
    auto o = held({60, 64, 67}, 0, 8 * sixteenth);
    o.events.push_back(on(5 * sixteenth, 72));
    std::sort(o.events.begin(), o.events.end(),
              [](const auto& x, const auto& y) { return x.samplePosition < y.samplePosition; });
    o.events.push_back(off(8 * sixteenth, 72));
    CHECK(keys(notesOf(renderNotes(a, 3 * beat, o))) == std::vector<int>{60, 64, 67, 60, 64, 60, 64, 67});
}

TEST_CASE("Arpeggiator: the lanes are kept in its state", "[fast]")
{
    A a;
    a.setStep(A::OctaveLane, 3, 2.0f);
    a.setStep(A::VelocityLane, 15, 150.0f);
    nlohmann::json extra = nlohmann::json::object();
    a.writeExtraState(extra);
    A b;
    b.readExtraState(extra);
    CHECK(b.step(A::OctaveLane, 3) == 2.0f);
    CHECK(b.step(A::VelocityLane, 15) == 150.0f);
    CHECK(b.step(A::GateLane, 0) == 100.0f);
    b.readExtraState({{"lane.octave", {9, -9}}});
    CHECK(b.step(A::OctaveLane, 0) == 2.0f);
    CHECK(b.step(A::OctaveLane, 1) == -2.0f);
    CHECK(b.step(A::OctaveLane, 3) == 0.0f);
}

TEST_CASE("Arpeggiator: under random notes and settings, every note started ends once", "[fast]")
{
    std::mt19937 rng(3);
    for (int round = 0; round < 20; ++round)
    {
        A a;
        for (std::size_t i = 0; i < a.params().size(); ++i)
            a.params().setNormalized(i, std::uniform_real_distribution<float>(0, 1)(rng));
        for (int l = 0; l < A::LaneCount; ++l)
            for (int s = 0; s < A::maxSteps; ++s)
                a.setStep(l, s,
                          std::uniform_real_distribution<float>(l == A::OctaveLane    ? -2.0f
                                                                : l == A::RatchetLane ? 1.0f
                                                                                      : 0.0f,
                                                                l == A::OctaveLane    ? 2.0f
                                                                : l == A::RatchetLane ? 4.0f
                                                                                      : 100.0f)(rng));
        std::vector<TimedEvent> events;
        std::vector<int> down;
        for (int t = 0; t < 200; ++t)
        {
            const int key = std::uniform_int_distribution<int>(40, 80)(rng);
            const std::int64_t at = t * 1100;
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
        events.push_back({200 * 1100, {.type = EventType::AllNotesOff}});
        RenderOptions o;
        o.sampleRate = sr;
        o.events = events;
        o.blockPattern = {7, 128, 31};
        INFO("round " << round);
        const auto out = renderNotes(a, 200 * 1100 + 10, o);
        std::vector<TimedEvent> notes;
        for (const auto& e : out)
            if (e.event.type == EventType::NoteOn || e.event.type == EventType::NoteOff)
                notes.push_back(e);
        CHECK(balanced(notes));
    }
}
