// Cross-host null tests (ADR 0007, M0 step 9). A device must sound identical wherever it runs:
//   1. natively inside tracktion_engine (the DAW adapter) vs. the offline device render;
//   2. our own VST3 build hosted in tracktion_engine vs. the native device.
// Residual must be below -120 dBFS.

#include "engine/DevicePlugin.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>

#if defined(FD_HAVE_TEST_PATHS)
#include "fd_test_paths.h"
#endif

namespace te = tracktion;
using namespace fd::testkit;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kLength = 96000; // 2 s

/// Runs render tasks synchronously (the default UIBehaviour does nothing) and records warnings.
struct TestUIBehaviour final : te::UIBehaviour
{
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }
    }
    void showWarningMessage(const juce::String& message) override { warnings.add(message); }
    juce::StringArray warnings;
};

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestUIBehaviour* ui = new TestUIBehaviour();
    te::Engine engine{"fd-integration-tests", std::unique_ptr<te::UIBehaviour>(ui), nullptr};
    juce::TemporaryFile inputFile{".wav"};
    AudioBuffer input = whiteNoise(kLength, 9, 0.25f, 2);

    Fixture()
    {
        fd::engine::registerDevices(engine);
        REQUIRE(writeWav(inputFile.getFile().getFullPathName().toStdString(), input, kSampleRate));
    }

    std::unique_ptr<te::Edit> makeEdit()
    {
        auto edit = te::Edit::createSingleTrackEdit(engine, te::Edit::EditRole::forRendering);
        auto* track = te::getAudioTracks(*edit)[0];
        track->insertWaveClip("input", inputFile.getFile(),
                              {{te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)}, {}}, false);
        return edit;
    }

    AudioBuffer render(te::Edit& edit)
    {
        juce::TemporaryFile out(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters p(edit);
        p.destFile = out.getFile();
        p.audioFormat = &wav;
        p.bitDepth = 32;
        p.sampleRateForAudio = kSampleRate;
        p.blockSizeForAudio = 512;
        p.time = {te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)};
        p.tracksToDo = te::toBitSet(te::getAllTracks(edit));
        p.canRenderInMono = false;
        p.checkNodesForAudio = false;
        const auto file = te::Renderer::renderToFile("null test", p);
        INFO("engine warnings: " << ui->warnings.joinIntoString("; "));
        REQUIRE(file.existsAsFile());
        auto wav32 = readWav(file.getFullPathName().toStdString());
        REQUIRE(wav32.has_value());
        return wav32->buffer;
    }
};

/// Settings that exercise every Utility stage.
const std::vector<std::pair<const char*, float>> kSettings{
    {"gain", -6.0f}, {"balance", 0.3f}, {"width", 150.0f}, {"invert.r", 1.0f}, {"dc", 1.0f}};

void applyTo(fd::Device& device)
{
    for (auto [id, value] : kSettings)
        device.params().set(*device.params().indexOf(id), value);
}
} // namespace

TEST_CASE("tracktion's own signal path is transparent", "[integration]")
{
    Fixture f;
    auto edit = f.makeEdit();
    const auto out = f.render(*edit);
    INFO("residual " << nullResidualDb(out, f.input) << " dBFS");
    CHECK(nullResidualDb(out, f.input) < -120.0);
}

TEST_CASE("native device in tracktion matches the offline device render", "[integration]")
{
    Fixture f;
    auto edit = f.makeEdit();
    auto plugin = edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor("utility"), {});
    REQUIRE(plugin != nullptr);
    te::getAudioTracks(*edit)[0]->pluginList.insertPlugin(plugin, 0, nullptr);

    auto& native = dynamic_cast<fd::engine::DevicePlugin&>(*plugin);
    for (auto [id, value] : kSettings)
        native.parameter(*native.device().params().indexOf(id)).setParameter(value, juce::sendNotificationSync);
    const auto inEngine = f.render(*edit);

    auto device = fd::findDevice("utility")->create();
    applyTo(*device);
    const auto offline = render(*device, f.input, {.sampleRate = kSampleRate});

    INFO("residual " << nullResidualDb(inEngine, offline) << " dBFS");
    CHECK(peakDb(offline) > -40.0); // the settings really did something audible
    CHECK(nullResidualDb(inEngine, offline) < -120.0);
}

TEST_CASE("native instrument in tracktion matches the offline render of the same notes", "[integration]")
{
    Fixture f;
    auto edit = te::Edit::createSingleTrackEdit(f.engine, te::Edit::EditRole::forRendering);
    auto* track = te::getAudioTracks(*edit)[0];
    auto clip = track->insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)}, nullptr);
    REQUIRE(clip != nullptr);

    // 120 bpm: one beat = 24000 samples. Overlapping notes, a repeated key and a chord.
    struct Note { int key; double start, length; int velocity; };
    const std::vector<Note> notes{{48, 0.0, 1.0, 100}, {55, 0.5, 1.5, 80}, {60, 1.0, 0.25, 127},
                                  {60, 1.5, 0.5, 64}, {64, 2.0, 1.0, 90}, {67, 2.0, 1.0, 90}};
    std::vector<TimedEvent> events;
    for (const auto& n : notes)
    {
        clip->getSequence().addNote(n.key, te::BeatPosition::fromBeats(n.start), te::BeatDuration::fromBeats(n.length),
                                    n.velocity, 0, nullptr);
        const auto vel = juce::MidiMessage::noteOn(1, n.key, static_cast<juce::uint8>(n.velocity)).getFloatVelocity();
        events.push_back({static_cast<std::int64_t>(n.start * 24000.0),
                          {.type = fd::EventType::NoteOn, .key = static_cast<std::uint8_t>(n.key), .value = vel}});
        // tracktion plays note-offs 0.1 ms early so they sort before same-time note-ons
        // (MidiNote::getPlaybackTime); the reference does the same.
        const double offSeconds = (n.start + n.length) * 0.5 - 0.0001;
        events.push_back({juce::roundToInt(offSeconds * kSampleRate),
                          {.type = fd::EventType::NoteOff, .key = static_cast<std::uint8_t>(n.key)}});
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        // Note-offs first at equal times, as a sequencer sends them.
        return a.samplePosition != b.samplePosition ? a.samplePosition < b.samplePosition
                                                    : a.event.type == fd::EventType::NoteOff && b.event.type != fd::EventType::NoteOff;
    });

    auto plugin = edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor("subtractive-synth"), {});
    REQUIRE(plugin != nullptr);
    track->pluginList.insertPlugin(plugin, 0, nullptr);
    const auto inEngine = f.render(*edit);

    auto device = fd::findDevice("subtractive-synth")->create();
    RenderOptions o;
    o.sampleRate = kSampleRate;
    o.events = events;
    const auto offline = render(*device, silence(kLength, 1), o);

    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        writeWav(std::string(dump) + "/engine.wav", inEngine, kSampleRate);
        writeWav(std::string(dump) + "/offline.wav", offline, kSampleRate);
    }
    INFO("residual " << nullResidualDb(inEngine, offline) << " dBFS; engine peak " << peakDb(inEngine) << " dBFS");
    CHECK(peakDb(offline) > -30.0);
    CHECK(nullResidualDb(inEngine, offline) < -120.0);
}

TEST_CASE("sample loads in the DAW go through undo and match the offline render", "[integration]")
{
    Fixture f;
    juce::TemporaryFile sampleFile(".wav");
    REQUIRE(writeWav(sampleFile.getFile().getFullPathName().toStdString(), sine(220.0, kSampleRate, 48000, 0.5f, 1),
                     kSampleRate));
    const nlohmann::json extra{{"sample.path", sampleFile.getFile().getFullPathName().toStdString()}};

    auto edit = te::Edit::createSingleTrackEdit(f.engine, te::Edit::EditRole::forRendering);
    auto* track = te::getAudioTracks(*edit)[0];
    auto clip = track->insertMIDIClip({te::TimePosition(), te::TimeDuration::fromSeconds(kLength / kSampleRate)}, nullptr);
    clip->getSequence().addNote(60, te::BeatPosition::fromBeats(0.5), te::BeatDuration::fromBeats(1.0), 100, 0, nullptr);
    clip->getSequence().addNote(67, te::BeatPosition::fromBeats(1.0), te::BeatDuration::fromBeats(2.0), 90, 0, nullptr);

    auto plugin = edit->getPluginCache().createNewPlugin(fd::engine::DevicePlugin::typeNameFor("sampler"), {});
    REQUIRE(plugin != nullptr);
    track->pluginList.insertPlugin(plugin, 0, nullptr);
    auto& native = dynamic_cast<fd::engine::DevicePlugin&>(*plugin);

    // A UI drop: recorded with the Edit's undo manager (ADR 0017).
    auto& undo = edit->getUndoManager();
    undo.beginNewTransaction();
    fd::engine::DevicePluginBinding(native).applyExtraState(extra);
    auto loaded = [&] {
        nlohmann::json state = nlohmann::json::object();
        native.device().writeExtraState(state);
        return state.contains("sample.path");
    };
    CHECK(loaded());
    undo.undo();
    CHECK_FALSE(loaded());
    undo.redo();
    REQUIRE(loaded());

    const auto inEngine = f.render(*edit);

    auto device = fd::findDevice("sampler")->create();
    device->readExtraState(extra);
    RenderOptions o;
    o.sampleRate = kSampleRate;
    const auto vel = [](int v) { return juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(v)).getFloatVelocity(); };
    auto off = [](double beats) { return juce::roundToInt((beats * 0.5 - 0.0001) * kSampleRate); }; // tracktion's nudge
    o.events = {{12000, {.type = fd::EventType::NoteOn, .key = 60, .value = vel(100)}},
                {24000, {.type = fd::EventType::NoteOn, .key = 67, .value = vel(90)}},
                {off(1.5), {.type = fd::EventType::NoteOff, .key = 60}},
                {off(3.0), {.type = fd::EventType::NoteOff, .key = 67}}};
    const auto offline = render(*device, silence(kLength, 1), o);

    INFO("residual " << nullResidualDb(inEngine, offline) << " dBFS");
    CHECK(peakDb(offline) > -20.0);
    CHECK(nullResidualDb(inEngine, offline) < -120.0);
}

#if defined(FD_UTILITY_VST3_PATH)
TEST_CASE("our VST3 hosted in tracktion matches the native device", "[integration]")
{
    Fixture f;

    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(found, FD_UTILITY_VST3_PATH);
    REQUIRE(found.size() == 1);
    f.engine.getPluginManager().knownPluginList.addType(*found[0]); // as a plugin scan would

    auto edit = f.makeEdit();
    auto external = edit->getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *found[0]);
    REQUIRE(external != nullptr);
    te::getAudioTracks(*edit)[0]->pluginList.insertPlugin(external, 0, nullptr);
    external->initialiseFully(); // external plugins build their parameter list lazily

    // Hosted VST3 parameters are normalised (0..1) and identified by name (VST3 hashes string IDs).
    const auto& info = fd::findDevice("utility")->info();
    int matched = 0;
    for (auto [id, value] : kSettings)
    {
        const auto& spec = info.params[*fd::ParamStore(info.params).indexOf(id)];
        for (auto* p : external->getAutomatableParameters())
            if (p->getParameterName() == juce::String(std::string(spec.name)))
            {
                p->setParameter(spec.toNormalized(value), juce::sendNotificationSync);
                ++matched;
            }
    }
    INFO("hosted parameters: " << external->getAutomatableParameters().size()
                                << "; engine warnings: " << f.ui->warnings.joinIntoString("; "));
    REQUIRE(matched == static_cast<int>(kSettings.size()));
    const auto viaVst3 = f.render(*edit);

    auto device = fd::findDevice("utility")->create();
    applyTo(*device);
    const auto offline = render(*device, f.input, {.sampleRate = kSampleRate});

    INFO("residual " << nullResidualDb(viaVst3, offline) << " dBFS");
    CHECK(nullResidualDb(viaVst3, offline) < -120.0);
}
#endif
