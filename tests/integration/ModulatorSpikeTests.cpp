// Modulators, step 0 (docs/design/modulators.md §17, §19): what tracktion's modifiers do with
// modulators of our own type, checked before modulators are built on them. A modifier of Segno's
// own type (the custom-modifiers patch) moves our module, a plugin's parameter, a rack's macro and
// the track's volume; gives each target its own copy; is undone and redone, saved and read back;
// repeats a roll and changes with another; hears the sound where it sits in the chain; hears
// notes before and after the instrument; keeps working when moved into a rack. And how finely a
// fast modulator reaches our modules and a plugin.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>

#include <atomic>
#include <cmath>

#if FD_HAVE_TEST_PATHS
#include "fd_test_paths.h"
#endif

namespace te = tracktion;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kLength = 96000; // 2 s
const juce::Identifier kSpikeType{"FDSPIKEMODULATOR"};

/// A value from -1 to 1 that depends only on its inputs (SplitMix64, as the notes' chance).
float hashed(std::uint64_t a, std::uint64_t b, std::uint64_t c)
{
    std::uint64_t z = a * 0x9E3779B97F4A7C15ull + b * 0xBF58476D1CE4E5B9ull + c * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(static_cast<double>(z >> 11) / static_cast<double>(1ull << 53)) * 2.0f - 1.0f;
}

/// One modulator of our own type, enough to try what step 0 asks. Its kind: an LFO (a sine, a
/// pure function of the song's time), Random (a new value every 1/rate s from a hash of its roll,
/// gliding to it, also a function of time), a Follower (the level where it sits, measured in the
/// graph) or Notes (counts the note-ons it hears).
class SpikeModulator final : public te::Modifier, private juce::ValueTree::Listener
{
public:
    enum Kind
    {
        Lfo = 0,
        Random = 1,
        Follower = 2,
        Notes = 3
    };

    SpikeModulator(te::Edit& e, const juce::ValueTree& v) : te::Modifier(e, v)
    {
        auto* um = &edit.getUndoManager();
        rate.referTo(state, "fdRate", um, 2.0f);
        rateParam = new te::AutomatableParameter("rate", "Rate", *this, {0.01f, 50.0f});
        addAutomatableParameter(rateParam);
        rateParam->attachToCurrentValue(rate);
        state.addListener(this);
        refresh();
    }

    ~SpikeModulator() override
    {
        state.removeListener(this);
        notifyListenersOfDeletion();
        if (timer_ != nullptr)
            edit.removeModifierTimer(*timer_);
        for (auto* p : getAutomatableParameters())
            p->detachFromCurrentValue();
        deleteAutomatableParameters();
    }

    using te::Modifier::initialise;
    void initialise() override
    {
        timer_ = std::make_unique<Timer>(*this);
        edit.addModifierTimer(*timer_);
        restoreChangedParametersFromState();
    }

    juce::String getName() const override { return "Spike modulator"; }
    juce::String getSelectableDescription() override { return getName(); }

    struct Assignment final : te::AutomatableParameter::ModifierAssignment
    {
        Assignment(const juce::ValueTree& v, const SpikeModulator& m)
            : te::AutomatableParameter::ModifierAssignment(m.edit, v), modulator(m.itemID)
        {
            lane.referTo(state, "fdLane", nullptr, 0);
        }
        bool isForModifierSource(const ModifierSource& s) const override
        {
            const auto* m = dynamic_cast<const SpikeModulator*>(&s);
            return m != nullptr && m->itemID == modulator;
        }
        const te::EditItemID modulator;
        juce::CachedValue<int> lane;
    };

    te::AutomatableParameter::ModifierAssignment* createAssignment(const juce::ValueTree& v) override
    {
        return new Assignment(v, *this);
    }

    ProcessingPosition getProcessingPosition() override { return ProcessingPosition::beforePlugin; }
    te::EditItemID getPluginToProcessBefore() override { return te::EditItemID::fromVar(state["fdBefore"]); }
    juce::StringArray getAudioInputNames() override { return {"Left", "Right"}; }
    juce::StringArray getMidiInputNames() override { return {"MIDI"}; }

    float getCurrentValue() override { return valueAt(0, now_.load()); }

    float getValueFor(const te::AutomatableParameter::ModifierAssignment& a, te::TimePosition t) override
    {
        const auto* mine = dynamic_cast<const Assignment*>(&a);
        const int lane = mine != nullptr ? mine->lane.get() : 0;
        // Exact to the time a parameter asks for, or (as tracktion's own) its block's start.
        return valueAt(lane, exact_.load() ? t.inSeconds() : now_.load());
    }

    void applyToBuffer(const te::PluginRenderContext& rc) override
    {
        setEditTime(rc.editTime.getStart());
        if (kind_ == Follower && rc.destBuffer != nullptr)
        {
            float peak = 0.0f;
            for (int c = 0; c < rc.destBuffer->getNumChannels(); ++c)
                peak = std::max(peak, rc.destBuffer->getMagnitude(c, rc.bufferStartSample, rc.bufferNumSamples));
            level_.store(peak);
            float seen = loudest.load();
            while (peak > seen && !loudest.compare_exchange_weak(seen, peak))
            {
            }
        }
        if (kind_ == Notes && rc.bufferForMidiMessages != nullptr)
            for (auto& m : *rc.bufferForMidiMessages)
                if (m.isNoteOn())
                    ++notesHeard;
    }

    // What the tests read and set.
    std::atomic<float> loudest{0.0f};
    std::atomic<int> notesHeard{0};
    void setExact(bool exact) { exact_.store(exact); }

private:
    struct Timer final : te::ModifierTimer
    {
        explicit Timer(SpikeModulator& m) : owner(m) {}
        void updateStreamTime(te::TimePosition editTime, int) override
        {
            owner.updateParameterStreams(editTime);
            owner.now_.store(editTime.inSeconds());
        }
        SpikeModulator& owner;
    };

    float valueAt(int lane, double seconds) const
    {
        const float hz = rateParam->getCurrentValue();
        switch (kind_.load())
        {
        case Lfo:
        {
            const double spread = perTarget_ ? static_cast<double>(lane) / std::max(1, lanes_.load()) : 0.0;
            return static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * (hz * seconds + spread)));
        }
        case Random:
        {
            const double x = hz * seconds;
            const double n = std::floor(x);
            const auto copy = static_cast<std::uint64_t>(perTarget_ ? lane : 0);
            const auto step = static_cast<std::uint64_t>(static_cast<std::int64_t>(n));
            const auto seed = static_cast<std::uint64_t>(roll_.load()) * 1000003ull + itemID.getRawID();
            const float from = hashed(seed, copy, step), to = hashed(seed, copy, step + 1);
            return from + (to - from) * static_cast<float>(x - n);
        }
        case Follower:
            return level_.load();
        default:
            return 0.0f;
        }
    }

    void refresh()
    {
        kind_.store(static_cast<int>(state.getProperty("fdKind", 0)));
        perTarget_.store(static_cast<bool>(state.getProperty("fdPerTarget", true)));
        lanes_.store(static_cast<int>(state.getProperty("fdLanes", 1)));
        roll_.store(static_cast<int>(state.getProperty("fdRoll", 1)));
    }
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { refresh(); }

    juce::CachedValue<float> rate;
    te::AutomatableParameter::Ptr rateParam;
    std::unique_ptr<Timer> timer_;
    std::atomic<int> kind_{0}, lanes_{1}, roll_{1};
    std::atomic<bool> perTarget_{true}, exact_{true};
    std::atomic<double> now_{0.0};
    std::atomic<float> level_{0.0f};
};

/// A plugin that applies its gain as it finds it each time it's called, unsmoothed: what a
/// third-party plugin that doesn't smooth its parameters would play.
class ProbePlugin final : public te::Plugin
{
public:
    static constexpr const char* xmlTypeName = "fdSpikeProbe";

    explicit ProbePlugin(te::PluginCreationInfo info) : te::Plugin(info)
    {
        gainValue.referTo(state, "gain", getUndoManager(), 0.5f);
        gain = addParam("gain", "Gain", {0.0f, 1.0f});
        gain->attachToCurrentValue(gainValue);
    }

    ~ProbePlugin() override
    {
        notifyListenersOfDeletion();
        gain->detachFromCurrentValue();
    }

    juce::String getName() const override { return "Probe"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }

    void applyToBuffer(const te::PluginRenderContext& rc) override
    {
        if (rc.destBuffer == nullptr)
            return;
        const float g = gain->getCurrentValue();
        for (int c = 0; c < rc.destBuffer->getNumChannels(); ++c)
            rc.destBuffer->applyGain(c, rc.bufferStartSample, rc.bufferNumSamples, g);
    }

    juce::CachedValue<float> gainValue;
    te::AutomatableParameter::Ptr gain;
};

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
    te::Engine engine{"fd-modulator-spike", std::make_unique<TestUIBehaviour>(), fd::engine::makeEngineBehaviour()};
    juce::TemporaryFile inputFile{".wav"};
    AudioBuffer input = dc(kLength, 0.5f); // the output over 0.5 is the gain, sample by sample
    std::unique_ptr<te::Edit> edit;

    Fixture()
    {
        te::ModifierList::isCustomType = [](const juce::Identifier& t) { return t == kSpikeType; };
        te::ModifierList::createCustom = [](te::Edit& e, const juce::ValueTree& v) -> te::Modifier* {
            return new SpikeModulator(e, v);
        };
        fd::engine::registerDevices(engine);
        engine.getPluginManager().createBuiltInType<ProbePlugin>();
        REQUIRE(writeWav(inputFile.getFile().getFullPathName().toStdString(), input, kSampleRate));
        edit = te::Edit::createSingleTrackEdit(engine, te::Edit::EditRole::forRendering);
        track().insertWaveClip("input", inputFile.getFile(),
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
        d.parameter(i).setParameter(d.device().info().params[i].toNormalized(plain), juce::sendNotificationSync);
    }

    /// A modulator of the kind, at the end of the list (the track's by default), processed just
    /// before `before` in the chain (after the chain without one).
    SpikeModulator& modulator(SpikeModulator::Kind kind, te::Plugin* before = nullptr, te::ModifierList* list = nullptr)
    {
        juce::ValueTree v(kSpikeType);
        v.setProperty("fdKind", static_cast<int>(kind), nullptr);
        if (before != nullptr)
            v.setProperty("fdBefore", before->itemID.toVar(), nullptr);
        auto& l = list != nullptr ? *list : *track().getModifierList();
        auto m = l.insertModifier(v, -1, nullptr);
        REQUIRE(m != nullptr);
        return dynamic_cast<SpikeModulator&>(*m);
    }

    /// A route: the modulator moves `p` around where it's set, by `amount` of its range either
    /// way (tracktion's assignment value, curve straight), as copy `lane`.
    static te::AutomatableParameter::ModifierAssignment::Ptr route(SpikeModulator& m, te::AutomatableParameter& p,
                                                                   float amount, int lane = 0)
    {
        auto a = p.addModifier(m, amount, 0.0f, 0.0f);
        REQUIRE(a != nullptr);
        a->state.setProperty("fdLane", lane, nullptr);
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
        const auto file = te::Renderer::renderToFile("modulator spike", p);
        REQUIRE(file.existsAsFile());
        auto read = readWav(file.getFullPathName().toStdString());
        REQUIRE(read.has_value());
        return read->buffer;
    }
};

/// The gain, in dB, sample by sample: the output over the DC input's 0.5.
std::vector<double> gainDb(const AudioBuffer& out)
{
    std::vector<double> db(static_cast<size_t>(out.numSamples()));
    for (int i = 0; i < out.numSamples(); ++i)
        db[static_cast<size_t>(i)] = 20.0 * std::log10(std::max(1.0e-9, std::abs(static_cast<double>(out.at(0, i))) / 0.5));
    return db;
}

/// How far the gain swings (max − min, dB) after the first 50 ms, and how often it crosses its
/// middle (twice a cycle).
struct Swing
{
    double rangeDb = 0.0;
    int crossings = 0;
};

Swing swingOf(const AudioBuffer& out)
{
    const auto db = gainDb(out);
    const size_t from = static_cast<size_t>(kSampleRate * 0.05);
    const auto [lo, hi] = std::minmax_element(db.begin() + static_cast<std::ptrdiff_t>(from), db.end());
    const double middle = 0.5 * (*lo + *hi);
    Swing s{*hi - *lo, 0};
    for (size_t i = from + 1; i < db.size(); ++i)
        if ((db[i - 1] - middle) * (db[i] - middle) < 0.0)
            ++s.crossings;
    return s;
}
} // namespace

TEST_CASE("modulators (spike): an LFO of our own type moves our module's gain", "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    Fixture::set(*utility, "gain", -12.0f);
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.1f);
    const auto swing = swingOf(f.render());
    INFO("swing " << swing.rangeDb << " dB, " << swing.crossings << " crossings");
    CHECK(swing.rangeDb > 6.0);
    // 2 Hz over 1.95 s: about 8 crossings of the middle.
    CHECK(swing.crossings >= 7);
    CHECK(swing.crossings <= 9);
}

TEST_CASE("modulators (spike): it moves the track's volume and a rack's macro", "[integration]")
{
    SECTION("the track's volume")
    {
        Fixture f;
        auto* volume = f.track().getVolumePlugin();
        REQUIRE(volume != nullptr);
        auto& lfo = f.modulator(SpikeModulator::Lfo);
        Fixture::route(lfo, *volume->volParam, 0.1f);
        const auto swing = swingOf(f.render());
        INFO("swing " << swing.rangeDb << " dB");
        CHECK(swing.rangeDb > 3.0);
        CHECK(swing.crossings >= 7);
    }
    SECTION("a rack's macro (Morph is one)")
    {
        Fixture f;
        auto utility = f.edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor("utility"), {});
        auto r = f.edit->getRackList().addNewRack();
        REQUIRE(r->addPlugin(utility, {0.5f, 0.5f}, false));
        r->addConnection({}, 0, utility->itemID, 0);
        for (int pin = 1; pin <= 2; ++pin)
        {
            r->addConnection({}, pin, utility->itemID, pin);
            r->addConnection(utility->itemID, pin, {}, pin);
        }
        f.track().pluginList.insertPlugin(f.edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(*r)), 0,
                                          nullptr);
        // The macro moves the gain from -24 to 0 dB; the LFO moves the macro around its middle.
        auto* macro = r->getMacroParameterListForWriting().createMacroParameter();
        auto& gain = Fixture::param(*utility, "gain");
        const auto& spec = fd::findDevice("utility")->info().params[0];
        gain.setParameter(spec.toNormalized(-24.0f), juce::sendNotificationSync);
        gain.addModifier(*macro, spec.toNormalized(0.0f) - spec.toNormalized(-24.0f), 0.0f, 0.0f);
        macro->setParameter(0.5f, juce::sendNotificationSync);
        auto& lfo = f.modulator(SpikeModulator::Lfo);
        Fixture::route(lfo, *macro, 0.4f);
        const auto swing = swingOf(f.render());
        INFO("swing " << swing.rangeDb << " dB");
        CHECK(swing.rangeDb > 10.0);
        CHECK(swing.crossings >= 7);
    }
}

TEST_CASE("modulators (spike): each target its own copy, or all together", "[integration]")
{
    Fixture f;
    auto a = f.module("utility", 0);
    auto b = f.module("utility", 1);
    auto& random = f.modulator(SpikeModulator::Random);
    auto first = Fixture::route(random, Fixture::param(*a, "gain"), 0.1f, 0);
    auto second = Fixture::route(random, Fixture::param(*b, "gain"), 0.1f, 1);

    const auto differ = [&] {
        int different = 0;
        for (int i = 0; i < 20; ++i)
        {
            const auto t = te::TimePosition::fromSeconds(0.07 * i);
            if (std::abs(random.getValueFor(*first, t) - random.getValueFor(*second, t)) > 1.0e-4f)
                ++different;
        }
        return different;
    };
    CHECK(differ() >= 18); // Per Target on: two paths

    random.state.setProperty("fdPerTarget", false, nullptr);
    CHECK(differ() == 0); // off: one path for both

    // The LFO spreads its copies over a cycle: two of them half a cycle apart.
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    lfo.state.setProperty("fdLanes", 2, nullptr);
    auto lfoA = Fixture::route(lfo, Fixture::param(*a, "width"), 0.1f, 0);
    auto lfoB = Fixture::route(lfo, Fixture::param(*b, "width"), 0.1f, 1);
    for (double s : {0.03, 0.11, 0.4})
    {
        const auto t = te::TimePosition::fromSeconds(s);
        CHECK(lfo.getValueFor(*lfoA, t) == Approx(-lfo.getValueFor(*lfoB, t)).margin(1.0e-5));
    }
}

TEST_CASE("modulators (spike): adding one and its route is undone and redone whole", "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    f.edit->getUndoManager().beginNewTransaction("Add modulator");
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.1f);
    f.edit->getUndoManager().beginNewTransaction();
    CHECK(swingOf(f.render()).rangeDb > 6.0);

    REQUIRE(f.edit->getUndoManager().undo());
    CHECK(f.track().getModifierList()->getModifiers().isEmpty());
    CHECK(swingOf(f.render()).rangeDb < 0.01);

    REQUIRE(f.edit->getUndoManager().redo());
    REQUIRE(f.track().getModifierList()->getModifiers().size() == 1);
    const auto swing = swingOf(f.render());
    INFO("after redo: " << swing.rangeDb << " dB");
    CHECK(swing.rangeDb > 6.0);
    CHECK(swing.crossings >= 7);
}

TEST_CASE("modulators (spike): a project read back plays its modulators the same", "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto& random = f.modulator(SpikeModulator::Random);
    Fixture::route(random, Fixture::param(*utility, "gain"), 0.1f);
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    Fixture::route(lfo, *f.track().getVolumePlugin()->volParam, 0.05f);
    const auto first = f.render();

    const auto xml = f.edit->state.toXmlString();
    te::Edit::Options options{f.engine, juce::ValueTree::fromXml(xml), te::ProjectItemID::createNewID(te::ProjectID{})};
    options.role = te::Edit::forRendering;
    auto copy = te::Edit::createEdit(std::move(options));
    REQUIRE(copy != nullptr);
    auto& track = *te::getAudioTracks(*copy)[0];
    REQUIRE(track.getModifierList()->getModifiers().size() == 2);
    const auto second = f.render(copy.get());
    INFO("read back against the original: " << nullResidualDb(second, first) << " dB");
    CHECK(nullResidualDb(second, first) < -100.0);
}

TEST_CASE("modulators (spike): a roll plays the same every time, another roll otherwise", "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto& random = f.modulator(SpikeModulator::Random);
    Fixture::route(random, Fixture::param(*utility, "gain"), 0.1f);
    random.state.setProperty("fdRoll", 7, nullptr);
    const auto first = f.render();
    const auto again = f.render();
    INFO("the same roll again: " << nullResidualDb(again, first) << " dB");
    CHECK(nullResidualDb(again, first) < -100.0); // the null the rack spike asks of a render
    random.state.setProperty("fdRoll", 8, nullptr);
    const auto other = f.render();
    INFO("another roll against the first: " << nullResidualDb(other, first) << " dB");
    CHECK(nullResidualDb(other, first) > -20.0);
}

TEST_CASE("modulators (spike): a Follower hears the sound where it sits in the chain", "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    Fixture::set(*utility, "gain", -20.0f);
    auto& before = f.modulator(SpikeModulator::Follower, utility.get()); // before the Utility
    auto& after = f.modulator(SpikeModulator::Follower);                 // after the chain
    f.render();
    INFO("before " << before.loudest.load() << ", after " << after.loudest.load());
    CHECK(before.loudest.load() == Approx(0.5f).margin(0.01f));
    CHECK(after.loudest.load() == Approx(0.05f).margin(0.005f));
}

TEST_CASE("modulators (spike): notes reach a modulator before and after the instrument", "[integration]")
{
    Fixture f;
    f.track().getClips()[0]->removeFromParent();
    auto clip = f.track().insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(2.0)}, nullptr);
    REQUIRE(clip != nullptr);
    for (int n = 0; n < 4; ++n)
        clip->getSequence().addNote(60 + n, te::BeatPosition::fromBeats(n * 0.5), te::BeatDuration::fromBeats(0.25), 100,
                                    0, nullptr);
    auto synth = f.module("subtractive-synth");
    auto& beforeSynth = f.modulator(SpikeModulator::Notes, synth.get());
    auto& afterChain = f.modulator(SpikeModulator::Notes);
    f.render();
    CHECK(beforeSynth.notesHeard.load() == 4);
    // Recorded, not required: whether notes still flow after an instrument.
    WARN("notes heard after the instrument: " << afterChain.notesHeard.load());
}

TEST_CASE("modulators (spike): moved into a rack, a modulator still moves what's in it", "[integration]")
{
    Fixture f;
    auto utility = f.edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor("utility"), {});
    auto r = f.edit->getRackList().addNewRack();
    REQUIRE(r->addPlugin(utility, {0.5f, 0.5f}, false));
    r->addConnection({}, 0, utility->itemID, 0);
    for (int pin = 1; pin <= 2; ++pin)
    {
        r->addConnection({}, pin, utility->itemID, pin);
        r->addConnection(utility->itemID, pin, {}, pin);
    }
    f.track().pluginList.insertPlugin(f.edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(*r)), 0,
                                      nullptr);
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.1f);
    CHECK(swingOf(f.render()).rangeDb > 6.0);

    // Grouped: its state moves into the rack's modifiers; tracktion keeps the same object.
    auto moved = r->getModifierList().insertModifier(lfo.state, -1, nullptr);
    CHECK(moved.get() == &lfo);
    CHECK(f.track().getModifierList()->getModifiers().isEmpty());
    const auto swing = swingOf(f.render());
    INFO("in the rack: " << swing.rangeDb << " dB, " << swing.crossings << " crossings");
    CHECK(swing.rangeDb > 6.0);
    CHECK(swing.crossings >= 7);
}

namespace
{
/// The largest change of gain from one sample to the next (dB), and the longest the gain stays
/// exactly put (samples), after the first 50 ms.
struct Steps
{
    double largestDb = 0.0;
    int longestHold = 0;
};

Steps stepsOf(const AudioBuffer& out)
{
    const auto db = gainDb(out);
    Steps s;
    int hold = 0;
    for (size_t i = static_cast<size_t>(kSampleRate * 0.05) + 1; i < db.size(); ++i)
    {
        s.largestDb = std::max(s.largestDb, std::abs(db[i] - db[i - 1]));
        hold = out.at(0, static_cast<int>(i)) == out.at(0, static_cast<int>(i - 1)) ? hold + 1 : 0;
        s.longestHold = std::max(s.longestHold, hold + 1);
    }
    return s;
}
} // namespace

TEST_CASE("modulators (spike): how finely a 20 Hz LFO reaches our module (measured)", "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    Fixture::set(*utility, "gain", -12.0f);
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    lfo.state.setProperty("fdRate", 20.0f, nullptr);
    Fixture::route(lfo, Fixture::param(*utility, "gain"), 0.1f);
    const auto exact = stepsOf(f.render());
    lfo.setExact(false);
    const auto blockStart = stepsOf(f.render());
    WARN("our Utility, 20 Hz: exact to the time: largest step " << exact.largestDb << " dB, held "
                                                                << exact.longestHold << " samples; at block starts: "
                                                                << blockStart.largestDb << " dB, held "
                                                                << blockStart.longestHold << " samples");
    CHECK(exact.largestDb < 0.2); // our modules ramp their parameters
}

#if defined(FD_UTILITY_VST3_PATH)
TEST_CASE("modulators (spike): it moves a hosted plugin's parameter", "[integration]")
{
    // Our Utility's VST3 stands in for a third-party plugin.
    Fixture f;
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(found, FD_UTILITY_VST3_PATH);
    REQUIRE(found.size() == 1);
    f.engine.getPluginManager().knownPluginList.addType(*found[0]);
    auto external = f.edit->getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *found[0]);
    REQUIRE(external != nullptr);
    f.track().pluginList.insertPlugin(external, 0, nullptr);
    external->initialiseFully();
    const auto& spec = fd::findDevice("utility")->info().params[0]; // gain
    te::AutomatableParameter* gain = nullptr;
    for (auto* p : external->getAutomatableParameters())
        if (p->getParameterName() == juce::String(std::string(spec.name)))
            gain = p;
    REQUIRE(gain != nullptr);
    gain->setParameter(spec.toNormalized(-12.0f), juce::sendNotificationSync);
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    Fixture::route(lfo, *gain, 0.1f);
    const auto swing = swingOf(f.render());
    INFO("swing " << swing.rangeDb << " dB, " << swing.crossings << " crossings");
    CHECK(swing.rangeDb > 6.0);
    CHECK(swing.crossings >= 7);
}
#endif

TEST_CASE("modulators (spike): how finely a 20 Hz LFO reaches a plugin that doesn't smooth (measured)",
          "[integration]")
{
    Fixture f;
    auto probe = f.edit->getPluginCache().createNewPlugin(ProbePlugin::xmlTypeName, {});
    REQUIRE(probe != nullptr);
    f.track().pluginList.insertPlugin(probe, 0, nullptr);
    auto& lfo = f.modulator(SpikeModulator::Lfo);
    lfo.state.setProperty("fdRate", 20.0f, nullptr);
    Fixture::route(lfo, *dynamic_cast<ProbePlugin&>(*probe).gain, 0.1f);
    const auto exact = stepsOf(f.render());
    lfo.setExact(false);
    const auto blockStart = stepsOf(f.render());
    WARN("a plugin that doesn't smooth, 20 Hz, 512-sample blocks: exact to the time: largest step "
         << exact.largestDb << " dB, held " << exact.longestHold << " samples; at block starts: "
         << blockStart.largestDb << " dB, held " << blockStart.longestHold << " samples");
    // tracktion splits a modulated plugin's block into 128-sample pieces; exact values reach
    // each piece, four times as finely as the block's start alone.
    CHECK(exact.longestHold <= 129);
    CHECK(blockStart.longestHold >= 500);
    CHECK(exact.largestDb < blockStart.largestDb / 2.0);
}
