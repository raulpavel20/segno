// Plugin adapter tests: the JUCE AudioProcessor wrapper around a device (plugins/src).
// Linked against the Utility plugin's shared code; the adapter itself is device-agnostic.

#include "Compressor.h"
#include "DeviceAudioProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

    // Collapsing a section narrows the editor (asynchronously) and stores the view.
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner(processor->createEditor());
    auto* editor = dynamic_cast<fd::plugin::DeviceEditor*>(editorOwner.get());
    REQUIRE(editor != nullptr);
    const int wide = editor->getWidth();
    CHECK(editor->getHeight() == fd::ui::Theme::current().devicePanelHeight + 16);
    auto& panel = editor->panel();
    REQUIRE_FALSE(panel.sections().empty());
    panel.sections().front()->setCollapsed(true, true);
    editor->handleUpdateNowIfNeeded(); // the deferred resize
    CHECK(editor->getWidth() == wide - (panel.sections().front()->getWidth() == 24
                                            ? fd::ui::Theme::current().deviceGrid.sectionWidth(panel.sections().front()->columns()) - 24
                                            : 0));
    CHECK(editor->getWidth() < wide);
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
