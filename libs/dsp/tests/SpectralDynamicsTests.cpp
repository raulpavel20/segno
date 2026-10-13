// A spectral band's detector (EQ stage 2): what sticks out comes down, the rest stays.
#include "fd/dsp/SpectralDynamics.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using namespace fd::dsp;

namespace
{
constexpr double sr = 48000.0;
constexpr int fftSize = 8192;
constexpr int bins = fftSize / 2 + 1;

double hz(int k)
{
    return k * sr / fftSize;
}

int binOf(double f)
{
    return static_cast<int>(std::lround(f * fftSize / sr));
}

/// A full mix's slope (3 dB/oct down: flat once tilted), `levelDb` at 1 kHz, with a resonance of
/// `bumpDb` at `bumpHz` (about a tenth of an octave wide).
std::vector<float> spectrum(double levelDb, double bumpDb = 0.0, double bumpHz = 3000.0)
{
    std::vector<float> p(bins);
    for (int k = 0; k < bins; ++k)
    {
        const double f = std::max(hz(1), hz(k));
        const double octaves = std::log2(f / bumpHz);
        const double db =
            levelDb - 3.0 * std::log2(f / 1000.0) + bumpDb * std::exp(-0.5 * std::pow(octaves * 24.0, 2.0));
        p[static_cast<size_t>(k)] = static_cast<float>(std::pow(10.0, db / 10.0));
    }
    return p;
}

struct Detector
{
    SpectralDynamics d;
    SpectralDynamics::Scratch scratch;
    SpectralDynamics::Settings s{.sampleRate = sr, .fftSize = fftSize, .frameSeconds = 1024.0 / sr};
    std::vector<float> weight = std::vector<float>(bins, 1.0f), gain = std::vector<float>(bins, 0.0f);

    Detector()
    {
        d.prepare(bins);
        scratch.prepare(bins);
    }
    /// `frames` frames of the same spectrum; the gains after them.
    const std::vector<float>& run(const std::vector<float>& power, int frames)
    {
        for (int i = 0; i < frames; ++i)
            d.process(power.data(), weight.data(), bins, s, scratch, gain.data());
        return gain;
    }
    [[nodiscard]] float at(double f) const { return gain[static_cast<size_t>(binOf(f))]; }
};
} // namespace

TEST_CASE("spectral dynamics: a resonance comes down to its surroundings, an octave away nothing moves",
          "[fast]")
{
    Detector d;
    d.s.rangeDb = -18.0f;
    d.run(spectrum(-20.0, 12.0), 100);
    CHECK(d.at(3000.0) < -9.0f); // 12 dB over: down to within about 2 of its surroundings
    CHECK(d.at(3000.0) > -12.0f);
    CHECK(std::abs(d.at(1500.0)) < 0.1f);
    CHECK(std::abs(d.at(6000.0)) < 0.1f);
}

TEST_CASE("spectral dynamics: a smooth spectrum, or one outside the band, is left alone", "[fast]")
{
    Detector flat;
    flat.s.rangeDb = -18.0f;
    const auto& g = flat.run(spectrum(-20.0), 50);
    CHECK(*std::min_element(g.begin() + 1, g.end()) > -0.05f);

    Detector outside;
    outside.s.rangeDb = -18.0f;
    std::fill(outside.weight.begin(), outside.weight.end(), 0.0f);
    const auto& o = outside.run(spectrum(-20.0, 12.0), 50);
    CHECK(*std::min_element(o.begin(), o.end()) == 0.0f);
}

TEST_CASE("spectral dynamics: Detail sets how wide a build-up it finds", "[fast]")
{
    // A broad build-up (about two thirds of an octave): low Detail compares it with a wide
    // surrounding and finds it; high Detail sees mostly the build-up itself.
    auto broad = spectrum(-20.0);
    for (int k = 1; k < bins; ++k)
        broad[static_cast<size_t>(k)] *= static_cast<float>(
            std::pow(10.0, 0.8 * std::exp(-0.5 * std::pow(std::log2(hz(k) / 2000.0) * 4.0, 2.0))));
    Detector low, high;
    low.s.detail = 0.0f, high.s.detail = 1.0f;
    low.s.rangeDb = high.s.rangeDb = -18.0f;
    low.run(broad, 100);
    high.run(broad, 100);
    CHECK(low.at(2000.0) < -3.0f);
    CHECK(high.at(2000.0) > low.at(2000.0) + 2.0f);
}

TEST_CASE("spectral dynamics: Manual turns down what's over the threshold, Range above zero boosts", "[fast]")
{
    Detector manual;
    manual.s.autoThreshold = false;
    manual.s.thresholdDb = -30.0f;
    manual.s.rangeDb = -6.0f;
    manual.run(spectrum(-20.0), 100); // tilted, every bin reads -20: 10 dB over
    CHECK(std::abs(manual.at(1000.0) + 6.0f) < 0.01f);
    CHECK(std::abs(manual.at(8000.0) + 6.0f) < 0.01f);
    manual.s.thresholdDb = -6.0f;
    manual.run(spectrum(-20.0), 100); // 14 dB under: back to rest
    CHECK(std::abs(manual.at(1000.0)) < 0.01f);

    Detector boost;
    boost.s.rangeDb = 18.0f;
    boost.run(spectrum(-20.0, 12.0), 100);
    CHECK(boost.at(3000.0) > 9.0f);
}

TEST_CASE("spectral dynamics: it moves at the attack and comes back at the release", "[fast]")
{
    Detector d;
    d.s.rangeDb = -18.0f;
    d.s.attackMs = 5.0f;
    d.s.releaseMs = 500.0f;
    const auto resonant = spectrum(-20.0, 12.0);
    // The trigger's own smoothing (40 ms) comes first: within a few frames it's mostly there.
    d.run(resonant, 6);
    const float down = d.at(3000.0);
    CHECK(down < -7.0f);
    d.run(spectrum(-20.0), 6); // gone: the gain comes back slowly (about 500 ms at 3 kHz, x0.58)
    CHECK(d.at(3000.0) < 0.5f * down);
    d.run(spectrum(-20.0), 200);
    CHECK(d.at(3000.0) > -0.05f);
}
