// Device conformance suite (ADR 0007). Runs automatically on every registered device.
// A device that fails here is not done, whatever else it does.

#include "Sampler.h"
#include "fd/device/Library.h"
#include "fd/device/PresetCheck.h"
#include "fd/device/Presets.h"
#include "fd/device/Registry.h"
#include "fd/device/State.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
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

/// A fresh instance, made audible where the device needs an asset (e.g. a sample; ADR 0017).
std::unique_ptr<Device> create(const DeviceFactory& f)
{
    auto d = f.create();
    if (f.testSetup != nullptr)
        f.testSetup(*d);
    return d;
}

/// Deterministic pseudo-random setting of every parameter, except those that change the
/// device's latency: they're configuration, not something that moves while audio plays (ADR 0020).
void randomize(Device& d, std::uint32_t seed)
{
    Rng rng(seed);
    for (size_t i = 0; i < d.params().size(); ++i)
    {
        const double value = rng.next01();
        // Not latency (fixed per instance), nor how you listen (never kept: Transient).
        if (!d.params().spec(i).hasFlag(ParamFlags::ChangesLatency) &&
            !d.params().spec(i).hasFlag(ParamFlags::Transient))
            d.params().setNormalized(i, static_cast<float>(value));
    }
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
                auto d = create(f);
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
            auto d = create(f);
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
        auto d = create(f);
        const auto out = render(*d, silence(48000, f.info().buses.inputChannels), {.sampleRate = kSampleRate});
        CHECK(peakDb(out.slice(36000, 12000)) < -120.0);
        CHECK(countSubnormals(out) == 0);
    });
}

TEST_CASE("conformance: survives parameter fuzzing", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        auto d = create(f);
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
                auto d = create(f);
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
        auto a = create(f);
        randomize(*a, 1234);
        const std::string saved = serializeState(*a);

        auto b = create(f);
        REQUIRE(deserializeState(*b, saved) == ApplyResult::Ok);
        CHECK(serializeState(*b) == saved);
        CHECK(bitIdentical(render(*a, s.audio, optionsFor(s)), render(*b, s.audio, optionsFor(s))));
    });
}

TEST_CASE("conformance: reset is deterministic", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        const auto s = stimulusFor(f.info(), 24000);
        auto d = create(f);
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
        // Measured at defaults (filters at random settings add group delay, which is not latency),
        // through the dry path where there's a Dry/Wet: held back by the latency so it lines up
        // with the wet, it's the latency alone even where the defaults filter (the Filter's).
        auto d = create(f);
        if (const auto mix = d->params().indexOf("mix"))
            d->params().set(*mix, 0.0f);
        const auto in = whiteNoise(16384, 3, 0.25f, f.info().buses.inputChannels);
        const auto out = render(*d, in, {.sampleRate = kSampleRate});
        CHECK(estimateLatency(in, out, 2048) == d->latencySamples());

        // ADR 0016: reported latency never depends on parameters...
        for (std::uint32_t setting : {21u, 22u, 23u})
        {
            auto r = create(f);
            randomize(*r, setting);
            r->prepare({kSampleRate, 512});
            INFO("setting " << setting);
            CHECK(r->latencySamples() == d->latencySamples());
        }
        // ...except settings marked as changing it (ADR 0020): measured, value by value.
        for (size_t i = 0; i < d->params().size(); ++i)
        {
            const auto& spec = d->params().spec(i);
            if (!spec.hasFlag(ParamFlags::ChangesLatency))
                continue;
            CHECK_FALSE(spec.hasFlag(ParamFlags::Automatable));
            for (int step = 0; step < std::max(2, spec.numSteps()); ++step)
            {
                auto r = create(f);
                r->params().set(i, spec.min + static_cast<float>(step));
                INFO(spec.id << " = " << r->params().get(i));
                const auto measured = render(*r, in, {.sampleRate = kSampleRate});
                CHECK(estimateLatency(in, measured, std::max(2048, r->latencySamples() + 256)) ==
                      r->latencySamples());
            }
        }
    });
}

// --- MIDI effects (ADR 0074) ---------------------------------------------------------------

namespace
{
bool isNoteEffect(const DeviceInfo& info)
{
    return info.kind == DeviceKind::MidiEffect;
}

/// What a MIDI effect hears: a melody and chords, overlapping, with a controller and pressure; a
/// song of chords changing every two beats, in a key. Deterministic from the seed.
struct NoteStimulus
{
    std::vector<TimedEvent> events;
    SongView song;
};

NoteStimulus noteStimulusFor(int numSamples, std::uint32_t seed)
{
    NoteStimulus s;
    Rng rng(seed);
    std::vector<std::pair<std::int64_t, TimedEvent>> offs;
    for (std::int64_t at = 0; at < numSamples - 4000;
         at += 1500 + static_cast<std::int64_t>(rng.nextU32() % 4000))
    {
        const int notes = 1 + static_cast<int>(rng.nextU32() % 3);
        for (int n = 0; n < notes; ++n)
        {
            const auto key = static_cast<std::uint8_t>(40 + rng.nextU32() % 40);
            s.events.push_back({at,
                                {.type = EventType::NoteOn,
                                 .key = key,
                                 .value = 0.3f + 0.6f * static_cast<float>(rng.next01())}});
            const auto end = std::min<std::int64_t>(numSamples - 1, at + 500 + rng.nextU32() % 9000);
            offs.push_back({end, {end, {.type = EventType::NoteOff, .key = key}}});
        }
        if (rng.nextU32() % 4 == 0)
            s.events.push_back({at + 7, {.type = EventType::Controller, .key = 1, .value = 0.5f}});
    }
    for (const auto& [at, e] : offs)
        s.events.push_back(e);
    std::stable_sort(s.events.begin(), s.events.end(), [](const TimedEvent& a, const TimedEvent& b)
                     { return a.samplePosition < b.samplePosition; });
    // Notes that overlap themselves are left as they come: a module has to cope.
    for (int i = 0; i < 64; ++i)
    {
        const int root = static_cast<int>((i * 7) % 12);
        const std::uint16_t triad = (i % 3 == 1) ? 0b000010001001 : 0b000010010001;
        std::uint16_t notes = 0;
        for (int b = 0; b < 12; ++b)
            if ((triad >> b) & 1u)
                notes = static_cast<std::uint16_t>(notes | (1u << ((b + root) % 12)));
        s.song.chords.push_back({.start = i * 2.0, .end = i * 2.0 + 2.0, .root = root, .notes = notes});
    }
    s.song.sections = {{.start = 0.0, .end = 16.0, .name = 1}, {.start = 16.0, .end = 128.0, .name = 2}};
    return s;
}

RenderOptions noteOptionsFor(const NoteStimulus& s, std::vector<TimedEvent>& out,
                             std::vector<int> pattern = {512})
{
    RenderOptions o;
    o.sampleRate = kSampleRate;
    o.blockPattern = std::move(pattern);
    o.events = s.events;
    o.song = &s.song;
    o.transport.key = {.root = 2, .notes = 0x0AD6}; // D major: D E F♯ G A B C♯
    o.eventsOut = &out;
    return o;
}

std::vector<TimedEvent> notesOut(Device& d, const NoteStimulus& s, int numSamples,
                                 std::vector<int> pattern = {512})
{
    std::vector<TimedEvent> out;
    render(d, AudioBuffer(1, numSamples), noteOptionsFor(s, out, std::move(pattern)));
    return out;
}

bool sameNotes(const std::vector<TimedEvent>& a, const std::vector<TimedEvent>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].samplePosition != b[i].samplePosition || a[i].event.type != b[i].event.type ||
            a[i].event.key != b[i].event.key || a[i].event.channel != b[i].event.channel ||
            a[i].event.value != b[i].event.value)
            return false;
    return true;
}

/// Every note started is ended once, and none is ended that wasn't started.
std::string unbalanced(const std::vector<TimedEvent>& events)
{
    std::map<int, int> sounding;
    for (const auto& e : events)
    {
        const int k = e.event.channel * 128 + e.event.key;
        if (e.event.type == EventType::NoteOn)
            ++sounding[k];
        else if (e.event.type == EventType::NoteOff && --sounding[k] < 0)
            return "note " + std::to_string(e.event.key) + " ended unstarted at " +
                   std::to_string(e.samplePosition);
    }
    for (const auto& [k, n] : sounding)
        if (n != 0)
            return "note " + std::to_string(k % 128) + " left sounding";
    return {};
}
} // namespace

TEST_CASE("conformance: a MIDI effect's notes are the same for any block size", "[fast][conformance]")
{
    forEachDevice(
        [](const DeviceFactory& f)
        {
            if (!isNoteEffect(f.info()))
                return;
            const auto s = noteStimulusFor(96000, 3);
            for (std::uint32_t setting : {0u, 11u, 12u, 13u})
            {
                auto make = [&]
                {
                    auto d = create(f);
                    if (setting != 0)
                        randomize(*d, setting);
                    return d;
                };
                auto ref = make();
                const auto reference = notesOut(*ref, s, 96000);
                for (const auto& pattern :
                     std::vector<std::vector<int>>{{1}, {7}, {64}, {480}, {4096}, {3, 100, 31, 33, 1000}})
                {
                    auto d = make();
                    INFO("setting " << setting << ", first block size " << pattern.front());
                    CHECK(sameNotes(notesOut(*d, s, 96000, pattern), reference));
                }
            }
        });
}

TEST_CASE("conformance: a MIDI effect ends every note it starts, through stops and jumps",
          "[fast][conformance]")
{
    forEachDevice(
        [](const DeviceFactory& f)
        {
            if (!isNoteEffect(f.info()))
                return;
            for (std::uint32_t seed = 1; seed <= 12; ++seed)
            {
                auto d = create(f);
                if (seed > 1)
                    randomize(*d, seed * 31);
                const auto s = noteStimulusFor(192000, seed);
                std::vector<TimedEvent> out;
                auto o = noteOptionsFor(s, out, {64, 1024, 333, 17});
                // The host's way: a stop or a jump comes with all notes off (DevicePlugin, step 0a).
                Rng rng(seed);
                std::vector<std::int64_t> jumps;
                for (int j = 0; j < 4; ++j)
                    jumps.push_back(rng.nextU32() % 192000);
                auto events = o.events;
                for (const auto at : jumps)
                    events.push_back({at, {.type = EventType::AllNotesOff}});
                std::stable_sort(events.begin(), events.end(), [](const TimedEvent& a, const TimedEvent& b)
                                 { return a.samplePosition < b.samplePosition; });
                events.push_back({191999, {.type = EventType::AllNotesOff}});
                o.events = events;
                o.transportAt = [jumps](TransportInfo& t, std::int64_t at)
                {
                    for (const auto j : jumps)
                        if (at >= j)
                            t.ppqPosition += 3.25;                         // jumped on
                    t.playing = !(at >= jumps[0] && at < jumps[0] + 9000); // stopped a while
                };
                render(*d, AudioBuffer(1, 192000), o);
                INFO("seed " << seed);
                CHECK(unbalanced(out).empty());
                CAPTURE(unbalanced(out));
            }
        });
}

TEST_CASE("conformance: a MIDI effect's state and reset reproduce its notes", "[fast][conformance]")
{
    forEachDevice(
        [](const DeviceFactory& f)
        {
            if (!isNoteEffect(f.info()))
                return;
            const auto s = noteStimulusFor(96000, 9);
            auto a = create(f);
            randomize(*a, 4321);
            const auto first = notesOut(*a, s, 96000);
            auto b = create(f);
            REQUIRE(deserializeState(*b, serializeState(*a)) == ApplyResult::Ok);
            CHECK(sameNotes(notesOut(*b, s, 96000), first));
            a->reset();
            std::vector<TimedEvent> again;
            auto o = noteOptionsFor(s, again);
            o.skipPrepare = true;
            render(*a, AudioBuffer(1, 96000), o);
            CHECK(sameNotes(again, first));
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
            auto d = create(f);
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
        auto d = create(f);
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
        auto d = create(f);
        const auto s = stimulusFor(f.info(), 48000);
        auto opts = optionsFor(s, {1, 64, 4096});
        Rng rng(5);
        opts.beforeBlock = [&](Device& dev, std::int64_t) { randomize(dev, rng.nextU32()); };
        const auto before = allocationsDuringProcess();
        render(*d, s.audio, opts);
        CHECK(allocationsDuringProcess() - before == 0);
    });
}

namespace
{
std::vector<std::string> tableCells(const std::string& line)
{
    std::vector<std::string> cells;
    std::string cell;
    for (size_t i = 1; i < line.size(); ++i)
    {
        if (line[i] == '|')
        {
            const auto first = cell.find_first_not_of(' '), last = cell.find_last_not_of(' ');
            cells.push_back(first == std::string::npos ? std::string() : cell.substr(first, last - first + 1));
            cell.clear();
        }
        else
            cell += line[i];
    }
    return cells;
}

/// Each parameter's description in the device's SPEC: the "Description" column of every table
/// under "## Parameters". IDs with "<n>" (e.g. `band<n>.freq`) stand for every number.
std::map<std::string, std::string> specDescriptions(const DeviceInfo& info, std::vector<std::string>& problems)
{
    std::ifstream file(std::string(FD_SOURCE_DIR) + "/devices/" + std::string(info.id) + "/SPEC.md");
    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();
    std::map<std::string, std::string> out;
    // Every Parameters section: the core's ("## Parameters") and a later stage's ("### Parameters"
    // under its own heading), each up to the next heading.
    std::vector<std::string> lines;
    {
        std::istringstream all(text);
        static const std::regex heading("^#{2,3} Parameters\\s*$");
        bool in = false;
        for (std::string line; std::getline(all, line);)
        {
            if (line.starts_with("#"))
                in = std::regex_match(line, heading);
            else if (in)
                lines.push_back(line);
        }
    }
    if (lines.empty())
    {
        problems.push_back("SPEC.md has no Parameters section");
        return out;
    }
    for (size_t i = 0; i + 1 < lines.size(); ++i)
    {
        if (!lines[i].starts_with("|") || !lines[i + 1].starts_with("|---"))
            continue;
        const auto header = tableCells(lines[i]);
        const auto column = std::find(header.begin(), header.end(), "Description") - header.begin();
        if (column == static_cast<long>(header.size()))
        {
            problems.push_back("a parameter table without a Description column");
            continue;
        }
        for (i += 2; i < lines.size() && lines[i].starts_with("|"); ++i)
        {
            const auto cells = tableCells(lines[i]);
            if (static_cast<long>(cells.size()) <= column)
                continue;
            static const std::regex idPattern("`([a-z0-9.<>-]+)`");
            for (std::sregex_iterator it(cells[0].begin(), cells[0].end(), idPattern), end; it != end; ++it)
            {
                auto pattern = std::regex_replace((*it)[1].str(), std::regex("\\."), "\\.");
                pattern = std::regex_replace(pattern, std::regex("<[a-z]>"), "[0-9]+"); // families: <n>, <k>
                const std::regex matcher(pattern);
                for (const auto& p : info.params)
                    if (std::regex_match(std::string(p.id), matcher))
                        out[std::string(p.id)] = cells[static_cast<size_t>(column)];
            }
        }
    }
    return out;
}
} // namespace

TEST_CASE("conformance: every parameter says what it does, in the same words as its SPEC", "[fast][conformance]")
{
    for (const auto* factory : allDevices())
    {
        const auto& info = factory->info();
        INFO(info.id);
        std::vector<std::string> problems;
        const auto spec = specDescriptions(info, problems);
        CHECK(problems.empty());
        for (const auto& p : info.params)
        {
            if (p.hasFlag(ParamFlags::Hidden))
                continue;
            INFO(p.id);
            CHECK_FALSE(p.description.empty());
            CHECK(p.description.ends_with("."));
            CHECK(p.description.size() < 140); // one sentence, fit for a tooltip
            REQUIRE(spec.contains(std::string(p.id)));
            CHECK(spec.at(std::string(p.id)) == p.description);
        }
    }
}

TEST_CASE("conformance: meters are named, unique and on the analysis bus", "[fast][conformance]")
{
    forEachDevice([](const DeviceFactory& f) {
        std::set<std::string_view> ids;
        for (const auto& m : f.info().meters)
        {
            INFO(m.id);
            CHECK(m.slot >= 0);
            CHECK(m.slot < AnalysisBus::numSlots);
            CHECK(ids.insert(m.id).second);
            CHECK_FALSE(m.name.empty());
            CHECK(std::all_of(m.id.begin(), m.id.end(), [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; }));
        }
    });
}

// ---- factory presets (ADR 0044, docs/design/presets.md §13) ---------------------------------

namespace
{
/// Words a preset may not use: other vendors' names, products and famous hardware (ADR 0008).
const std::regex kBannedWords("\\b(ableton|live|bitwig|logic|cubase|fabfilter|waves|universal "
                              "audio|\\buad?\\b|softube|native instruments|serum|"
                              "massive|vital|sylenth|moog|minimoog|roland|juno|jupiter|tb-?303|tr-?808|tr-?"
                              "909|prophet|oberheim|korg|ms-?20|"
                              "neve|ssl|api|pultec|la-?2a|1176|fairchild|distressor|tube-?tech|manley|shadow "
                              "hills|ozone|izotope|valhalla)\\b",
                              std::regex::icase);
const std::set<std::string> kTags{"Drums", "Bass", "Vocals", "Keys", "Guitar", "Bus", "Master", "Subtle",
                                  "Heavy", "Warm", "Bright", "Wide", "Clean", "Dirty", "Lead", "Pad", "Pluck",
                                  "Brass", "FX", "Check", "Fix", "Stereo", "Level", "Mix", "Ducking",
                                  // The Sampler's instruments (factory-library.md §7).
                                  "Strings", "Woodwinds", "Orchestral"};

/// Whether a state's sound is in the Library and the Library isn't here (ADR 0055).
bool soundNotHere(const DeviceState& state)
{
    if (!state.extra.is_object())
        return false;
    for (const auto& [key, value] : state.extra.items())
        if (value.is_string() && fd::library::isReference(value.get<std::string>()))
        {
            std::error_code ec;
            const auto file = fd::library::resolve(value.get<std::string>());
            if (file.empty() || !std::filesystem::exists(file, ec))
                return true;
        }
    return false;
}

/// What a preset plays: -18 dBFS pink noise into an effect; a held C3 into an instrument.
AudioBuffer renderPreset(Device& d, const DeviceInfo& info, double* inputRmsDb)
{
    constexpr int length = 96000;
    if (isEffect(info))
    {
        auto in = pinkNoise(length, 7, 0.25f, std::max(1, info.buses.inputChannels));
        const double gain = std::pow(10.0, (-18.0 - rmsDb(in)) / 20.0);
        for (int c = 0; c < in.numChannels(); ++c)
            for (int i = 0; i < in.numSamples(); ++i)
                in.at(c, i) *= static_cast<float>(gain);
        *inputRmsDb = rmsDb(in);
        RenderOptions o;
        o.sampleRate = kSampleRate;
        return render(d, in, o);
    }
    *inputRmsDb = 0.0;
    // A C3, or the note an octave from it the instrument plays (a Sampler's zones).
    const auto key = static_cast<std::uint8_t>(fd::devices::Sampler::playableKey(d, 60));
    return renderWithTail(d, length,
                          {{0, {.type = EventType::NoteOn, .key = key, .value = 0.8f}},
                           {length / 2, {.type = EventType::NoteOff, .key = key}}});
}
} // namespace

TEST_CASE("conformance: transient settings (how you listen) are never automated, kept or in presets",
          "[fast][conformance]")
{
    for (const auto* f : allDevices())
    {
        const auto& info = f->info();
        auto d = f->create();
        for (size_t i = 0; i < d->params().size(); ++i)
        {
            const auto& spec = d->params().spec(i);
            if (!spec.hasFlag(ParamFlags::Transient))
                continue;
            INFO(info.id << ": " << spec.id);
            CHECK_FALSE(spec.hasFlag(ParamFlags::Automatable));
            // Set away from its default: not in the state, and back to the default when one is applied.
            d->params().set(i, spec.def == spec.max ? spec.min : spec.max);
            const auto state = captureState(*d);
            CHECK_FALSE(state.params.contains(std::string(spec.id)));
            REQUIRE(applyState(*d, state) == ApplyResult::Ok);
            CHECK(d->params().get(i) == spec.def);
        }
    }
}

TEST_CASE("conformance: every factory preset loads, plays cleanly within level and is its own",
          "[fast][conformance]")
{
    std::map<std::string, std::vector<Preset>> byDevice;
    int withoutLibrary = 0;
    for (const auto& file : factoryPresetFiles())
    {
        INFO(file.name);
        auto p = parsePresetFile(file.text, std::string(file.name));
        REQUIRE(p);
        byDevice[p->state.deviceId].push_back(std::move(*p));
    }
    for (const auto& [id, presets] : byDevice)
    {
        const auto* factory = findDevice(id);
        REQUIRE(factory != nullptr);
        const auto& info = factory->info();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const auto& p = presets[i];
            INFO(id << ": " << p.name);
            CHECK(presetNameProblem(p.name).empty());
            CHECK_FALSE(std::regex_search(p.name + " " + p.description, kBannedWords));
            CHECK(p.state.stateVersion == info.stateVersion);   // made with the module as it is
            CHECK(p.state.params.size() == static_cast<size_t>(std::count_if( // the whole state
                                               info.params.begin(), info.params.end(), [](const ParamSpec& s)
                                               { return !s.hasFlag(ParamFlags::Transient); })));
            for (const auto& [param, value] : p.state.params)
                CHECK(std::any_of(info.params.begin(), info.params.end(),
                                  [&](const ParamSpec& s) { return s.id == param; }));
            CHECK_FALSE(p.description.empty());
            CHECK(p.description.ends_with("."));
            CHECK_FALSE(p.tags.empty());
            for (const auto& tag : p.tags)
            {
                INFO("tag " << tag);
                CHECK(kTags.contains(tag));
            }
            for (size_t j = 0; j < i; ++j)
            {
                INFO("against " << presets[j].name);
                CHECK_FALSE(sameSound(info, presets[j].state, p.state));
            }
            CHECK_FALSE(sameSound(info, defaultState(info), p.state));

            // A sound in the Library that isn't here (ADR 0055): heard by the Library's own test.
            if (soundNotHere(p.state))
            {
                ++withoutLibrary;
                continue;
            }
            auto d = create(*factory);
            REQUIRE(applyState(*d, p.state) == ApplyResult::Ok);
            double inputRms = 0.0;
            const auto out = renderPreset(*d, info, &inputRms);
            CHECK(allFinite(out));
            CHECK(peakDb(out) <= 0.0);
            // It sounds; except a flipped gate, which passes only what a gate takes away: on steady
            // noise above its threshold, nothing; and a preset that mutes (the Tuner's Silent
            // tuning).
            const auto on = [&](const char* param)
            {
                const auto it = p.state.params.find(param);
                return it != p.state.params.end() && it->second > 0.5f;
            };
            CHECK((rmsDb(out) > -60.0 || on("flip") || on("mute")));
            // A chord at full velocity keeps its headroom (ADR 0069; the whole keyboard is checked in
            // the Release suite).
            if (isInstrument(info))
            {
                auto chordDevice = create(*factory);
                REQUIRE(applyState(*chordDevice, p.state) == ApplyResult::Ok);
                std::vector<TimedEvent> chord;
                for (const std::uint8_t key : {60, 64, 67, 72})
                {
                    chord.push_back({0, {.type = EventType::NoteOn, .key = key, .value = 1.0f}});
                    chord.push_back({72000, {.type = EventType::NoteOff, .key = key}});
                }
                CHECK(peakDb(renderWithTail(*chordDevice, 96000, chord)) <= -1.0);
            }
            if (std::getenv("FD_PRESET_REPORT") != nullptr) // for level matching by hand
                std::cout << id << " | " << p.name << " | level " << rmsDb(out) - inputRms << " dB, peak "
                          << peakDb(out) << " dBFS\n";
        }
    }
    CHECK(byDevice.size() >= 9);
    if (withoutLibrary > 0)
        WARN(withoutLibrary << " presets play sounds from the Library, which isn't here: \"conformance: the "
                               "Sampler's presets play their instruments\" hears them");
}

TEST_CASE("conformance: the Sampler's presets play their instruments from the Library, cleanly, within level",
          "[library][conformance]")
{
    // The Library a development build makes (cmake --build --preset dev --target segno-library).
    const auto library = std::filesystem::path(FD_SOURCE_DIR) / ".cache" / "library";
    if (!std::filesystem::exists(library / "library.json"))
        SKIP("The Library isn't built: cmake --build --preset dev --target segno-library");
    fd::library::setRoot(library);
    const auto* factory = findDevice("sampler");
    REQUIRE(factory != nullptr);
    int heard = 0;
    for (const auto& file : factoryPresetFiles())
    {
        const auto p = parsePresetFile(file.text, std::string(file.name));
        REQUIRE(p);
        if (p->state.deviceId != "sampler")
            continue;
        INFO(p->name);
        auto d = create(*factory);
        REQUIRE(applyState(*d, p->state) == ApplyResult::Ok);
        const auto& sampler = dynamic_cast<const fd::devices::Sampler&>(*d);
        CHECK(sampler.loadError().empty());
        const auto map = sampler.map();
        REQUIRE(map != nullptr);
        CHECK(map->missing == 0);
        CHECK(map->ignored.empty());
        // As the preview plays it: a C1 for a bass or a kit (its kick), a C3 for the rest, in the
        // instrument's register.
        const bool low = !p->tags.empty() && (p->tags.front() == "Bass" || p->tags.front() == "Drums");
        const auto key = static_cast<std::uint8_t>(fd::devices::Sampler::playableKey(*d, low ? 36 : 60));
        const auto out = renderWithTail(*d, 96000,
                                        {{0, {.type = EventType::NoteOn, .key = key, .value = 0.8f}},
                                         {48000, {.type = EventType::NoteOff, .key = key}}});
        CHECK(allFinite(out));
        CHECK(peakDb(out) <= -1.0);
        CHECK(rmsDb(out) > -60.0); // it sounds
        if (std::getenv("FD_PRESET_REPORT") != nullptr)
            std::cout << "sampler | " << p->name << " | level " << rmsDb(out) << " dB, peak " << peakDb(out)
                      << " dBFS\n";
        ++heard;
    }
    CHECK(heard == 48);
    fd::library::setRoot({});
}

TEST_CASE("conformance: every instrument's factory preset plays cleanly anywhere on the keyboard",
          "[.presets]")
{
    // ADR 0069: a note in every octave and four-note chords on five roots at full velocity, none
    // above -1 dBFS, the notes in the preset's range (its recipe's "keys", or 36 to 84) within its
    // set's spread (12 dB, or the recipes' "keyboardSpread"). The Release suite runs it.
    for (const auto& file : factoryPresetFiles())
    {
        const auto p = parsePresetFile(file.text, std::string(file.name));
        REQUIRE(p);
        const auto* factory = findDevice(p->state.deviceId);
        REQUIRE(factory != nullptr);
        if (!isInstrument(factory->info()) || soundNotHere(p->state))
            continue;
        std::ifstream in(std::string(FD_SOURCE_DIR) + "/devices/" + p->state.deviceId +
                         "/presets/recipes.json");
        std::stringstream text;
        text << in.rdbuf();
        const auto recipes = nlohmann::json::parse(text.str(), nullptr, false);
        double spread = 12.0;
        int low = 36, high = 84;
        if (recipes.is_object())
        {
            spread = recipes.value("keyboardSpread", 12.0);
            for (const auto& r : recipes["presets"])
                if (r.value("name", std::string()) == p->name && r.contains("keys"))
                {
                    low = r["keys"][0].get<int>();
                    high = r["keys"][1].get<int>();
                }
        }
        INFO(p->state.deviceId << ": " << p->name);
        const auto problems = checkKeyboard(*factory, p->state).problems(-1.0, spread, low, high);
        for (const auto& problem : problems)
            UNSCOPED_INFO(problem);
        CHECK(problems.empty());
    }
}

TEST_CASE("conformance: the factory presets are what their recipes make", "[fast][conformance]")
{
    // devices/<id>/presets/recipes.json is the source (presets.md §13); `fd-presets make` writes
    // the files beside it. A preset edited by hand, or a recipe changed without remaking, fails here.
    std::map<std::string, std::map<std::string, std::string>> built; // device -> name -> text
    for (const auto& file : factoryPresetFiles())
        if (const auto p = parsePresetFile(file.text, std::string(file.name)))
            built[p->state.deviceId][std::string(file.name)] = std::string(file.text);
    forEachDevice(
        [&](const DeviceFactory& f)
        {
            const auto path =
                std::string(FD_SOURCE_DIR) + "/devices/" + std::string(f.info().id) + "/presets/recipes.json";
            std::ifstream in(path);
            if (!in)
            {
                CHECK(built[std::string(f.info().id)].empty()); // no recipes, no presets
                return;
            }
            std::stringstream text;
            text << in.rdbuf();
            const auto made = presetsFromRecipes(f, nlohmann::json::parse(text.str()));
            for (const auto& problem : made.problems)
                UNSCOPED_INFO(problem);
            CHECK(made.problems.empty());
            auto& files = built[std::string(f.info().id)];
            CHECK(made.presets.size() == files.size());
            for (const auto& p : made.presets)
            {
                INFO(p.name);
                REQUIRE(files.contains(p.name));
                CHECK(files.at(p.name) == presetFileText(p));
            }
        });
}
