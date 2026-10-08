#include "fd/dsp/Fft.h"
#include "fd/dsp/Math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace fd::dsp;
using Catch::Approx;

TEST_CASE("FFT finds a bin-centred sine at the right level", "[fast]")
{
    Fft fft(12);
    const int n = fft.size();
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(twoPi * 100.0 * i / n));
    std::vector<float> mags(static_cast<size_t>(n / 2 + 1));
    std::vector<std::complex<float>> scratch;
    fft.magnitudes(x.data(), mags.data(), scratch);
    const auto peak = std::distance(mags.begin(), std::max_element(mags.begin(), mags.end()));
    CHECK(peak == 100);
    CHECK(mags[100] == Approx(0.5f).margin(0.01f));
}
