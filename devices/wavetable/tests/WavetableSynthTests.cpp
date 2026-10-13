// The Wavetable Synth (devices/wavetable/SPEC.md).
#include "FactoryTables.h"
#include "WavetableSynth.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using W = fd::devices::WavetableSynth;

namespace
{
constexpr double sr = 48000.0;

/// A held note: `key` for `seconds`, released, then `tail` seconds.
RenderOptions held(int key, double seconds, double tail = 0.3, float velocity = 0.8f)
{
    RenderOptions o;
    o.sampleRate = sr;
    o.events = {
        {.samplePosition = 0,
         .event = {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = velocity}},
        {.samplePosition = static_cast<std::int64_t>(seconds * sr),
         .event = {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}}};
    o.tailSamples = static_cast<int>(tail * sr);
    return o;
}

AudioBuffer silence(double seconds)
{
    return AudioBuffer(0, static_cast<int>(seconds * sr));
}
} // namespace

TEST_CASE("wavetable synth: its parameters keep the SPEC's IDs, and its tables are there", "[fast]")
{
    auto device = std::make_unique<W>();
    auto& w = *device;
    CHECK(W::NumParams == 379);
    CHECK(w.params().spec(W::osc(0, W::OscPos)).id == "osc1.pos");
    CHECK(w.params().spec(W::osc(2, W::OscRoute)).id == "osc3.route");
    CHECK(w.params().spec(W::filter(1, W::FilterEnv)).id == "filter2.env");
    CHECK(w.params().spec(W::env(3, W::EnvMode)).id == "env4.mode");
    CHECK(w.params().spec(W::lfo(7, W::LfoPoly)).id == "lfo8.poly");
    CHECK(w.params().spec(W::route(31, W::RouteScale)).id == "mod32.aux");
    CHECK(w.params().spec(W::Quality).id == "quality");
    CHECK(w.params().spec(W::Quality).hasFlag(ParamFlags::ChangesLatency));
    CHECK(fd::devices::wavetable::factoryTables().size() >= 60);
    for (const auto& t : fd::devices::wavetable::factoryTables())
    {
        INFO(t.id);
        const auto table = fd::devices::wavetable::factoryTable(t.id);
        REQUIRE(table != nullptr);
        CHECK(table->frames() >= 1);
        CHECK(table->highestHarmonic() >= 1);
    }
}

TEST_CASE("wavetable synth: a note plays at its pitch, and stops after its release", "[fast]")
{
    auto w = std::make_unique<W>();
    const auto out = render(*w, silence(1.0), held(69, 0.6));
    CHECK(rmsDb(out.slice(4800, 9600)) > -30.0);
    CHECK(peakDb(out.slice(static_cast<int>(0.9 * sr), static_cast<int>(0.05 * sr))) < -90.0);
    // A4: 440 Hz (zero crossings over half a second of the sustain).
    int crossings = 0;
    const int from = static_cast<int>(0.1 * sr), to = static_cast<int>(0.5 * sr);
    for (int i = from + 1; i < to; ++i)
        if (out.at(0, i - 1) < 0.0f && out.at(0, i) >= 0.0f)
            ++crossings;
    CHECK(crossings / ((to - from) / sr) == Approx(440.0).margin(3.0));
}
