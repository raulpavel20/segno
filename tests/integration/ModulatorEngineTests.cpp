// The modulators in the engine (docs/design/modulators.md §12, §13, §17): engine::Modulator, a
// tracktion modifier of our own type, moving what it's routed to. Each kind heard in a render of
// a constant input through a Utility (its gain, sample by sample), Per Target's copies, undo and
// redo, a project read back, Keep and fresh rolls, the Follower where it sits, the Envelope on
// notes and on the beat, Steps in time.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Modulator.h"
#include "fd/testkit/Allocations.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fd/device/Registry.h>

namespace te = tracktion;
using namespace fd::testkit;
using namespace fd::model::modulator;
using Catch::Approx;
using fd::engine::Modulator;
using Kind = fd::model::ModulatorKind;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kLength = 96000; // 2 s, four beats at 120 BPM

struct TestUIBehaviour final : te::UIBehaviour
{
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }
    }
};

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    te::Engine engine{"fd-modulator-engine", std::make_unique<TestUIBehaviour>(),
                      fd::engine::makeEngineBehaviour()};
    juce::TemporaryFile inputFile{".wav"};
    AudioBuffer input = dc(kLength, 0.5f); // the output over 0.5 is the gain, sample by sample
    std::unique_ptr<te::Edit> edit;

    Fixture()
    {
        fd::engine::registerDevices(engine); // the modulators' type too
        REQUIRE(writeWav(inputFile.getFile().getFullPathName().toStdString(), input, kSampleRate));
        edit = te::Edit::createSingleTrackEdit(engine, te::Edit::EditRole::forRendering);
        track().insertWaveClip(
            "input", inputFile.getFile(),
            {{te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)}, {}}, false);
    }

    te::AudioTrack& track() { return *te::getAudioTracks(*edit)[0]; }

    te::Plugin::Ptr module(const std::string& id, int index = 0)
    {
        auto p = edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor(id), {});
        REQUIRE(p != nullptr);
        track().pluginList.insertPlugin(p, index, nullptr);
        return p;
    }

    static te::AutomatableParameter& param(te::Plugin& p, const std::string& id)
    {
        auto& d = dynamic_cast<fd::engine::DevicePlugin&>(p);
        return d.parameter(*d.device().params().indexOf(id));
    }

    static void set(te::Plugin& p, const std::string& id, float plain)
    {
        auto& d = dynamic_cast<fd::engine::DevicePlugin&>(p);
        const auto i = *d.device().params().indexOf(id);
        d.parameter(i).setParameter(d.device().info().params[i].toNormalized(plain),
                                    juce::sendNotificationSync);
    }

    /// A modulator on the track, after its chain (or just before `before`), its roll kept so a
    /// render repeats.
    Modulator& modulator(Kind kind, te::Plugin* before = nullptr)
    {
        auto m = track().getModifierList()->insertModifier(Modulator::createState(kind), -1, nullptr);
        REQUIRE(m != nullptr);
        auto& mod = dynamic_cast<Modulator&>(*m);
        if (before != nullptr)
            mod.setPlace(before->itemID, nullptr);
        mod.startPass(7);
        mod.setKept(true, nullptr);
        return mod;
    }

    static void set(Modulator& m, size_t param, float plain)
    {
        m.parameter(param).setParameter(m.info().params[param].toNormalized(plain),
                                        juce::sendNotificationSync);
    }

    /// A route: `amount` of the target's range (both ways, half of it each way at 1), as copy `copy`.
    static te::AutomatableParameter::ModifierAssignment::Ptr route(Modulator& m, te::AutomatableParameter& p,
                                                                   float amount, int copy = 0)
    {
        auto a = p.addModifier(m, amount, 0.0f, 0.0f);
        REQUIRE(a != nullptr);
        a->state.setProperty(Modulator::copyId, copy, nullptr);
        dynamic_cast<Modulator::Assignment&>(*a).copy.store(copy);
        return a;
    }

    AudioBuffer render(te::Edit* which = nullptr)
    {
        auto& e = which != nullptr ? *which : *edit;
        juce::TemporaryFile out(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters p(e);
        p.destFile = out.getFile();
        p.audioFormat = &wav;
        p.bitDepth = 32;
        p.sampleRateForAudio = kSampleRate;
        p.blockSizeForAudio = 512;
        p.time = {te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)};
        p.tracksToDo = te::toBitSet(te::getAllTracks(e));
        p.canRenderInMono = false;
        p.checkNodesForAudio = false;
        const auto file = te::Renderer::renderToFile("modulators", p);
        REQUIRE(file.existsAsFile());
        auto read = readWav(file.getFullPathName().toStdString());
        REQUIRE(read.has_value());
        return read->buffer;
    }
};

/// The gain in dB at sample `i`: the output over the input's 0.5.
double gainDbAt(const AudioBuffer& out, int i)
{
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(static_cast<double>(out.at(0, i))) / 0.5));
}

/// How far the gain swings (dB) after the first 50 ms, and how often it crosses its middle.
struct Swing
{
    double rangeDb = 0.0;
    int crossings = 0;
};

Swing swingOf(const AudioBuffer& out)
{
    double lo = 1e9, hi = -1e9;
    const int from = static_cast<int>(kSampleRate * 0.05);
    for (int i = from; i < out.numSamples(); ++i)
    {
        lo = std::min(lo, gainDbAt(out, i));
        hi = std::max(hi, gainDbAt(out, i));
    }
    const double middle = 0.5 * (lo + hi);
    Swing s{hi - lo, 0};
    for (int i = from + 1; i < out.numSamples(); ++i)
        if ((gainDbAt(out, i - 1) - middle) * (gainDbAt(out, i) - middle) < 0.0)
            ++s.crossings;
    return s;
}
} // namespace

TEST_CASE("modulators: an LFO moves our module's gain, both ways, in time", "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    Fixture::set(*utility, "gain", -12.0f);
    auto& lfo = f.modulator(Kind::Lfo);
    Fixture::set(lfo, Lfo::Rate, 2.0f);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.2f); // a tenth of the range each way
    const auto swing = swingOf(f.render());
    INFO("swing " << swing.rangeDb << " dB, " << swing.crossings << " crossings");
    CHECK(swing.rangeDb > 6.0);
    CHECK(swing.crossings >= 7); // 2 Hz over 1.95 s
    CHECK(swing.crossings <= 9);

    // Depth 0: still.
    Fixture::set(lfo, Lfo::Depth, 0.0f);
    CHECK(swingOf(f.render()).rangeDb < 0.01);
    // Synced to a bar at 120 BPM: one cycle in 2 s, two crossings.
    Fixture::set(lfo, Lfo::Depth, 100.0f);
    Fixture::set(lfo, Lfo::Sync, 16.0f); // 1 bar
    const auto bar = swingOf(f.render());
    INFO("a bar: " << bar.crossings << " crossings");
    CHECK(bar.crossings >= 1);
    CHECK(bar.crossings <= 3);
}

TEST_CASE("modulators: one way moves it up only; a negative amount turns it down",
          "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    const auto& spec = fd::findDevice("utility")->info().params[0];
    Fixture::set(*utility, "gain", -24.0f);
    auto& lfo = f.modulator(Kind::Lfo);
    Fixture::set(lfo, Lfo::BothWays, 0.0f);
    auto route = Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.3f);
    auto out = f.render();
    double lowest = 1e9;
    for (int i = 0; i < out.numSamples(); ++i)
        lowest = std::min(lowest, gainDbAt(out, i));
    CHECK(lowest == Approx(-24.0).margin(0.3)); // never below where it's set
    route->value = -0.3f;
    out = f.render();
    double highest = -1e9;
    for (int i = 0; i < out.numSamples(); ++i)
        highest = std::max(highest, gainDbAt(out, i));
    CHECK(highest == Approx(-24.0).margin(0.3)); // never above
    (void)spec;
}

TEST_CASE("modulators: each target its own copy (Per Target), or all together", "[integration][modulators]")
{
    Fixture f;
    auto a = f.module("utility", 0);
    auto b = f.module("utility", 1);
    auto& random = f.modulator(Kind::Random);
    auto first = Fixture::route(random, Fixture::param(*a, "gain"), 0.2f, 0);
    auto second = Fixture::route(random, Fixture::param(*b, "gain"), 0.2f, 1);
    random.setRouteCount(2);
    const auto differ = [&](Modulator& m, const auto& x, const auto& y)
    {
        int different = 0;
        for (int i = 0; i < 20; ++i)
        {
            const auto t = te::TimePosition::fromSeconds(0.07 * i);
            if (std::abs(m.getValueFor(*x, t) - m.getValueFor(*y, t)) > 1.0e-5f)
                ++different;
        }
        return different;
    };
    CHECK(differ(random, first, second) >= 18); // on by default for Random
    Fixture::set(random, Random::PerTarget, 0.0f);
    CHECK(differ(random, first, second) == 0);

    // The LFO, Per Target on: its two copies half a cycle apart.
    auto& lfo = f.modulator(Kind::Lfo);
    Fixture::set(lfo, Lfo::PerTarget, 1.0f);
    lfo.setRouteCount(2);
    auto la = Fixture::route(lfo, Fixture::param(*a, "width"), 0.2f, 0);
    auto lb = Fixture::route(lfo, Fixture::param(*b, "width"), 0.2f, 1);
    for (double s : {0.13, 0.4, 0.71})
    {
        const auto t = te::TimePosition::fromSeconds(s);
        CHECK(lfo.getValueFor(*la, t) == Approx(-lfo.getValueFor(*lb, t)).margin(1.0e-5));
    }
}

TEST_CASE("modulators: adding one and its route is undone and redone whole", "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto& um = f.edit->getUndoManager();
    um.beginNewTransaction("Add modulator");
    auto m = f.track().getModifierList()->insertModifier(Modulator::createState(Kind::Lfo), -1, nullptr);
    auto& lfo = dynamic_cast<Modulator&>(*m);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.2f);
    um.beginNewTransaction();
    CHECK(swingOf(f.render()).rangeDb > 6.0);
    REQUIRE(um.undo());
    CHECK(f.track().getModifierList()->getModifiers().isEmpty());
    CHECK(swingOf(f.render()).rangeDb < 0.01);
    REQUIRE(um.redo());
    CHECK(swingOf(f.render()).rangeDb > 6.0);
}

TEST_CASE("modulators: a project read back plays them the same", "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto& random = f.modulator(Kind::Random);
    Fixture::route(random, Fixture::param(*utility, "gain"), 0.2f);
    auto& steps = f.modulator(Kind::Steps);
    steps.setStep(2, 0.9f, 0.5f, nullptr);
    Fixture::route(steps, *f.track().getVolumePlugin()->volParam, 0.1f);
    const auto first = f.render();

    te::Edit::Options options{f.engine, juce::ValueTree::fromXml(f.edit->state.toXmlString()),
                              te::ProjectItemID::createNewID(te::ProjectID{})};
    options.role = te::Edit::forRendering;
    auto copy = te::Edit::createEdit(std::move(options));
    REQUIRE(copy != nullptr);
    const auto mods = te::getAudioTracks(*copy)[0]->getModifierList()->getModifiers();
    REQUIRE(mods.size() == 2);
    auto* stepsCopy = dynamic_cast<Modulator*>(mods[1].get());
    REQUIRE(stepsCopy != nullptr);
    CHECK(stepsCopy->kind() == Kind::Steps);
    CHECK(stepsCopy->stepValues()[2] == Approx(0.9f));
    CHECK(stepsCopy->stepChances()[2] == Approx(0.5f));
    CHECK(stepsCopy->kept());
    const auto second = f.render(copy.get());
    INFO("read back against the original: " << nullResidualDb(second, first) << " dB");
    CHECK(nullResidualDb(second, first) < -100.0);
}

TEST_CASE("modulators: a kept roll plays the same every time; fresh passes roll again (ADR 0062)",
          "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto& random = f.modulator(Kind::Random); // kept at roll 7
    Fixture::route(random, Fixture::param(*utility, "gain"), 0.2f);
    const auto kept = f.render();
    random.startPass(99); // a new pass: a kept roll doesn't care
    CHECK(nullResidualDb(f.render(), kept) < -100.0);

    random.setKept(false, nullptr); // let it roll: this pass's dice
    CHECK_FALSE(random.kept());
    const auto fresh = f.render();
    INFO("a fresh roll against the kept one: " << nullResidualDb(fresh, kept) << " dB");
    CHECK(nullResidualDb(fresh, kept) > -20.0);
    random.startPass(100);
    CHECK(nullResidualDb(f.render(), fresh) > -20.0); // the next pass, other dice

    random.setKept(true, nullptr); // keeps the roll playing now (100)
    const auto again = f.render();
    random.startPass(101);
    CHECK(nullResidualDb(f.render(), again) < -100.0);
}

TEST_CASE("modulators: a Follower hears the sound where it sits", "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    Fixture::set(*utility, "gain", -20.0f);
    auto& before = f.modulator(Kind::Follower, utility.get());
    auto& after = f.modulator(Kind::Follower);
    f.render();
    // Read at the render's end, where the clip's edge fade has begun: what matters is that the
    // one after the Utility hears a tenth of what the one before it hears.
    const float heardBefore = before.getCurrentValue(), heardAfter = after.getCurrentValue();
    INFO("before " << heardBefore << ", after " << heardAfter);
    CHECK(heardBefore > 0.3f);
    CHECK(heardAfter / heardBefore == Approx(0.1f).margin(0.002f));
    // Its Gain scales what it hears.
    Fixture::set(after, Follower::Gain, 20.0f);
    f.render();
    CHECK(after.getCurrentValue() == Approx(before.getCurrentValue()).margin(0.005f));
}

TEST_CASE("modulators: a Follower listening to another track hears it, and passes on only its own track's "
          "sound (§12.4)",
          "[integration][modulators]")
{
    Fixture f;
    f.edit->ensureNumberOfAudioTracks(2);
    auto& quiet = *te::getAudioTracks(*f.edit)[1]; // no clip: silent where the Follower sits
    auto m = quiet.getModifierList()->insertModifier(Modulator::createState(Kind::Follower), -1, nullptr);
    REQUIRE(m != nullptr);
    auto& follower = dynamic_cast<Modulator&>(*m);
    auto out = f.render();
    CHECK(follower.getCurrentValue() < 0.01f); // here: nothing

    follower.state.setProperty(Modulator::listenId, f.track().itemID.toVar(), nullptr);
    CHECK(follower.getSidechainSourceID() == f.track().itemID);
    f.edit->restartPlayback();
    out = f.render();
    INFO("heard " << follower.getCurrentValue());
    CHECK(follower.getCurrentValue() > 0.3f); // the first track's 0.5
    // The mix is the first track's alone: the sound it heard isn't passed on down its own chain.
    float loudest = 0.0f;
    for (int i = 0; i < out.numSamples() / 2; ++i)
        loudest = std::max(loudest, std::abs(out.at(0, i)));
    CHECK(loudest == Approx(0.5f).margin(0.01f));
}

TEST_CASE("modulators: processing allocates nothing: each of the five, routed, hearing notes, playing and "
          "stopped (§20)",
          "[integration][modulators]")
{
    REQUIRE(fd::testkit::allocationTrackingEnabled());
    {
        // The count works in this build: an allocation in a counted scope shows.
        const auto was = fd::testkit::allocationsDuringProcess();
        {
            const fd::testkit::CountAllocations count;
            auto made = std::make_unique<std::vector<int>>(64);
            REQUIRE(made != nullptr);
        }
        REQUIRE(fd::testkit::allocationsDuringProcess() > was);
    }
    Fixture f;
    auto utility = f.module("utility");
    auto& gain = Fixture::param(*utility, "gain");
    std::vector<Modulator*> all;
    for (const auto kind : fd::model::allModulators)
    {
        auto& m = f.modulator(kind, utility.get());
        Fixture::route(m, gain, 0.2f);
        m.initialise(kSampleRate, 512);
        all.push_back(&m);
    }
    constexpr int block = 512;
    juce::AudioBuffer<float> buffer(4, block); // a Follower hearing another track has four
    te::MidiMessageArray midi;
    midi.addMidiMessage(juce::MidiMessage::noteOn(1, 60, 0.8f), 0.0, {});
    midi.addMidiMessage(juce::MidiMessage::noteOff(1, 60), 0.01, {});
    std::vector<te::AutomatableParameter::ModifierAssignment::Ptr> routes;
    for (auto a : gain.getAssignments())
        routes.push_back(a);
    // A first pass outside the count (what's made once, on the first block, may be).
    const auto runFor = [&](int blocks, int startBlock)
    {
        for (int b = startBlock; b < startBlock + blocks; ++b)
        {
            const auto t = te::TimePosition::fromSeconds(b * block / kSampleRate);
            f.edit->updateModifierTimers(t, block);
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < block; ++i)
                    buffer.setSample(c, i, 0.4f * std::sin(static_cast<float>(b * block + i) * 0.03f));
            te::PluginRenderContext rc(&buffer, 0, block, &midi, 0.0,
                                       {t, te::TimeDuration::fromSeconds(block / kSampleRate)}, b % 100 < 80,
                                       false, false, false);
            for (auto* m : all)
                m->applyToBuffer(rc);
            for (auto* m : all)
                for (const auto& a : routes)
                    (void)m->getValueFor(*a, t);
            (void)gain.getCurrentValue();
        }
    };
    runFor(4, 0);
    const auto before = fd::testkit::allocationsDuringProcess();
    {
        const fd::testkit::CountAllocations count;
        runFor(300, 4); // playing, stopping and starting again; the loop's laps
        runFor(50, 0);
    }
    CHECK(fd::testkit::allocationsDuringProcess() == before);
}

TEST_CASE("modulators: the Envelope fires on the beat and on notes", "[integration][modulators]")
{
    SECTION("every beat")
    {
        Fixture f;
        auto utility = f.module("utility");
        Fixture::set(*utility, "gain", -40.0f);
        auto& env = f.modulator(Kind::Envelope);
        Fixture::set(env, Envelope::Trigger, 1.0f); // Beat
        Fixture::set(env, Envelope::Every, 10.0f);  // 1/4 (Every's choices start at 1/64)
        Fixture::set(env, Envelope::Attack, 0.0f);
        Fixture::set(env, Envelope::Decay, 100.0f);
        Fixture::set(env, Envelope::Release, 50.0f);
        Fixture::route(env, Fixture::param(*utility, "gain"), 0.5f);
        const auto out = f.render();
        // A pulse on each of the four beats (0, 0.5, 1, 1.5 s at 120 BPM).
        int pulses = 0;
        bool high = false;
        for (int i = 0; i < out.numSamples(); ++i)
        {
            const bool up = gainDbAt(out, i) > -30.0;
            if (up && !high)
                ++pulses;
            high = up;
        }
        CHECK(pulses == 4);
    }
    SECTION("on notes, played where it sits")
    {
        Fixture f;
        auto clip =
            f.track().insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(2.0)}, nullptr);
        REQUIRE(clip != nullptr);
        clip->getSequence().addNote(60, te::BeatPosition::fromBeats(2.0), te::BeatDuration::fromBeats(1.0),
                                    100, 0,
                                    nullptr); // at 1 s, for 0.5 s
        auto utility = f.module("utility");
        Fixture::set(*utility, "gain", -40.0f);
        auto& env = f.modulator(Kind::Envelope, utility.get());
        Fixture::set(env, Envelope::Attack, 0.0f);
        Fixture::set(env, Envelope::Sustain, 100.0f);
        Fixture::set(env, Envelope::Release, 1.0f);
        Fixture::route(env, Fixture::param(*utility, "gain"), 0.5f);
        const auto out = f.render();
        CHECK(gainDbAt(out, static_cast<int>(kSampleRate * 0.9)) < -35.0); // before the note
        CHECK(gainDbAt(out, static_cast<int>(kSampleRate * 1.2)) > -20.0); // while it's held
        CHECK(gainDbAt(out, static_cast<int>(kSampleRate * 1.8)) < -35.0); // after it's released
    }
}

TEST_CASE("modulators: Steps step through their pattern in time", "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    Fixture::set(*utility, "gain", -12.0f);
    auto& steps = f.modulator(Kind::Steps);
    Fixture::set(steps, Steps::Length, 4.0f);
    Fixture::set(steps, Steps::Sync, 11.0f); // a step each 1/4: 0.5 s at 120 BPM
    for (int i = 0; i < 4; ++i)
        steps.setStep(i, i % 2 == 0 ? 1.0f : -1.0f, 1.0f, nullptr);
    Fixture::route(steps, Fixture::param(*utility, "gain"), 0.2f);
    const auto out = f.render();
    const double up = gainDbAt(out, static_cast<int>(kSampleRate * 0.25));
    const double down = gainDbAt(out, static_cast<int>(kSampleRate * 0.75));
    const double upAgain = gainDbAt(out, static_cast<int>(kSampleRate * 1.25));
    INFO(up << " " << down << " " << upAgain);
    CHECK(up > down + 6.0);
    CHECK(upAgain == Approx(up).margin(0.1));
}

TEST_CASE("modulators: one moves another (Random on an LFO's Rate)", "[integration][modulators]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto& lfo = f.modulator(Kind::Lfo);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.2f);
    const auto steady = f.render();
    auto& random = f.modulator(Kind::Random);
    Fixture::set(random, Random::Rate, 3.0f);
    Fixture::route(random, lfo.parameter(Lfo::Rate), 0.5f);
    const auto wobbled = f.render();
    INFO("moved against steady: " << nullResidualDb(wobbled, steady) << " dB");
    CHECK(nullResidualDb(wobbled, steady) > -30.0);
}
