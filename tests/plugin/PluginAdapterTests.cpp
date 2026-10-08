// Plugin adapter tests: the JUCE AudioProcessor wrapper around a device (plugins/src).
// Linked against the Utility plugin's shared code; the adapter itself is device-agnostic.

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
