#include "fd/dsp/Math.h"
#include "fd/dsp/Resampling.h"
#include "fd/testkit/Metrics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace fd::dsp;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;
constexpr int pad = SincKernels::padding;

/// A source sine with padding around it, as SampleData stores it.
std::vector<float> paddedSine(double cyclesPerSample, int length)
{
    std::vector<float> v(static_cast<size_t>(length + 2 * pad), 0.0f);
    for (int i = 0; i < length; ++i)
        v[static_cast<size_t>(i + pad)] = static_cast<float>(std::sin(twoPi * cyclesPerSample * i));
    return v;
}

/// Reads `src` at `speed` (source samples per output sample) with the matching kernel.
AudioBuffer play(const std::vector<float>& src, double speed, int outLength)
{
    const auto& table = SincKernels::instance().table(speed);
    AudioBuffer out(1, outLength);
    for (int i = 0; i < outLength; ++i)
        out.at(0, i) = SincKernels::read(table, src.data() + pad, 100.0 + i * speed);
    return out;
}
} // namespace

TEST_CASE("sinc interpolation reproduces the source at integer positions", "[fast]")
{
    const auto src = paddedSine(0.1, 4000);
    const auto& t = SincKernels::instance().table(1.0);
    for (int i = 100; i < 3900; i += 37)
        CHECK(SincKernels::read(t, src.data() + pad, i) == Approx(src[static_cast<size_t>(i + pad)]).margin(1e-4));
}

TEST_CASE("sinc interpolation is flat in the pass band and exact in pitch", "[fast]")
{
    constexpr int n = 1 << 14;
    for (double speed : {0.37, 0.8, 1.0, 1.26, 1.9})
        for (int cycles : {101, 1501, 3501}) // output frequency, bin-centred
        {
            const double outF = binCentredFrequency(cycles, n, sr) / sr;
            if (outF > 0.40 || outF / speed > 0.40) // keep the source itself below its Nyquist
                continue;
            const auto src = paddedSine(outF / speed, static_cast<int>(n * speed) + 400);
            const auto out = play(src, speed, n);
            INFO("speed " << speed << ", output " << outF * sr << " Hz");
            const auto levels = harmonicLevelsDb(out, 0, 0, n, cycles, 1);
            CHECK(levels[0] == Approx(0.0).margin(0.05)); // flat, and all energy in the exact bin
        }
}

TEST_CASE("reading faster suppresses aliasing with stretched kernels", "[fast]")
{
    // A source tone that would alias when read at 1.9x: 0.4 of the source rate maps to 0.76
    // of the output rate. The stretched kernel must remove it (< -70 dB).
    constexpr int n = 1 << 14;
    const double speed = 1.9;
    const auto src = paddedSine(0.4, static_cast<int>(n * speed) + 400);
    const auto out = play(src, speed, n);
    CHECK(peakDb(out) < -70.0);
}

TEST_CASE("decimation keeps the band and removes what would alias", "[fast]")
{
    constexpr int n = 1 << 15;
    auto level = [&](double cyclesPerSample) {
        std::vector<float> in(static_cast<size_t>(2 * n));
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = static_cast<float>(std::sin(twoPi * cyclesPerSample * static_cast<double>(i)));
        const auto out = decimateByTwo(in);
        AudioBuffer b(1, n);
        for (int i = 0; i < n; ++i)
            b.at(0, i) = out[static_cast<size_t>(i)];
        return rmsDb(b.slice(1000, n - 2000)) + 3.0103; // sine RMS -> amplitude in dB
    };
    CHECK(level(0.05) == Approx(0.0).margin(0.01));
    CHECK(level(0.2) == Approx(0.0).margin(0.05));
    CHECK(level(0.26) < -70.0);
    CHECK(level(0.45) < -70.0);
}
