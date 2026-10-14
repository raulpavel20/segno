// Plugin adapter tests: the JUCE AudioProcessor wrapper around a device (plugins/src).
// Linked against the Utility plugin's shared code; the adapter itself is device-agnostic.

#include "Compressor.h"
#include "DeviceAudioProcessor.h"
#include "Utility.h"
#include "WavetableSynth.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <random>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
std::unique_ptr<juce::AudioProcessor> makePlugin()
{
    return std::unique_ptr<juce::AudioProcessor>(createPluginFilter());
}
} // namespace

TEST_CASE("plugin state round trip restores every parameter", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto a = makePlugin();
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto* p : a->getParameters())
    {
        // Discrete parameters only take their legal steps (JUCE's bool parameter stores raw values).
        const float v = dist(rng);
        p->setValue(p->isDiscrete() ? std::round(v * static_cast<float>(p->getNumSteps() - 1)) /
                                          static_cast<float>(p->getNumSteps() - 1)
                                    : v);
    }

    juce::MemoryBlock state;
    a->getStateInformation(state);
    REQUIRE(state.getSize() > 0);

    auto b = makePlugin();
    b->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    const auto pa = a->getParameters();
    const auto pb = b->getParameters();
    REQUIRE(pa.size() == pb.size());
    for (int i = 0; i < pa.size(); ++i)
    {
        INFO(pa[i]->getName(64) << ": " << pa[i]->getCurrentValueAsText() << " vs " << pb[i]->getCurrentValueAsText());
        CHECK(pb[i]->getValue() == Catch::Approx(pa[i]->getValue()).margin(1e-6));
    }

    juce::MemoryBlock again;
    b->getStateInformation(again);
    CHECK(again == state);
}

TEST_CASE("plugin parameters use the device's normalised mapping and text", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto p = makePlugin();
    auto* gain = dynamic_cast<juce::RangedAudioParameter*>(p->getParameters()[0]);
    REQUIRE(gain != nullptr);
    CHECK(gain->getName(32) == "Gain");
    CHECK(gain->getValue() == Catch::Approx(0.5f).margin(1e-5)); // 0 dB sits at the centre
    CHECK(gain->getText(0.5f, 32) == "0.0 dB");
    CHECK(gain->getText(0.0f, 32) == "-inf dB");
    CHECK(gain->getValueForText("-6 dB") == Catch::Approx(gain->convertTo0to1(-6.0f)));
}

TEST_CASE("plugin state keeps the editor's view state beside the device state", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto a = makePlugin();
    auto* processor = dynamic_cast<fd::plugin::DeviceAudioProcessor*>(a.get());
    REQUIRE(processor != nullptr);

    // With every section expanded, the state is just the device state.
    juce::MemoryBlock plain;
    a->getStateInformation(plain);
    CHECK_FALSE(plain.toString().contains("\"view\""));

    // Collapsing a section narrows the editor (asynchronously) to the panel, never below what its
    // header needs, and stores the view.
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner(processor->createEditor());
    auto* editor = dynamic_cast<fd::plugin::DeviceEditor*>(editorOwner.get());
    REQUIRE(editor != nullptr);
    const int wide = editor->getWidth();
    CHECK(editor->getHeight() == fd::ui::Theme::current().devicePanelHeight + 16);
    auto& panel = editor->panel();
    REQUIRE_FALSE(panel.sections().empty());
    panel.sections().front()->setCollapsed(true, true);
    editor->handleUpdateNowIfNeeded(); // the deferred resize
    CHECK(editor->getWidth() == panel.preferredWidth() + 16);
    CHECK(panel.preferredWidth() >= panel.headerMinimumWidth());
    CHECK(editor->getWidth() <= wide);
    REQUIRE_FALSE(processor->viewState().empty());

    juce::MemoryBlock withView;
    a->getStateInformation(withView);
    CHECK(withView.toString().contains("\"view\""));

    // Another instance takes both; saving again gives the same bytes.
    auto b = makePlugin();
    auto* other = dynamic_cast<fd::plugin::DeviceAudioProcessor*>(b.get());
    b->setStateInformation(withView.getData(), static_cast<int>(withView.getSize()));
    CHECK(other->viewState() == processor->viewState());
    juce::MemoryBlock again;
    b->getStateInformation(again);
    CHECK(again == withView);

    // An editor opened later starts collapsed; an open one follows a loaded state.
    {
        std::unique_ptr<juce::AudioProcessorEditor> later(other->createEditor());
        auto* e = dynamic_cast<fd::plugin::DeviceEditor*>(later.get());
        CHECK(e->panel().sections().front()->isCollapsed());
        CHECK(e->getWidth() == editor->getWidth());
        b->setStateInformation(plain.getData(), static_cast<int>(plain.getSize()));
        other->viewStateLoaded.dispatchPendingMessages(); // the asynchronous notice
        CHECK_FALSE(e->panel().sections().front()->isCollapsed());
        CHECK(e->getWidth() == wide);
    }
    editorOwner.reset(); // editors go before their processors
}

TEST_CASE("plugin reports a new latency when a latency-changing setting moves", "[plugin]")
{
    // ADR 0020: the Compressor's Lookahead sets its latency; the host hears of each change.
    juce::ScopedJuceInitialiser_GUI juce;
    fd::plugin::DeviceAudioProcessor p(fd::devices::compressorFactory(), nullptr);
    struct Listener final : juce::AudioProcessorListener
    {
        int latencyChanges = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
        {
            latencyChanges += details.latencyChanged ? 1 : 0;
        }
    } listener;
    p.addListener(&listener);
    p.prepareToPlay(48000.0, 512);
    CHECK(p.getLatencySamples() == 0);

    auto& lookahead = p.parameter(fd::devices::Compressor::Lookahead);
    CHECK_FALSE(lookahead.isAutomatable());
    lookahead.setValueNotifyingHost(1.0f); // "10 ms"
    p.updateLatency();
    CHECK(p.getLatencySamples() == 480);
    CHECK(listener.latencyChanges == 1);

    p.updateLatency(); // unchanged: no further notice
    CHECK(listener.latencyChanges == 1);

    // Text from the CLAP wrapper arrives as Latin-1 bytes; "\u221E:1" must still read as infinity.
    auto& ratio = p.parameter(fd::devices::Compressor::Ratio);
    CHECK(ratio.getText(1.0f, 16) == juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E:1")));
    juce::String latin1;
    for (const char b : std::string("\xE2\x88\x9E:1"))
        latin1 += juce::String::charToString(static_cast<juce::juce_wchar>(static_cast<unsigned char>(b)));
    CHECK(ratio.getValueForText(latin1) == Catch::Approx(1.0f));
    CHECK(ratio.getValueForText(ratio.getText(1.0f, 16)) == Catch::Approx(1.0f));

    lookahead.setValueNotifyingHost(0.5f); // "1 ms"
    p.updateLatency();
    CHECK(p.getLatencySamples() == 48);
    CHECK(listener.latencyChanges == 2);
    p.removeListener(&listener);
}

TEST_CASE("plugin sidechain: Off or an inactive input changes nothing, External listens (ADR 0025)", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    using C = fd::devices::Compressor;
    // The compressor's output level (dB RMS) on a steady tone, with a silent sidechain key.
    auto run = [](bool inputActive, bool external) {
        fd::plugin::DeviceAudioProcessor p(fd::devices::compressorFactory(), nullptr);
        auto layout = p.getBusesLayout();
        REQUIRE(layout.inputBuses.size() == 2);
        layout.inputBuses.getReference(1) =
            inputActive ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        REQUIRE(p.setBusesLayout(layout));
        p.setSidechainExternal(external);
        const auto& specs = p.device().info().params;
        for (const auto& [param, value] : {std::pair{C::Threshold, -30.0f}, std::pair{C::Ratio, 8.0f}})
            p.parameter(param).setValueNotifyingHost(specs[param].toNormalized(value));
        p.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), 512);
        juce::MidiBuffer midi;
        double sum = 0.0;
        int phase = 0;
        for (int block = 0; block < 100; ++block)
        {
            buffer.clear(); // the key (any channels past the main two) stays silent
            for (int i = 0; i < 512; ++i, ++phase)
                for (int c = 0; c < 2; ++c)
                    buffer.setSample(c, i, 0.25f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(phase) / 48000.0f));
            p.processBlock(buffer, midi);
            if (block >= 50)
                for (int i = 0; i < 512; ++i)
                    sum += double(buffer.getSample(0, i)) * buffer.getSample(0, i);
        }
        return 10.0 * std::log10(sum / (50 * 512));
    };
    const double plain = run(false, false); // compresses its own input
    CHECK(run(true, false) == Catch::Approx(plain).margin(1e-9)); // Off: the host's input is ignored
    CHECK(run(false, true) == Catch::Approx(plain).margin(1e-9)); // External, but the host routes nothing
    CHECK(run(true, true) > plain + 10.0);                         // External: it hears the silent key

    // The choice is plugin state, beside the device state, and only when External.
    fd::plugin::DeviceAudioProcessor a(fd::devices::compressorFactory(), nullptr);
    fd::plugin::DeviceAudioProcessor b(fd::devices::compressorFactory(), nullptr);
    juce::MemoryBlock plainState, externalState;
    a.getStateInformation(plainState);
    CHECK_FALSE(plainState.toString().contains("sidechain"));
    a.setSidechainExternal(true);
    a.getStateInformation(externalState);
    b.setStateInformation(externalState.getData(), static_cast<int>(externalState.getSize()));
    CHECK(b.sidechainExternal());
    b.setStateInformation(plainState.getData(), static_cast<int>(plainState.getSize()));
    CHECK_FALSE(b.sidechainExternal());
}

TEST_CASE(
    "plugin presets: the program list, a load through the host's parameters, the memo kept in the state "
    "(ADR 0044)",
    "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto a = makePlugin();
    auto& processor = dynamic_cast<fd::plugin::DeviceAudioProcessor&>(*a);
    const auto& info = processor.device().info();
    auto& host = processor.presets();

    // The host's program list: Default settings, then the factory presets.
    REQUIRE(a->getNumPrograms() >= 1);
    CHECK(a->getProgramName(0) == "Default settings");
    CHECK(a->getCurrentProgram() == 0);

    // A preset of yours, as the DAW writes one (the same format and folder).
    auto state = host.state();
    state.params["gain"] = -6.0f;
    const auto name = "Plugin test " + juce::Uuid().toString().toStdString();
    const auto saved = host.library().save(info, state, name, {"Test"}, {}, {}, true);
    REQUIRE(saved.preset != nullptr);
    const auto key = saved.preset->key();

    // A load sets each parameter as a gesture, so hosts record it like a hand on the control.
    struct Gestures final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begun; }
        void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ended; }
        int begun = 0, ended = 0;
    } gestures;
    a->addListener(&gestures);
    fd::PresetController c(info, host);
    REQUIRE(c.load(*host.library().find(info, key)));
    a->removeListener(&gestures);
    CHECK(gestures.begun >= 1);
    CHECK(gestures.begun == gestures.ended);
    CHECK(processor.parameter(0).getValue() ==
          Catch::Approx(info.params[0].toNormalized(-6.0f)).margin(1e-6));
    CHECK(c.loadedName() == name);

    // Kept with the plugin's state, beside the device state.
    juce::MemoryBlock block;
    a->getStateInformation(block);
    auto b = makePlugin();
    b->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    auto& other = dynamic_cast<fd::plugin::DeviceAudioProcessor&>(*b);
    CHECK(fd::PresetController(info, other.presets()).loadedName() == name);

    // Escape after browsing puts back what was there (plugins have no undo of their own).
    host.beginBrowsing();
    c.load(*host.library().find(info, ""));
    CHECK(processor.parameter(0).getValue() ==
          Catch::Approx(info.params[0].toNormalized(info.params[0].def)).margin(1e-6));
    host.endBrowsing(false);
    CHECK(processor.parameter(0).getValue() ==
          Catch::Approx(info.params[0].toNormalized(-6.0f)).margin(1e-6));
    CHECK(c.loadedName() == name);

    // A program the host sets again (after a state load) changes nothing: one of yours isn't a
    // program, so the host sees the first, and setting it again keeps yours.
    CHECK(a->getCurrentProgram() == 0);
    a->setCurrentProgram(0);
    CHECK(c.loadedName() == name);

    REQUIRE(host.library().remove(info, *host.library().find(info, key),
                                  [](const std::filesystem::path& f) { return std::filesystem::remove(f); }));
}

namespace
{
/// A module editor of a stub module (ADR 0053): two panes, its Preview a held note.
class StubEditor final : public fd::ui::ModuleEditor
{
public:
    explicit StubEditor(fd::ui::DeviceUiContext& context) : context_(context)
    {
        setMinimumRows(6);
        addPane("One", 4);
        addPane({}, 0, 4);
        preview_.setSize(fd::ui::Toggle::widthFor(preview_.getButtonText()), 20);
        preview_.onClick = [this] { context_.playNote(60, preview_.getToggleState() ? 0.8f : 0.0f); };
        addCommand(preview_);
    }
    juce::String tabName() const override { return "Stub"; }
    juce::String title() const override { return "Something to edit"; }
    fd::ui::Toggle preview_{"Preview"};

private:
    fd::ui::DeviceUiContext& context_;
};

class StubPanel final : public fd::ui::DevicePanel
{
public:
    explicit StubPanel(fd::ui::DeviceUiContext& context) : DevicePanel(context)
    {
        addSection("main", "Main", 2);
    }
    bool performCommand(std::string_view id) override
    {
        if (id != "edit" || !context().showEditor)
            return false;
        context().showEditor(std::make_unique<StubEditor>(context()));
        return true;
    }
};

const fd::ui::PanelFactory& stubPanel()
{
    static const fd::ui::PanelFactory factory{
        .deviceId = "utility",
        .create = [](fd::ui::DeviceUiContext& ctx) -> std::unique_ptr<fd::ui::DevicePanel>
        { return std::make_unique<StubPanel>(ctx); }};
    return factory;
}
} // namespace

TEST_CASE("plugin: a module's editor opens under the panel, the window grown for it, Done shrinks it back "
          "(ADR 0053)",
          "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    fd::plugin::DeviceAudioProcessor processor(fd::devices::utilityFactory(), &stubPanel());
    std::unique_ptr<juce::AudioProcessorEditor> owner(processor.createEditor());
    auto* editor = dynamic_cast<fd::plugin::DeviceEditor*>(owner.get());
    REQUIRE(editor != nullptr);
    const int height = editor->getHeight(), width = editor->getWidth();

    REQUIRE(editor->panel().performCommand("edit"));
    editor->handleUpdateNowIfNeeded(); // the deferred resize
    auto* module = editor->moduleEditor();
    REQUIRE(module != nullptr);
    const auto& t = fd::ui::Theme::current();
    CHECK(editor->getHeight() == height + t.shell.panelHeaderHeight + module->minimumHeight() + 8);
    CHECK(editor->getWidth() == std::max(width, module->minimumWidth() + 16));
    // Below the panel, the header above it, nothing touching.
    CHECK(module->getBottom() == editor->getHeight() - 8);
    CHECK(module->getHeight() == module->minimumHeight());
    CHECK(editor->editorHeader().getBottom() == module->getY());
    CHECK(editor->editorHeader().getY() >= t.devicePanelHeight + 8);
    // Its commands in its header, Done last.
    const auto commands = module->commands();
    REQUIRE(commands.size() == 2);
    CHECK(commands[0]->getParentComponent() == &editor->editorHeader());
    CHECK(commands[1]->getParentComponent() == &editor->editorHeader());
    CHECK(commands[0]->getRight() < commands[1]->getX());

    // Done: closed, the window back to the panel.
    REQUIRE(module->onDone);
    module->onDone(); // what Done's click does
    editor->handleUpdateNowIfNeeded();
    CHECK(editor->moduleEditor() == nullptr);
    CHECK(editor->getHeight() == height);
    CHECK(editor->getWidth() == width);
    owner.reset();
}

TEST_CASE("plugin: an editor's note reaches the device at the next block, beside the host's MIDI", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    fd::plugin::DeviceAudioProcessor processor(fd::devices::wavetableFactory(), nullptr);
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    const auto loudness = [&]
    {
        float peak = 0.0f;
        for (int i = 0; i < 20; ++i)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            peak = std::max(peak, buffer.getMagnitude(0, 0, 256));
        }
        return peak;
    };
    CHECK(loudness() == 0.0f);
    processor.playNote(48, 0.8f); // Preview's held note
    CHECK(loudness() > 0.01f);
    processor.playNote(48, 0.0f);
    for (int i = 0; i < 400; ++i) // its release
        processor.processBlock(buffer, midi);
    CHECK(loudness() < 1e-4f);
    // A full queue drops what doesn't fit rather than wait.
    for (int i = 0; i < 100; ++i)
        processor.playNote(48, i % 2 == 0 ? 0.5f : 0.0f);
    loudness();
    processor.releaseResources();
}
