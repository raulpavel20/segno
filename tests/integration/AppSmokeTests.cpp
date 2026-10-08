// Smoke test: the DAW's main component builds offscreen with a native device chain and a loaded
// clip, without starting playback.

#include "ui/MainComponent.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("DAW main component builds with a native Utility and the demo loop", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-integration-tests"};
        fd::engine::registerDevices(engine);
        fd::app::MainComponent main(engine);
        main.setSize(900, 420);
        const auto image = main.createComponentSnapshot(main.getLocalBounds());
        CHECK(image.isValid());
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
