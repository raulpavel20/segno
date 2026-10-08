#include "Eq.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using E = fd::devices::Eq;

namespace
{
constexpr double sr = 48000.0;

void setBand(Device& d, int band, E::BandType type, float freq, float gain, float q = 0.71f,
             E::Placement place = E::Placement::Stereo, int slope = 1)
{
    auto& p = d.params();
    p.set(E::paramIndex(band, E::On), 1.0f);
    p.set(E::paramIndex(band, E::Type), static_cast<float>(type));
    p.set(E::paramIndex(band, E::Freq), freq);
    p.set(E::paramIndex(band, E::Gain), gain);
    p.set(E::paramIndex(band, E::Q), q);
    p.set(E::paramIndex(band, E::Place), static_cast<float>(place));
    p.set(E::paramIndex(band, E::Slope), static_cast<float>(slope));
}

/// Measured magnitude in dB at f from an impulse through the device (L channel).
double measuredDb(Device& d, double f, int channel = 0)
{
    const auto ir = render(d, impulse(16384, 2), {.sampleRate = sr});
    return 20.0 * std::log10(std::abs(responseAt(ir.channel(channel), ir.numSamples(), f, sr)));
}
} // namespace

TEST_CASE("EQ has 24 bands and permanent IDs", "[fast]")
{
    E eq;
    CHECK(eq.params().size() == static_cast<size_t>(E::NumParams));
    CHECK(eq.params().spec(E::paramIndex(2, E::Freq)).id == "band3.freq");
    CHECK(eq.params().spec(E::Output).id == "output");
}

TEST_CASE("EQ defaults are bit-transparent", "[fast]")
{
    E eq;
    const auto in = whiteNoise(48000, 2, 0.5f);
    CHECK(nullResidualDb(render(eq, in, {.sampleRate = sr}), in) == silenceDb);
}

TEST_CASE("EQ with all bands off nulls against the input", "[fast]")
{
    E eq;
    for (int b = 0; b < E::numBands; ++b)
        eq.params().set(E::paramIndex(b, E::On), 0.0f);
    const auto in = whiteNoise(48000, 3, 0.5f);
    CHECK(nullResidualDb(render(eq, in, {.sampleRate = sr}), in) == silenceDb);
}

TEST_CASE("EQ bell, shelf and cut responses match the design", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 6.0f, 1.0f);
    CHECK(measuredDb(eq, 1000.0) == Approx(6.0).margin(0.05));

    E shelf;
    setBand(shelf, 0, E::BandType::LowShelf, 200.0f, -9.0f);
    CHECK(measuredDb(shelf, 20.0) == Approx(-9.0).margin(0.1));

    for (int slope = 0; slope < 8; ++slope)
    {
        // Butterworth attenuation one octave below the cutoff: 10 log10(1 + 2^(2N)).
        E cut;
        setBand(cut, 0, E::BandType::LowCut, 1000.0f, 0.0f, 0.71f, E::Placement::Stereo, slope);
        const int order = E::slopeDbPerOct(slope) / 6;
        const double expected = -10.0 * std::log10(1.0 + std::pow(2.0, 2.0 * order));
        INFO("slope index " << slope);
        CHECK(measuredDb(cut, 500.0) == Approx(expected).margin(0.5));
        CHECK(measuredDb(cut, 1000.0) == Approx(-3.0103).margin(0.1));
    }
}

TEST_CASE("EQ Mid/Side placement leaves the other component untouched", "[fast]")
{
    // L == R input has no side: a side band must change nothing.
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 12.0f, 1.0f, E::Placement::Side);
    const auto in = sine(1000.0, sr, 24000, 0.25f, 2);
    const auto out = render(eq, in, {.sampleRate = sr});
    CHECK(nullResidualDb(out, in) < -100.0);

    // A left band leaves the right channel bit-identical.
    E left;
    setBand(left, 1, E::BandType::Bell, 1000.0f, 12.0f, 1.0f, E::Placement::Left);
    const auto noise = whiteNoise(24000, 5, 0.25f);
    const auto o2 = render(left, noise, {.sampleRate = sr});
    bool rightUntouched = true;
    for (int i = 0; i < noise.numSamples(); ++i)
        rightUntouched &= o2.at(1, i) == noise.at(1, i);
    CHECK(rightUntouched);
}

TEST_CASE("EQ fast frequency sweep does not click", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 20.0f, 12.0f, 2.0f);
    const auto in = sine(1000.0, sr, 96000, 0.25f, 2);
    RenderOptions opts{.sampleRate = sr};
    opts.blockPattern = {64};
    opts.beforeBlock = [](Device& d, std::int64_t pos) {
        const double t = static_cast<double>(pos) / 48000.0; // 20 Hz -> 20 kHz over 1 s (log)
        d.params().set(E::paramIndex(1, E::Freq), static_cast<float>(20.0 * std::pow(1000.0, std::min(1.0, t))));
    };
    const auto out = render(eq, in, opts);
    CHECK(allFinite(out));
    CHECK(clickScore(out.slice(4800, 86400), 0) < 8.0);
}

TEST_CASE("EQ analyzer taps carry the pre and post signals", "[fast]")
{
    E eq;
    setBand(eq, 1, E::BandType::Bell, 1000.0f, 12.0f, 1.0f);
    render(eq, sine(1000.0, sr, 4096, 0.25f, 2), {.sampleRate = sr});
    std::vector<float> pre(4096), post(4096);
    CHECK(eq.analysis().fifo(E::PreFifo).pop(pre.data(), pre.size()) == 4096);
    CHECK(eq.analysis().fifo(E::PostFifo).pop(post.data(), post.size()) == 4096);
    float peakPre = 0.0f, peakPost = 0.0f;
    for (int i = 2048; i < 4096; ++i)
    {
        peakPre = std::max(peakPre, std::abs(pre[static_cast<size_t>(i)]));
        peakPost = std::max(peakPost, std::abs(post[static_cast<size_t>(i)]));
    }
    CHECK(20.0 * std::log10(peakPost / peakPre) == Approx(12.0).margin(0.2));
}
