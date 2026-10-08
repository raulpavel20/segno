// Device conformance suite (ADR 0007). Runs automatically on every registered device.
// A device that fails here is not done, whatever else it does.

#include "fd/device/Registry.h"
#include "fd/device/State.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace fd;
using namespace fd::testkit;

namespace
{
constexpr double kSampleRate = 48000.0;

bool isEffect(const DeviceInfo& info)
{
    return info.kind == DeviceKind::AudioEffect;
}

/// Stimulus for a device: noise for effects; a short note pattern for instruments.
struct Stimulus
{
    AudioBuffer audio;
    std::vector<TimedEvent> events;
};

Stimulus stimulusFor(const DeviceInfo& info, int numSamples, std::uint32_t seed = 1)
{
    Stimulus s;
    s.audio = isEffect(info) ? whiteNoise(numSamples, seed, 0.25f, std::max(1, info.buses.inputChannels))
                             : silence(numSamples, std::max(1, info.buses.inputChannels));
    if (info.buses.acceptsMidi)
    {
        const int step = numSamples / 8;
        for (int k = 0; k < 4; ++k)
        {
            const auto note = static_cast<std::uint8_t>(48 + 7 * k);
            s.events.push_back({k * step * 2, {.type = EventType::NoteOn, .key = note, .value = 0.8f}});
            s.events.push_back({k * step * 2 + step, {.type = EventType::NoteOff, .key = note}});
        }
    }
    return s;
}

RenderOptions optionsFor(const Stimulus& s, std::vector<int> pattern = {512})
{
    RenderOptions o;
    o.sampleRate = kSampleRate;
    o.blockPattern = std::move(pattern);
    o.events = s.events;
    return o;
}

/// Deterministic pseudo-random setting of every parameter.
void randomize(Device& d, std::uint32_t seed)
{
    Rng rng(seed);
    for (size_t i = 0; i < d.params().size(); ++i)
        d.params().setNormalized(i, rng.next01());
}

template <typename Fn>
void forEachDevice(Fn&& fn)
{
    REQUIRE_FALSE(allDevices().empty());
    for (const auto* factory : allDevices())
    {
        DYNAMIC_SECTION(factory->info().id)
        {
            fn(*factory);
        }
    }
}
} // namespace

TEST_CASE("conformance: output is identical for any block size", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        const auto s = stimulusFor(f.info(), 24000);
        for (std::uint32_t setting : {0u, 11u, 12u}) // 0 = defaults
        {
            auto make = [&] {
                auto d = f.create();
                if (setting != 0)
                    randomize(*d, setting);
                return d;
            };
            auto ref = make();
            const auto reference = render(*ref, s.audio, optionsFor(s));
            for (const auto& pattern : std::vector<std::vector<int>>{{1}, {7}, {64}, {480}, {4096}, {3, 100, 31, 33, 1000}})
            {
                auto d = make();
                INFO("setting " << setting << ", first block size " << pattern.front());
                CHECK(bitIdentical(render(*d, s.audio, optionsFor(s, pattern)), reference));
            }
            auto inPlace = make();
            auto opts = optionsFor(s);
            opts.inPlace = true;
            INFO("in-place processing, setting " << setting);
            CHECK(bitIdentical(render(*inPlace, s.audio, opts), reference));
        }
    });
}

TEST_CASE("conformance: works at common sample rates", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        {
            auto d = f.create();
            const auto s = stimulusFor(f.info(), static_cast<int>(sr / 2));
            auto opts = optionsFor(s);
            opts.sampleRate = sr;
            const auto out = render(*d, s.audio, opts);
            INFO("sample rate " << sr);
            CHECK(allFinite(out));
            CHECK(countSubnormals(out) == 0);
        }
    });
}

TEST_CASE("conformance: silence in gives silence out", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        if (!isEffect(f.info()))
            return;
        auto d = f.create();
        const auto out = render(*d, silence(48000, f.info().buses.inputChannels), {.sampleRate = kSampleRate});
        CHECK(peakDb(out.slice(36000, 12000)) < -120.0);
        CHECK(countSubnormals(out) == 0);
    });
}

TEST_CASE("conformance: survives parameter fuzzing", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        auto d = f.create();
        auto s = stimulusFor(f.info(), 96000, 5);
        for (int c = 0; c < s.audio.numChannels(); ++c) // add a full-scale DC section
            for (int i = 48000; i < 60000; ++i)
                s.audio.at(c, i) = 1.0f;
        Rng rng(99);
        auto opts = optionsFor(s, {64, 1024, 333, 17});
        opts.beforeBlock = [&](Device& dev, std::int64_t) { randomize(dev, rng.nextU32()); };
        const auto out = render(*d, s.audio, opts);
        CHECK(allFinite(out));
        // Generous bound: random settings can legitimately boost a lot (EQ: many +30 dB bands);
        // this catches instability, not loudness.
        CHECK(peak(out) < 1.0e6);
    });
}

TEST_CASE("conformance: parameter jumps do not click", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        if (!isEffect(f.info()))
            return; // instruments: covered by voice tests from M1
        const auto input = stereoSines(1000.0, 1500.0, kSampleRate, 48000, 0.5f);
        const auto& specs = f.info().params;
        for (size_t p = 0; p < specs.size(); ++p)
        {
            // Non-automatable parameters (e.g. oversampling quality) are not meant to change
            // during playback (ADR 0016).
            if (!specs[p].hasFlag(ParamFlags::Automatable))
                continue;
            for (bool upwards : {true, false})
            {
                auto d = f.create();
                d->params().set(p, upwards ? specs[p].min : specs[p].max);
                RenderOptions opts{.sampleRate = kSampleRate};
                opts.beforeBlock = [&](Device& dev, std::int64_t start) {
                    if (start >= 24000)
                        dev.params().set(p, upwards ? specs[p].max : specs[p].min);
                };
                // Score the region around the change only (the stimulus itself starts abruptly).
                const auto out = render(*d, input, opts).slice(12000, 36000);
                for (int c = 0; c < out.numChannels(); ++c)
                {
                    INFO("parameter '" << specs[p].id << "' " << (upwards ? "min->max" : "max->min") << ", channel " << c);
                    CHECK(clickScore(out, c) < 8.0);
                }
            }
        }
    });
}

TEST_CASE("conformance: state round trip reproduces the sound", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        const auto s = stimulusFor(f.info(), 24000);
        auto a = f.create();
        randomize(*a, 1234);
        const std::string saved = serializeState(*a);

        auto b = f.create();
        REQUIRE(deserializeState(*b, saved) == ApplyResult::Ok);
        CHECK(serializeState(*b) == saved);
        CHECK(bitIdentical(render(*a, s.audio, optionsFor(s)), render(*b, s.audio, optionsFor(s))));
    });
}

TEST_CASE("conformance: reset is deterministic", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        const auto s = stimulusFor(f.info(), 24000);
        auto d = f.create();
        randomize(*d, 77);
        const auto first = render(*d, s.audio, optionsFor(s));
        d->reset();
        auto opts = optionsFor(s);
        opts.skipPrepare = true;
        CHECK(bitIdentical(render(*d, s.audio, opts), first));
    });
}

TEST_CASE("conformance: reported latency matches measured latency", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        if (!isEffect(f.info()))
            return;
        // Measured at defaults (filters at random settings add group delay, which is not latency).
        auto d = f.create();
        const auto in = whiteNoise(16384, 3, 0.25f, f.info().buses.inputChannels);
        const auto out = render(*d, in, {.sampleRate = kSampleRate});
        CHECK(estimateLatency(in, out, 2048) == d->latencySamples());

        // ADR 0016: reported latency never depends on parameters.
        for (std::uint32_t setting : {21u, 22u, 23u})
        {
            auto r = f.create();
            randomize(*r, setting);
            r->prepare({kSampleRate, 512});
            INFO("setting " << setting);
            CHECK(r->latencySamples() == d->latencySamples());
        }
    });
}

// --- Instruments (M1 step A3) ------------------------------------------------------------

namespace
{
bool isInstrument(const DeviceInfo& info)
{
    return info.kind == DeviceKind::Instrument && info.buses.acceptsMidi;
}

/// A busy second of playing: overlapping notes beyond typical polyphony, repeated keys, a
/// sustain pedal held over note-offs, pitch bend and mod wheel. Every note is released and
/// the pedal lifted by `lengthSamples`.
std::vector<TimedEvent> busyPlaying(int lengthSamples, std::uint32_t seed)
{
    Rng rng(seed);
    std::vector<TimedEvent> events;
    const int noteSpan = lengthSamples / 2;
    for (int n = 0; n < 40; ++n)
    {
        const auto key = static_cast<std::uint8_t>(36 + static_cast<int>(rng.next01() * 48.0) % 48);
        const auto on = static_cast<std::int64_t>(rng.next01() * noteSpan);
        const auto off = on + 1 + static_cast<std::int64_t>(rng.next01() * noteSpan * 0.5);
        events.push_back({on, {.type = EventType::NoteOn, .key = key, .value = 0.2f + 0.8f * static_cast<float>(rng.next01())}});
        events.push_back({off, {.type = EventType::NoteOff, .key = key}});
    }
    events.push_back({noteSpan / 3, {.type = EventType::Controller, .key = 64, .value = 1.0f}}); // pedal down
    events.push_back({noteSpan / 2, {.type = EventType::PitchBend, .value = 0.7f}});
    events.push_back({noteSpan / 2 + 100, {.type = EventType::Controller, .key = 1, .value = 0.6f}});
    events.push_back({lengthSamples - 1, {.type = EventType::Controller, .key = 64, .value = 0.0f}}); // pedal up
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedEvent& a, const TimedEvent& b) { return a.samplePosition < b.samplePosition; });
    return events;
}

/// Renders `events` over `length` samples plus the device's declared tail and 100 ms.
AudioBuffer renderWithTail(Device& d, int length, std::vector<TimedEvent> events)
{
    RenderOptions o;
    o.sampleRate = kSampleRate;
    o.events = std::move(events);
    o.tailSamples = static_cast<int>(std::ceil((d.tailSeconds() + 0.1) * kSampleRate));
    return render(d, silence(length, std::max(1, d.info().buses.inputChannels)), o);
}
} // namespace

TEST_CASE("conformance: instrument notes always release to silence", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        if (!isInstrument(f.info()))
            return;
        for (std::uint32_t setting : {0u, 21u, 22u, 23u, 24u})
        {
            auto d = f.create();
            if (setting != 0)
                randomize(*d, setting);
            const auto out = renderWithTail(*d, 48000, busyPlaying(48000, 100 + setting));
            INFO("setting " << setting << ", tail " << d->tailSeconds() << " s");
            CHECK(allFinite(out));
            if (setting == 0)
                CHECK(peak(out.slice(0, 48000)) > 1.0e-3); // it played (random settings may be quiet)
            CHECK(peakDb(out.slice(out.numSamples() - 2400, 2400)) < -120.0); // last 50 ms
        }
    });
}

TEST_CASE("conformance: all-notes-off silences an instrument", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        if (!isInstrument(f.info()))
            return;
        auto d = f.create();
        // Held notes and a held pedal: nothing is released except by all-notes-off.
        std::vector<TimedEvent> events{{0, {.type = EventType::Controller, .key = 64, .value = 1.0f}}};
        for (std::uint8_t k = 0; k < 6; ++k)
            events.push_back({100 + k * 50, {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(48 + 5 * k), .value = 0.8f}});
        events.push_back({24000, {.type = EventType::AllNotesOff}});
        const auto out = renderWithTail(*d, 48000, events);
        CHECK(peak(out.slice(12000, 12000)) > 1.0e-3);
        CHECK(peakDb(out.slice(out.numSamples() - 2400, 2400)) < -120.0);
    });
}

TEST_CASE("conformance: process() never allocates", "[fast][conformance]")
{
    if (!allocationTrackingEnabled())
        SKIP("allocation tracking is disabled in sanitizer builds (RTSan covers it)");
    forEachDevice([](const DeviceFactory& f) {
        auto d = f.create();
        const auto s = stimulusFor(f.info(), 48000);
        auto opts = optionsFor(s, {1, 64, 4096});
        Rng rng(5);
        opts.beforeBlock = [&](Device& dev, std::int64_t) { randomize(dev, rng.nextU32()); };
        const auto before = allocationsDuringProcess();
        render(*d, s.audio, opts);
        CHECK(allocationsDuringProcess() - before == 0);
    });
}
