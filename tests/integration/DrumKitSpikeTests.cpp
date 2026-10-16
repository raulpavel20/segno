// The Drum Kit, step 0 (docs/design/drum-kit.md §15): what a kit's pads need from tracktion's racks,
// checked before the kit is built on them. A kit is a rack with a chain a pad: the pad's inlet
// (engine/PadInlet.h), its modules, its volume and pan; the track's own audio goes round the pads,
// once. Checked: a pad hears only its note, at the note it plays, to the sample; pressure reaches
// its pad, bend every pad; a choke stops the rest of its group within the Sampler's steal fade,
// without a click; pads in time whatever their latency; a pad added while playing; a kit's state
// copied whole; pads swapped and cleared, each undone. The CPU and a whole kit's swap are timed in
// Release ([.benchmark]).
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/PadInlet.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fd/device/Registry.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

namespace te = tracktion;
using fd::engine::PadInlet;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBeat = 24000; // samples a beat at 120 bpm

// ---- a probe: marks what reaches an instrument, where it reaches it ------------------------

const fd::DeviceInfo probeInfo{.id = "pad-probe",
                               .name = "Pad probe",
                               .category = "Test",
                               .kind = fd::DeviceKind::Instrument,
                               .buses = {.inputChannels = 0, .outputChannels = 2, .acceptsMidi = true}};

/// One sample a mark for each event, at its offset: a note-on its key / 256, polyphonic pressure
/// minus its key / 512, pitch bend 1 / 1024, All sound off 3 / 4.
class PadProbe final : public fd::Device
{
public:
    PadProbe() : Device(probeInfo) {}
    void prepare(const fd::ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(fd::ProcessContext& c) noexcept override
    {
        for (int ch = 0; ch < c.output.numChannels; ++ch)
            std::fill_n(c.output.channel(ch), c.numSamples, 0.0f);
        if (c.events == nullptr)
            return;
        for (const auto& e : *c.events)
        {
            float mark = 0.0f;
            switch (e.type)
            {
            case fd::EventType::NoteOn:
                mark = static_cast<float>(e.key) / 256.0f;
                break;
            case fd::EventType::PolyPressure:
                mark = -static_cast<float>(e.key) / 512.0f;
                break;
            case fd::EventType::PitchBend:
                mark = 1.0f / 1024.0f;
                break;
            case fd::EventType::AllSoundOff:
                mark = 0.75f;
                break;
            case fd::EventType::NoteOff:
            case fd::EventType::Controller:
            case fd::EventType::ChannelPressure:
            case fd::EventType::AllNotesOff:
                break;
            }
            for (int ch = 0; ch < c.output.numChannels; ++ch)
                c.output.channel(ch)[e.sampleOffset] += mark;
        }
    }
};

const fd::DeviceFactory probeFactory{[]() -> const fd::DeviceInfo& { return probeInfo; },
                                     []() -> std::unique_ptr<fd::Device>
                                     { return std::make_unique<PadProbe>(); }};

struct TestUIBehaviour final : te::UIBehaviour
{
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }
    }
};

/// A sound on disk for the pads' Samplers.
struct Sample
{
    juce::TemporaryFile file{".wav"};
    explicit Sample(const AudioBuffer& sound)
    {
        REQUIRE(writeWav(file.getFile().getFullPathName().toStdString(), sound, kSampleRate));
    }
    [[nodiscard]] std::string path() const { return file.getFile().getFullPathName().toStdString(); }
};

/// A pad as the kit will hold it: the note it answers to, the note it plays, its choke group, and
/// its modules.
struct Pad
{
    int note = 36;
    int plays = 60;
    int choke = 0;
    std::vector<te::Plugin::Ptr> modules;
};

/// Builds kits on an edit's first track (drum-kit.md §15).
struct Kits
{
    te::Edit& edit;

    te::AudioTrack& track() { return *te::getAudioTracks(edit)[0]; }

    te::Plugin::Ptr module(const std::string& id)
    {
        auto p = edit.getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor(id), {});
        REQUIRE(p != nullptr);
        return p;
    }

    static void set(te::Plugin& p, const std::string& param, float plain)
    {
        auto& d = dynamic_cast<fd::engine::DevicePlugin&>(p);
        const auto i = *d.device().params().indexOf(param);
        d.setStoredValue(i, d.device().info().params[i].toNormalized(plain));
    }

    /// A Sampler playing `sample`: One-shot unless `classic`.
    te::Plugin::Ptr sampler(const Sample& sample, bool classic = false)
    {
        auto p = module("sampler");
        set(*p, "mode", classic ? 0.0f : 1.0f);
        fd::engine::DevicePluginBinding(dynamic_cast<fd::engine::DevicePlugin&>(*p))
            .applyExtraState({{"sample.path", sample.path()}});
        return p;
    }

    /// One pad's chain: its inlet, its modules, its volume and pan, from the kit's MIDI input to its
    /// output.
    void addPad(te::RackType& kit, const Pad& pad, juce::UndoManager* um = nullptr)
    {
        auto inlet = edit.getPluginCache().createNewPlugin(PadInlet::xmlTypeName, {});
        REQUIRE(inlet != nullptr);
        auto& in = dynamic_cast<PadInlet&>(*inlet);
        in.setNote(pad.note, um);
        in.setPlays(pad.plays, um);
        in.setChoke(pad.choke, um);
        std::vector<te::Plugin::Ptr> all{inlet};
        all.insert(all.end(), pad.modules.begin(), pad.modules.end());
        auto mixer = edit.getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {});
        REQUIRE(mixer != nullptr);
        mixer->state.setProperty(te::IDs::panLaw, static_cast<int>(te::PanLawBalance),
                                 um); // as a rack chain's
        all.push_back(mixer);
        for (size_t i = 0; i < all.size(); ++i)
            REQUIRE(kit.addPlugin(
                all[i], {0.2f + 0.6f * static_cast<float>(i) / static_cast<float>(all.size()), 0.5f}, false));
        kit.addConnection({}, 0, inlet->itemID, 0); // notes only: the pad's sound starts from silence
        for (size_t i = 0; i + 1 < all.size(); ++i)
            for (int pin = 0; pin <= 2; ++pin)
                kit.addConnection(all[i]->itemID, pin, all[i + 1]->itemID, pin);
        for (int pin = 1; pin <= 2; ++pin)
            kit.addConnection(all.back()->itemID, pin, {}, pin);
    }

    te::RackType::Ptr kit(const std::vector<Pad>& pads)
    {
        auto r = edit.getRackList().addNewRack();
        REQUIRE(r != nullptr);
        r->rackName = "Drum Kit";
        // The track's own audio round the pads, once (ADR 0019: an instrument adds to it).
        for (int pin = 1; pin <= 2; ++pin)
            REQUIRE(r->addConnection({}, pin, {}, pin));
        for (const auto& pad : pads)
            addPad(*r, pad);
        PadInlet::updateChokes(*r);
        return r;
    }

    te::Plugin::Ptr place(te::RackType& r)
    {
        auto instance = edit.getPluginCache().getOrCreatePluginFor(te::RackInstance::create(r));
        REQUIRE(instance != nullptr);
        track().pluginList.insertPlugin(instance, 0, nullptr);
        return instance;
    }

    te::MidiClip& clip(double seconds)
    {
        for (auto* c : track().getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*>(c))
                return *midi;
        auto c =
            track().insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(seconds)}, nullptr);
        REQUIRE(c != nullptr);
        return *c;
    }

    /// A note at a sample (at 120 bpm), a sixteenth long unless said, at full velocity.
    void note(int key, int atSample, double lengthBeats = 0.25, int velocity = 127)
    {
        clip(4.0).getSequence().addNote(key,
                                        te::BeatPosition::fromBeats(atSample / static_cast<double>(kBeat)),
                                        te::BeatDuration::fromBeats(lengthBeats), velocity, 0, nullptr);
    }

    AudioBuffer render(double seconds)
    {
        juce::TemporaryFile out(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters p(edit);
        p.destFile = out.getFile();
        p.audioFormat = &wav;
        p.bitDepth = 32;
        p.sampleRateForAudio = kSampleRate;
        p.blockSizeForAudio = 512;
        p.time = {te::TimePosition(), te::TimeDuration::fromSeconds(seconds)};
        p.tracksToDo = te::toBitSet(te::getAllTracks(edit));
        p.canRenderInMono = false;
        p.checkNodesForAudio = false;
        const auto file = te::Renderer::renderToFile("drum kit spike", p);
        REQUIRE(file.existsAsFile());
        auto read = readWav(file.getFullPathName().toStdString());
        REQUIRE(read.has_value());
        return read->buffer;
    }
};

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    te::Engine engine{"fd-drum-kit-spike", std::make_unique<TestUIBehaviour>(),
                      fd::engine::makeEngineBehaviour()};
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<Kits> kits;

    Fixture()
    {
        fd::engine::registerDevices(engine);
        fd::engine::registerDevice(engine, probeFactory);
        edit = te::Edit::createSingleTrackEdit(engine, te::Edit::EditRole::forRendering);
        kits = std::make_unique<Kits>(Kits{*edit});
    }
    Kits* operator->() { return kits.get(); }
};

/// Every sample of channel 0 that isn't silent, as (index, value).
std::vector<std::pair<int, float>> marks(const AudioBuffer& b)
{
    std::vector<std::pair<int, float>> out;
    for (int i = 0; i < b.numSamples(); ++i)
        if (std::abs(b.at(0, i)) > 1.0e-6f)
            out.emplace_back(i, b.at(0, i));
    return out;
}

AudioBuffer part(const AudioBuffer& b, int start, int length)
{
    AudioBuffer out(b.numChannels(), length);
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = 0; i < length; ++i)
            out.at(c, i) = b.at(c, start + i);
    return out;
}

/// The largest step between neighbouring samples of channel 0 in [start, start + length): a click
/// shows as a step far larger than the sound's own.
float largestStep(const AudioBuffer& b, int start, int length)
{
    float most = 0.0f;
    for (int i = std::max(1, start); i < start + length && i < b.numSamples(); ++i)
        most = std::max(most, std::abs(b.at(0, i) - b.at(0, i - 1)));
    return most;
}
} // namespace

TEST_CASE("drum kit (spike): a pad hears only its own note, at the note it plays, to the sample",
          "[integration]")
{
    Fixture f;
    auto kit = f->kit({{.note = 36, .plays = 60, .modules = {f->module("pad-probe")}},
                       {.note = 38, .plays = 62, .modules = {f->module("pad-probe")}}});
    f->place(*kit);
    f->note(36, kBeat / 2);     // pad 1
    f->note(38, kBeat);         // pad 2
    f->note(40, 3 * kBeat / 2); // no pad
    f->note(36, 56'837);        // pad 1 again, mid-block
    const auto got = marks(f->render(2.0));
    // Each pad marks the note it plays, where its note was, and nothing else is heard: no other
    // pad's note, and nothing for the note without a pad.
    const std::vector<std::pair<int, float>> expected{
        {kBeat / 2, 60.0f / 256.0f}, {kBeat, 62.0f / 256.0f}, {56'837, 60.0f / 256.0f}};
    REQUIRE(got.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        INFO("mark " << i);
        CHECK(got[i].first == expected[i].first);
        CHECK(got[i].second == Approx(expected[i].second).margin(1.0e-6));
    }
}

TEST_CASE("drum kit (spike): pressure reaches its own pad, pitch bend every pad", "[integration]")
{
    Fixture f;
    auto kit = f->kit({{.note = 36, .plays = 60, .modules = {f->module("pad-probe")}},
                       {.note = 38, .plays = 62, .modules = {f->module("pad-probe")}}});
    f->place(*kit);
    auto& seq = f->clip(4.0).getSequence();
    const auto at = [](int sample)
    { return te::BeatPosition::fromBeats(sample / static_cast<double>(kBeat)); };
    seq.addControllerEvent(at(18'000), te::MidiControllerEvent::aftertouchType, 100 << 7, 36, nullptr);
    seq.addControllerEvent(at(30'000), te::MidiControllerEvent::aftertouchType, 100 << 7, 38, nullptr);
    seq.addControllerEvent(at(42'000), te::MidiControllerEvent::pitchWheelType, 12'000, nullptr);
    const auto got = marks(f->render(1.5));
    // Pressure on 36 reaches pad 1 only, at the note it plays (60); on 38, pad 2 only (62); the
    // bend reaches both, so it's marked twice over.
    const std::vector<std::pair<int, float>> expected{
        {18'000, -60.0f / 512.0f}, {30'000, -62.0f / 512.0f}, {42'000, 2.0f / 1024.0f}};
    REQUIRE(got.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        INFO("mark " << i);
        CHECK(got[i].first == expected[i].first);
        CHECK(got[i].second == Approx(expected[i].second).margin(1.0e-6));
    }
}

TEST_CASE("drum kit (spike): the closed hat chokes the open hat within the steal fade, without a click",
          "[integration]")
{
    // The open hat: a long 330 Hz tone, played to its end (One-shot). The closed hat: silence, so
    // whatever sounds after it is the open hat.
    const Sample open(sine(330.0, kSampleRate, 96'000, 0.5f, 1));
    const Sample closed(silence(4800, 1));
    const int fade = static_cast<int>(std::lround(0.0015 * kSampleRate)); // the Sampler's steal fade
    for (const int chokeAt : {36'037, 60'300})                            // two places in a block
    {
        for (const bool grouped : {true, false})
        {
            INFO("closed hat at " << chokeAt << (grouped ? ", grouped" : ", in no group"));
            Fixture f;
            auto kit = f->kit({{.note = 46, .choke = 1, .modules = {f->sampler(open)}},
                               {.note = 42, .choke = grouped ? 1 : 0, .modules = {f->sampler(closed)}}});
            f->place(*kit);
            f->note(46, 12'000);
            f->note(46, 24'000); // the open hat again: a pad never chokes itself
            f->note(42, chokeAt);
            const auto out = f->render(2.0);
            const double tone = rmsDb(part(out, 30'000, 4800));
            CHECK(tone == Approx(-9.0).margin(0.5)); // still ringing after its own second hit
            const auto after = part(out, chokeAt + fade + 1, 9600);
            if (grouped)
            {
                CHECK(peakDb(after) < -120.0); // gone within the fade
                // No click: no step larger than the tone's own and the fade's together.
                const float own =
                    static_cast<float>(0.5 * 2.0 * juce::MathConstants<double>::pi * 330.0 / kSampleRate);
                const float step = largestStep(out, chokeAt - 48, fade + 96);
                INFO("largest step " << step << ", the tone's own " << own);
                CHECK(step < own + 0.5f / static_cast<float>(fade) + 0.005f);
            }
            else
                CHECK(rmsDb(after) == Approx(-9.0).margin(0.5)); // nothing chokes it
        }
    }
}

TEST_CASE("drum kit (spike): the track's own audio goes round the pads once", "[integration]")
{
    Fixture f;
    const auto noise = whiteNoise(48'000, 7, 0.25f, 2);
    juce::TemporaryFile input(".wav");
    REQUIRE(writeWav(input.getFile().getFullPathName().toStdString(), noise, kSampleRate));
    f->track().insertWaveClip("input", input.getFile(),
                              {{te::TimePosition(), te::TimeDuration::fromSeconds(1.0)}, {}}, false);
    std::vector<Pad> pads;
    for (int i = 0; i < 16; ++i)
        pads.push_back({.note = 36 + i, .modules = {f->module("pad-probe")}});
    auto kit = f->kit(pads);
    f->place(*kit);
    const auto out = f->render(1.0);
    INFO("residual " << nullResidualDb(out, noise) << " dB");
    CHECK(nullResidualDb(out, noise) < -100.0);
}

TEST_CASE("drum kit (spike): a pad sounds as the same instrument on a track does", "[integration]")
{
    const Sample hit(sine(220.0, kSampleRate, 24'000, 0.5f, 1));
    AudioBuffer onTrack, onPad;
    {
        Fixture f;
        f->track().pluginList.insertPlugin(f->sampler(hit), 0, nullptr);
        f->note(60, 12'000);
        onTrack = f->render(1.0);
    }
    {
        Fixture f;
        auto kit = f->kit({{.note = 36, .plays = 60, .modules = {f->sampler(hit)}}});
        f->place(*kit);
        f->note(36, 12'000);
        onPad = f->render(1.0);
    }
    INFO("pad against track: " << nullResidualDb(onPad, onTrack) << " dB");
    CHECK(peakDb(onTrack) > -10.0);
    CHECK(nullResidualDb(onPad, onTrack) < -100.0);
}

TEST_CASE("drum kit (spike): pads stay in time whatever their latency", "[integration]")
{
    Fixture f;
    auto compressor = f->module("compressor");
    Kits::set(*compressor, "lookahead", 2.0f); // 10 ms
    Kits::set(*compressor, "threshold", 0.0f); // the marks stay under it: nothing is compressed
    auto kit = f->kit({{.note = 36, .plays = 60, .modules = {f->module("pad-probe"), compressor}},
                       {.note = 38, .plays = 62, .modules = {f->module("pad-probe")}}});
    f->place(*kit);
    CHECK(compressor->getLatencySeconds() * kSampleRate == Approx(480.0)); // it really is late
    f->note(36, kBeat);
    f->note(38, kBeat);
    const auto got = marks(f->render(1.0));
    // Both pads' marks land together, where the notes are: lined up in the kit, compensated in the
    // song.
    REQUIRE_FALSE(got.empty());
    CHECK(got.front().first == kBeat);
    CHECK(got.front().second == Approx((60.0f + 62.0f) / 256.0f).margin(1.0e-3));
    double elsewhere = 0.0;
    for (size_t i = 1; i < got.size(); ++i)
        elsewhere = std::max(elsewhere, static_cast<double>(std::abs(got[i].second)));
    CHECK(elsewhere < 1.0e-3);
}

TEST_CASE("drum kit (spike): a kit's state, written and read back, plays the same", "[integration]")
{
    std::vector<std::unique_ptr<Sample>> sounds;
    for (int i = 0; i < 16; ++i)
        sounds.push_back(
            std::make_unique<Sample>(sine(110.0 * (1.0 + 0.25 * i), kSampleRate, 4800, 0.2f, 1)));
    juce::String xml;
    AudioBuffer first;
    {
        Fixture f;
        std::vector<Pad> pads;
        for (int i = 0; i < 16; ++i)
            pads.push_back({.note = 36 + i,
                            .choke = i < 4 ? 1 : 0,
                            .modules = {f->sampler(*sounds[static_cast<size_t>(i)])}});
        auto kit = f->kit(pads);
        f->place(*kit);
        for (int i = 0; i < 16; ++i)
            f->note(36 + i, 6000 * i);
        first = f->render(2.5);
        kit->flushStateToValueTree();
        xml = kit->createStateCopy(false).toXmlString();
    }
    // As a kit preset would hold it: the rack's state, into a fresh project.
    Fixture g;
    const auto start = std::chrono::steady_clock::now();
    auto state = juce::ValueTree::fromXml(xml);
    te::EditItemID::remapIDs(state, nullptr, g->edit); // as the Session reads a rack in
    auto copy = g->edit.getRackList().addRackTypeFrom(state);
    REQUIRE(copy != nullptr);
    g->place(*copy);
    PadInlet::updateChokes(*copy); // as the kit does once it's read
    const auto took =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WARN("16 loaded pads read back in " << took << " ms (Debug)");
    int inlets = 0;
    for (auto* p : copy->getPlugins())
        if (auto* inlet = dynamic_cast<PadInlet*>(p))
        {
            ++inlets;
            CHECK(inlet->chokedBy(36) == (inlet->choke() == 1 && inlet->note() != 36));
        }
    CHECK(inlets == 16);
    for (int i = 0; i < 16; ++i)
        g->note(36 + i, 6000 * i);
    const auto second = g->render(2.5);
    INFO("copy against original: " << nullResidualDb(second, first) << " dB");
    CHECK(peakDb(first) > -30.0);
    CHECK(nullResidualDb(second, first) < -100.0);
}

TEST_CASE("drum kit (spike): swapping and clearing pads are each one undo step back to the same sound",
          "[integration]")
{
    const Sample low(sine(220.0, kSampleRate, 9600, 0.3f, 1));
    const Sample high(sine(660.0, kSampleRate, 9600, 0.3f, 1));
    Fixture f;
    auto& um = f->edit.getUndoManager();
    auto kit =
        f->kit({{.note = 36, .modules = {f->sampler(low)}}, {.note = 38, .modules = {f->sampler(high)}}});
    f->place(*kit);
    f->note(36, 12'000);
    f->note(38, 36'000);
    const auto before = f->render(1.5);
    std::vector<PadInlet*> inlets;
    for (auto* p : kit->getPlugins())
        if (auto* inlet = dynamic_cast<PadInlet*>(p))
            inlets.push_back(inlet);
    REQUIRE(inlets.size() == 2);

    SECTION("swapped: the two pads' notes trade places")
    {
        um.beginNewTransaction("Swap pads");
        inlets[0]->setNote(38, &um);
        inlets[1]->setNote(36, &um);
        const auto swapped = f->render(1.5);
        CHECK(nullResidualDb(swapped, before) > -20.0); // heard: the sounds trade places
        um.beginNewTransaction();
        REQUIRE(um.undo());
        CHECK(inlets[0]->note() == 36);
        CHECK(inlets[1]->note() == 38);
        CHECK(nullResidualDb(f->render(1.5), before) < -100.0);
    }
    SECTION("cleared: a pad's chain goes, and comes back whole")
    {
        um.beginNewTransaction("Clear pad");
        std::vector<te::Plugin*> pad;
        // The second pad: its inlet, its Sampler and its mixer, by their chain's connections.
        for (auto* p = static_cast<te::Plugin*>(inlets[1]); p != nullptr;)
        {
            pad.push_back(p);
            te::Plugin* next = nullptr;
            for (auto* c : kit->getConnections())
                if (c->sourceID.get() == p->itemID && c->sourcePin.get() == 1 && c->destID.get().isValid())
                    next = f->edit.getPluginCache().getPluginFor(c->destID.get()).get();
            p = next;
        }
        REQUIRE(pad.size() == 3);
        for (auto* p : pad)
            p->removeFromParent();
        const auto cleared = f->render(1.5);
        CHECK(rmsDb(part(cleared, 36'000, 4800)) < -100.0); // the second pad's gone
        um.beginNewTransaction();
        REQUIRE(um.undo());
        CHECK(nullResidualDb(f->render(1.5), before) < -100.0);
    }
}

TEST_CASE("drum kit (spike): a pad added while the song plays leaves the others sounding", "[integration]")
{
    // Live, through tracktion's hosted device: a held note on one pad, and a second pad added
    // half way.
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-drum-kit-spike-live", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    fd::engine::Session session(engine);
    te::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = kSampleRate;
    io.blockSize = 480;
    io.inputChannels = 0;
    io.outputChannels = 2;
    te::test_utilities::EnginePlayer player(engine, io);
    auto& edit = *session.impl().edit;
    Kits kits{edit};
    const Sample tone(sine(440.0, kSampleRate, 4 * 48'000, 0.5f, 1));
    const Sample other(sine(880.0, kSampleRate, 4800, 0.5f, 1));
    auto kit = kits.kit({{.note = 36, .modules = {kits.sampler(tone, true)}}});
    kits.place(*kit);
    kits.note(36, 0, 6.0);     // held 3 s
    std::vector<float> levels; // each block's peak
    const auto pump = [&](double seconds)
    {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            const auto out = player.process(480);
            levels.push_back(out.getMagnitude(0, 0, out.getNumSamples()));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    session.setPosition(0.0);
    session.play();
    pump(1.0);
    REQUIRE(session.isPlaying());
    const size_t added = levels.size();
    kits.addPad(*kit, {.note = 38, .modules = {kits.sampler(other)}}, &edit.getUndoManager());
    PadInlet::updateChokes(*kit);
    pump(1.0);
    session.stop();
    pump(0.1);
    // Steady before, and every block after the pad came: no gap, no restart.
    float lowest = 1.0f;
    for (size_t b = added - 20; b < added + 80 && b < levels.size(); ++b)
        lowest = std::min(lowest, levels[b]);
    const float steady = levels[added - 5];
    INFO("steady peak " << steady << ", lowest around the new pad " << lowest);
    CHECK(steady > 0.3f); // the tone (0.5) through the live output
    CHECK(lowest > 0.9f * steady);
}

// ---- timed in Release ([.benchmark]): run the rel build with "[.benchmark]" -----------------

TEST_CASE("drum kit (benchmark): a kit of 16, 64 and 128 Sampler pads against as many tracks",
          "[integration][.benchmark]")
{
    const Sample hit(whiteNoise(9600, 3, 0.2f, 1)); // a 200 ms hit
    constexpr double seconds = 8.0;
    for (const int count : {16, 64, 128})
    {
        // A busy pattern: sixteenths, every pad in turn, over 8 s.
        const auto pattern = [&](auto&& place)
        {
            for (int step = 0; step < static_cast<int>(seconds * 8.0); ++step)
                place(step % count, step * kBeat / 4);
        };
        double kitMs = 0.0, tracksMs = 0.0;
        {
            Fixture f;
            std::vector<Pad> pads;
            for (int i = 0; i < count; ++i)
                pads.push_back({.note = i, .modules = {f->sampler(hit)}});
            auto kit = f->kit(pads);
            f->place(*kit);
            pattern([&](int pad, int at) { f->note(pad, at); });
            const auto start = std::chrono::steady_clock::now();
            const auto out = f->render(seconds);
            kitMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            CHECK(rmsDb(out) > -60.0);
        }
        {
            Fixture f;
            f->edit.ensureNumberOfAudioTracks(count);
            const auto tracks = te::getAudioTracks(f->edit);
            for (int i = 0; i < count; ++i)
            {
                auto* t = tracks[i];
                t->pluginList.insertPlugin(f->sampler(hit), 0, nullptr);
                t->insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(seconds)}, nullptr);
            }
            pattern(
                [&](int pad, int at)
                {
                    auto* c = dynamic_cast<te::MidiClip*>(tracks[pad]->getClips()[0]);
                    c->getSequence().addNote(60, te::BeatPosition::fromBeats(at / static_cast<double>(kBeat)),
                                             te::BeatDuration::fromBeats(0.25), 100, 0, nullptr);
                });
            const auto start = std::chrono::steady_clock::now();
            const auto out = f->render(seconds);
            tracksMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            CHECK(rmsDb(out) > -60.0);
        }
        WARN(count << " pads: the kit renders 8 s in " << kitMs << " ms, " << count << " tracks in "
                   << tracksMs << " ms (" << kitMs / tracksMs << "x)");
    }
}

TEST_CASE("drum kit (benchmark): a kit of 16 loaded pads read back whole", "[integration][.benchmark]")
{
    std::vector<std::unique_ptr<Sample>> sounds;
    for (int i = 0; i < 16; ++i)
        sounds.push_back(
            std::make_unique<Sample>(whiteNoise(24'000, static_cast<std::uint32_t>(i + 1), 0.2f, 2)));
    juce::String xml;
    {
        Fixture f;
        std::vector<Pad> pads;
        for (int i = 0; i < 16; ++i)
            pads.push_back({.note = 36 + i, .modules = {f->sampler(*sounds[static_cast<size_t>(i)])}});
        auto kit = f->kit(pads);
        kit->flushStateToValueTree();
        xml = kit->createStateCopy(false).toXmlString();
    }
    Fixture g;
    const auto start = std::chrono::steady_clock::now();
    auto state = juce::ValueTree::fromXml(xml);
    te::EditItemID::remapIDs(state, nullptr, g->edit);
    auto copy = g->edit.getRackList().addRackTypeFrom(state);
    REQUIRE(copy != nullptr);
    g->place(*copy);
    PadInlet::updateChokes(*copy);
    const auto took =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WARN("16 loaded pads (0.5 s stereo samples) read back in " << took << " ms");
#ifdef NDEBUG
    CHECK(took < 100.0); // drum-kit.md §15: a kit swapped quickly enough for A/B, in Release
#endif
}
