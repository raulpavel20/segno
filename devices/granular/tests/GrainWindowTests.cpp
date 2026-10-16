// Granular's DSP pieces (devices/granular/SPEC.md, "DSP design"), JUCE-free.
#include "GrainWindow.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("granular: a grain's window starts and ends at zero, peaks at one", "[fast]")
{
    using fd::devices::granular::GrainWindow;
    using fd::devices::granular::Window;
    for (const auto shape : {Window::Hann, Window::Gaussian, Window::Triangle, Window::SharpAttack,
                             Window::SharpEnd, Window::Square, Window::Drawn})
        for (const float skew : {-1.0f, 0.0f, 0.7f})
            for (const float flat : {0.0f, 0.5f})
            {
                GrainWindow w{.shape = shape, .skew = skew, .flatness = flat};
                INFO(static_cast<int>(shape) << " skew " << skew << " flat " << flat);
                CHECK(w.at(0.0f) < 1e-3f);
                CHECK(w.at(1.0f) < 1e-3f);
                float peak = 0.0f;
                for (int i = 0; i <= 1000; ++i)
                    peak = std::max(peak, w.at(static_cast<float>(i) / 1000.0f));
                CHECK(peak > 0.95f);
                CHECK(peak <= 1.0f + 1e-4f);
            }
}
