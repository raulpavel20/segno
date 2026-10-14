// Racks, step 0 (docs/design/racks.md §12): what tracktion's racks do with our modules, checked
// before racks are built on them. Chains side by side summed in time whatever their latency; a
// macro's mapping heard, From to To along its curve; grouping undone; a rack's state kept whole;
// notes reaching an instrument in a rack.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>

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
    te::Engine engine{"fd-rack-spike", std::make_unique<TestUIBehaviour>(), fd::engine::makeEngineBehaviour()};
    juce::TemporaryFile inputFile{".wav"};
    AudioBuffer input = whiteNoise(kLength, 9, 0.25f, 2);
    std::unique_ptr<te::Edit> edit;

    Fixture()
    {
        fd::engine::registerDevices(engine);
        REQUIRE(writeWav(inputFile.getFile().getFullPathName().toStdString(), input, kSampleRate));
        edit = te::Edit::createSingleTrackEdit(engine, te::Edit::EditRole::forRendering);
        track().insertWaveClip("input", inputFile.getFile(),
                               {{te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)}, {}}, false);
    }

    te::AudioTrack& track() { return *te::getAudioTracks(*edit)[0]; }

    te::Plugin::Ptr module(const std::string& id)
    {
        auto p = edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor(id), {});
        REQUIRE(p != nullptr);
        return p;
    }

    static void set(te::Plugin& p, const std::string& param, float plain)
    {
        auto& d = dynamic_cast<fd::engine::DevicePlugin&>(p);
        const auto i = *d.device().params().indexOf(param);
        d.parameter(i).setParameter(d.device().info().params[i].toNormalized(plain), juce::sendNotificationSync);
    }

    /// A rack of chains side by side: each chain's modules in order, then a volume and pan of its
    /// own; every chain from the rack's input, all summed at its output (racks.md §12).
    te::RackType::Ptr rack(const std::vector<std::vector<te::Plugin::Ptr>>& chains)
    {
        auto r = edit->getRackList().addNewRack();
        REQUIRE(r != nullptr);
        r->rackName = "Rack";
        for (const auto& chain : chains)
        {
            std::vector<te::Plugin::Ptr> all = chain;
            auto mixer = edit->getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {});
            REQUIRE(mixer != nullptr);
            all.push_back(mixer);
            for (size_t i = 0; i < all.size(); ++i)
                REQUIRE(r->addPlugin(all[i], {0.2f + 0.6f * static_cast<float>(i) / static_cast<float>(all.size()), 0.5f},
                                     false));
            // MIDI and audio (pins 1 and 2) from the input, module to module, to the output.
            r->addConnection({}, 0, all.front()->itemID, 0);
            for (int pin = 1; pin <= 2; ++pin)
                r->addConnection({}, pin, all.front()->itemID, pin);
            for (size_t i = 0; i + 1 < all.size(); ++i)
                for (int pin = 0; pin <= 2; ++pin)
                    r->addConnection(all[i]->itemID, pin, all[i + 1]->itemID, pin);
            for (int pin = 1; pin <= 2; ++pin)
                r->addConnection(all.back()->itemID, pin, {}, pin);
        }
        return r;
    }

    te::Plugin::Ptr place(te::RackType& r)
    {
        auto instance = edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(r));
        REQUIRE(instance != nullptr);
        track().pluginList.insertPlugin(instance, 0, nullptr);
        return instance;
    }

    AudioBuffer render()
    {
        juce::TemporaryFile out(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters p(*edit);
        p.destFile = out.getFile();
        p.audioFormat = &wav;
        p.bitDepth = 32;
        p.sampleRateForAudio = kSampleRate;
        p.blockSizeForAudio = 512;
        p.time = {te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)};
        p.tracksToDo = te::toBitSet(te::getAllTracks(*edit));
        p.canRenderInMono = false;
        p.checkNodesForAudio = false;
        const auto file = te::Renderer::renderToFile("rack spike", p);
        REQUIRE(file.existsAsFile());
        auto read = readWav(file.getFullPathName().toStdString());
        REQUIRE(read.has_value());
        return read->buffer;
    }
};

AudioBuffer scaled(const AudioBuffer& b, float gain)
{
    auto out = b;
    for (int c = 0; c < out.numChannels(); ++c)
        for (int i = 0; i < out.numSamples(); ++i)
            out.at(c, i) *= gain;
    return out;
}
} // namespace

TEST_CASE("racks (spike): a delayed chain beside a dry one sums in time", "[integration]")
{
    // The EQ in linear phase, flat: the input, later by its latency. Beside an empty chain, the
    // rack must line them up: twice the input, and no comb filtering.
    Fixture f;
    auto eq = f.module("eq");
    Fixture::set(*eq, "phase", 1.0f); // Linear phase
    auto r = f.rack({{eq}, {}});
    f.place(*r);
    CHECK(eq->getLatencySeconds() * kSampleRate > 1000.0); // the EQ really is late
    // tracktion lines the chains up inside the rack's graph (the rack instance itself reports no
    // latency): exactly twice the input, in time with the song.
    const auto out = f.render();
    const auto expected = scaled(f.input, 2.0f);
    INFO("residual " << nullResidualDb(out, expected) << " dB");
    CHECK(nullResidualDb(out, expected) < -100.0);
}

TEST_CASE("racks (spike): a macro moves our module's parameter in the sound, From to To along its curve",
          "[integration]")
{
    Fixture f;
    auto utility = f.module("utility");
    auto r = f.rack({{utility}});
    f.place(*r);
    auto& device = dynamic_cast<fd::engine::DevicePlugin&>(*utility);
    const auto index = *device.device().params().indexOf("gain");
    const auto& spec = device.device().info().params[index];
    auto& gain = device.parameter(index);

    // From -24 dB to 0 dB: the parameter's own value is From, the assignment's value To - From.
    auto* macro = r->getMacroParameterListForWriting().createMacroParameter();
    REQUIRE(macro != nullptr);
    const float from = spec.toNormalized(-24.0f), to = spec.toNormalized(0.0f);
    gain.setParameter(from, juce::sendNotificationSync);
    auto assignment = gain.addModifier(*macro, to - from, 0.0f, 0.0f); // curve 0: straight
    REQUIRE(assignment != nullptr);

    const auto levelAt = [&](float position) {
        macro->setParameter(position, juce::sendNotificationSync);
        return rmsDb(f.render()) - rmsDb(f.input);
    };
    CHECK(levelAt(0.0f) == Approx(-24.0).margin(0.2));
    CHECK(levelAt(1.0f) == Approx(0.0).margin(0.2));
    const double straight = levelAt(0.5f);
    INFO("halfway, straight: " << straight << " dB");
    CHECK(straight == Approx(spec.fromNormalized(from + 0.5f * (to - from))).margin(0.3));

    // A bent curve lands elsewhere at half way, either side of straight.
    assignment->curve = 0.4f;
    const double bentOne = levelAt(0.5f);
    assignment->curve = -0.4f;
    const double bentOther = levelAt(0.5f);
    INFO("halfway, bent: " << bentOne << " and " << bentOther << " dB");
    CHECK(std::abs(bentOne - straight) > 1.0);
    CHECK(std::abs(bentOther - straight) > 1.0);
    CHECK((bentOne - straight) * (bentOther - straight) < 0.0);
}

TEST_CASE("racks (spike): grouping modules into a rack is undone whole", "[integration]")
{
    Fixture f;
    auto& um = f.edit->getUndoManager();
    auto saturator = f.module("saturator");
    auto utility = f.module("utility");
    f.track().pluginList.insertPlugin(saturator, 0, nullptr);
    f.track().pluginList.insertPlugin(utility, 1, nullptr);
    Fixture::set(*saturator, "drive", 18.0f);
    const auto before = f.render();
    const auto modulesBefore = f.track().pluginList.size();

    // Group: out of the track's chain, into a rack placed where they were; one undo step.
    um.beginNewTransaction("Group into rack");
    saturator->removeFromParent();
    utility->removeFromParent();
    auto r = f.rack({{saturator, utility}});
    f.place(*r);
    CHECK(f.edit->getRackList().size() == 1);
    const auto grouped = f.render();
    INFO("grouped against before: " << nullResidualDb(grouped, before) << " dB");
    CHECK(nullResidualDb(grouped, before) < -100.0); // the same sound, in a rack

    um.beginNewTransaction();
    REQUIRE(um.undo());
    CHECK(f.edit->getRackList().size() == 0);
    CHECK(f.track().pluginList.size() == modulesBefore);
    const auto undone = f.render();
    INFO("undone against before: " << nullResidualDb(undone, before) << " dB");
    CHECK(nullResidualDb(undone, before) < -100.0);
}

TEST_CASE("racks (spike): a rack's state, written and read back, keeps its modules' settings", "[integration]")
{
    Fixture f;
    auto saturator = f.module("saturator");
    Fixture::set(*saturator, "drive", 24.0f);
    auto r = f.rack({{saturator}, {}});
    auto* macro = r->getMacroParameterListForWriting().createMacroParameter();
    macro->setParameter(0.7f, juce::sendNotificationSync);
    f.place(*r);
    const auto first = f.render();

    // As a rack file would hold it: the rack's state, into a fresh project.
    const auto state = r->createStateCopy(false);
    const auto xml = state.toXmlString();
    Fixture g;
    auto copy = g.edit->getRackList().addRackTypeFrom(juce::ValueTree::fromXml(xml));
    REQUIRE(copy != nullptr);
    CHECK(copy->getPlugins().size() == 3); // the Saturator and the two chains' mixers
    REQUIRE(copy->getMacroParameters().size() == 1);
    CHECK(copy->getMacroParameters()[0]->getCurrentValue() == Approx(0.7f));
    g.place(*copy);
    const auto second = g.render();
    INFO("copy against original: " << nullResidualDb(second, first) << " dB");
    CHECK(nullResidualDb(second, first) < -100.0);
}

TEST_CASE("racks (spike): notes reach an instrument in a rack", "[integration]")
{
    Fixture f;
    f.track().getClips()[0]->removeFromParent();
    auto clip = f.track().insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(2.0)}, nullptr);
    REQUIRE(clip != nullptr);
    clip->getSequence().addNote(60, te::BeatPosition::fromBeats(0.0), te::BeatDuration::fromBeats(2.0), 100, 0, nullptr);
    auto synth = f.module("subtractive-synth");
    auto r = f.rack({{synth}});
    f.place(*r);
    const auto out = f.render();
    CHECK(rmsDb(out) > -40.0);
}

#if defined(FD_UTILITY_VST3_PATH)
TEST_CASE("racks (spike): a hosted plugin in a rack keeps its settings when the rack is written and read back",
          "[integration]")
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
    auto r = f.rack({{external}});
    f.place(*r);
    external->initialiseFully();
    const auto& spec = fd::findDevice("utility")->info().params[0]; // gain
    int set = 0;
    for (auto* p : external->getAutomatableParameters())
        if (p->getParameterName() == juce::String(std::string(spec.name)))
        {
            p->setParameter(spec.toNormalized(-12.0f), juce::sendNotificationSync);
            ++set;
        }
    REQUIRE(set == 1);
    external->flushPluginStateToValueTree();
    const auto first = f.render();
    CHECK(rmsDb(first) - rmsDb(f.input) == Approx(-12.0).margin(0.2));

    Fixture g;
    g.engine.getPluginManager().knownPluginList.addType(*found[0]);
    r->flushStateToValueTree();
    auto copy = g.edit->getRackList().addRackTypeFrom(juce::ValueTree::fromXml(r->createStateCopy(false).toXmlString()));
    REQUIRE(copy != nullptr);
    g.place(*copy);
    for (auto* p : copy->getPlugins())
        p->initialiseFully();
    const auto second = g.render();
    INFO("copy against original: " << nullResidualDb(second, first) << " dB");
    CHECK(nullResidualDb(second, first) < -100.0);
}
#endif
