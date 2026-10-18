#include "../../pitch/dsp/Pitch.h"
#include "NoteRun.h"
#include "fd/testkit/DeviceRender.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fd/music/Following.h>

using namespace fd;
using P = fd::devices::Pitch;

TEST_CASE("note run: a chain run off the audio thread plays what it played, to the sample", "[fast]")
{
    // A riff through Pitch with chance: the same roll, the same notes as a render.
    std::vector<SongNote> riff;
    for (int i = 0; i < 32; ++i)
        riff.push_back({.start = 1.0 + i * 0.25,
                        .length = 0.2,
                        .key = static_cast<std::uint8_t>(60 + (i * 5) % 12),
                        .velocity = 0.8f});
    const auto settle = [](P& p)
    {
        p.params().set(P::Chance, 60.0f);
        p.params().set(P::Spread, 3.0f);
        p.params().set(P::Fit, 1.0f);
    };
    P live;
    settle(live);
    testkit::RenderOptions o;
    for (const auto& n : riff)
    {
        o.events.push_back(
            {std::llround(n.start * 24000), {.type = EventType::NoteOn, .key = n.key, .value = n.velocity}});
        o.events.push_back(
            {std::llround((n.start + n.length) * 24000), {.type = EventType::NoteOff, .key = n.key}});
    }
    std::stable_sort(o.events.begin(), o.events.end(),
                     [](const auto& a, const auto& b) { return a.samplePosition < b.samplePosition; });
    o.blockPattern = {480};
    const auto rendered = testkit::renderNotes(live, 10 * 24000, o);
    const auto roll = live.rollHeard();

    P fresh;
    settle(fresh);
    Device* chain[] = {&fresh};
    const std::uint64_t rolls[] = {roll};
    devices::NoteRun run{.from = 0.0, .to = 10.0, .rolls = rolls};
    const auto kept = devices::runNotes(chain, riff, run);
    std::vector<std::pair<long, int>> a, b;
    for (const auto& e : rendered)
        if (e.event.type == EventType::NoteOn)
            a.emplace_back(e.samplePosition, e.event.key);
    for (const auto& n : kept)
        b.emplace_back(std::lround(n.start * 24000), n.key);
    CHECK(a == b);
    CHECK(kept.size() == 32);
    // Another roll, other notes.
    const std::uint64_t other[] = {roll + 1};
    P third;
    settle(third);
    Device* chain3[] = {&third};
    devices::NoteRun again{.from = 0.0, .to = 10.0, .rolls = other};
    std::vector<int> keys3, keys;
    for (const auto& n : devices::runNotes(chain3, riff, again))
        keys3.push_back(n.key);
    for (const auto& n : kept)
        keys.push_back(n.key);
    CHECK(keys3 != keys);
}

TEST_CASE("note run: a note still sounding at the end is cut there; two modules in a row", "[fast]")
{
    P up, fit;
    up.params().set(P::Semitones, 12.0f);
    fit.params().set(P::Fit, 1.0f); // C major
    Device* chain[] = {&up, &fit};
    const std::vector<SongNote> notes{{.start = 0.0, .length = 1.0, .key = 61, .velocity = 0.5f},
                                      {.start = 3.0, .length = 4.0, .key = 64, .velocity = 0.5f}};
    devices::NoteRun run{.from = 0.0, .to = 4.0};
    const auto out = devices::runNotes(chain, notes, run);
    REQUIRE(out.size() == 2);
    CHECK(out[0].key == 72); // C♯ up an octave, fitted: C (a tie goes down)
    CHECK(out[0].length == 1.0);
    CHECK(out[1].key == 76);
    CHECK(out[1].start == 3.0);
    CHECK(out[1].length == 1.0); // cut at the end
}
