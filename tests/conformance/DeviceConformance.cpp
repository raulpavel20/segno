// Device conformance suite (ADR 0007). Runs automatically on every registered device.
// A device that fails here is not done, whatever else it does.

#include "fd/device/Registry.h"
#include "fd/device/State.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_test_macros.hpp>

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
