#include "fd/dsp/Math.h"
#include "fd/dsp/Oversampler.h"
#include "fd/dsp/Waveshapers.h"
#include "fd/testkit/Metrics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::dsp;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr ShaperCurve kCurves[] = {ShaperCurve::Soft, ShaperCurve::Warm, ShaperCurve::Tape,
                                   ShaperCurve::Hard, ShaperCurve::Fold, ShaperCurve::Rectify};

/// Driven bin-centred sine through a shaper, with optional ADAA and oversampling; returns the
/// aliasing ratio in the audible band (testkit::aliasingRatioDb).
double aliasing(ShaperCurve curve, bool adaa, int factor, int cycles = 1707 /* ~2.5 kHz */, double driveDb = 12.0)
{
    constexpr double sr = 48000.0;
    constexpr int size = 1 << 15;
    const double f0 = binCentredFrequency(cycles, size, sr);
    const double gain = 0.5 * std::pow(10.0, driveDb / 20.0);
    const int total = size * 2;
    AudioBuffer in(1, total), out(1, total);
    for (int i = 0; i < total; ++i)
        in.at(0, i) = static_cast<float>(gain * std::sin(2.0 * pi * f0 * i / sr));

    Oversampler os;
    os.prepare(256, 1);
    os.setFactor(factor == 1 ? 2 : factor);
    AdaaShaper shaper;
    shaper.setCurve(curve);
    shaper.reset();
    for (int pos = 0; pos < total; pos += 256)
    {
        const float* ip[] = {in.channel(0) + pos};
        float* op[] = {out.channel(0) + pos};
        if (factor == 1)
        {
            for (int i = 0; i < 256; ++i)
                op[0][i] = adaa ? shaper.process(ip[0][i]) : static_cast<float>(shape(curve, ip[0][i]));
            continue;
        }
        auto* up = os.upsample(ip, 1, 256);
        for (int i = 0; i < 256 * factor; ++i)
            up[0][i] = adaa ? shaper.process(up[0][i]) : static_cast<float>(shape(curve, up[0][i]));
        os.downsample(op, 1, 256);
    }
    return aliasingRatioDb(out, 0, total - size, size, cycles, sr);
}
} // namespace

TEST_CASE("antiderivatives match their curves", "[fast]")
{
    for (auto curve : kCurves)
        for (double x : {-3.0, -1.2, -0.4, -0.01, 0.02, 0.5, 0.99, 1.5, 4.0})
        {
            const double h = 1e-5;
            const double numeric = (shapeAntiderivative(curve, x + h) - shapeAntiderivative(curve, x - h)) / (2 * h);
            INFO("curve " << static_cast<int>(curve) << " at x = " << x);
            CHECK(numeric == Approx(shape(curve, x)).margin(1e-6));
        }
}

TEST_CASE("curves are continuous through zero and bounded", "[fast]")
{
    for (auto curve : kCurves)
    {
        CHECK(shape(curve, 1e-9) == Approx(shape(curve, -1e-9)).margin(1e-6));
        CHECK(std::abs(shape(curve, 100.0)) <= 1.0);
        CHECK(std::abs(shape(curve, -100.0)) <= 1.0);
    }
}

TEST_CASE("ADAA on a slow signal follows the curve", "[fast]")
{
    AdaaShaper s;
    s.setCurve(ShaperCurve::Soft);
    s.reset();
    double maxError = 0.0;
    double prev = 0.0;
    for (int i = 0; i < 2000; ++i)
    {
        const double x = 2.0 * std::sin(2.0 * pi * i / 2000.0);
        const double y = s.process(static_cast<float>(x));
        if (i > 0)
            maxError = std::max(maxError, std::abs(y - std::tanh(0.5 * (x + prev)))); // half-sample delay
        prev = x;
    }
    CHECK(maxError < 1e-4);
}

TEST_CASE("ADAA and oversampling each reduce aliasing", "[fast]")
{
    const double naive = aliasing(ShaperCurve::Hard, false, 1);
    const double adaa = aliasing(ShaperCurve::Hard, true, 1);
    const double adaa2x = aliasing(ShaperCurve::Hard, true, 2);
    const double adaa8x = aliasing(ShaperCurve::Hard, true, 8);
    INFO("naive " << naive << ", ADAA " << adaa << ", ADAA 2x " << adaa2x << ", ADAA 8x " << adaa8x << " dB");
    CHECK(adaa < naive - 6.0);
    CHECK(adaa2x < adaa - 6.0);
    CHECK(adaa8x < adaa2x - 20.0);
}

TEST_CASE("aliasing targets from the Saturator spec", "[fast]")
{
    for (auto curve : kCurves)
    {
        INFO("curve " << static_cast<int>(curve));
        // Default quality (4x), +12 dB drive, ~2.5 kHz.
        CHECK(aliasing(curve, true, 4) < -75.0);
        // Worst case: 8x, +24 dB drive, ~5 kHz.
        CHECK(aliasing(curve, true, 8, 3413, 24.0) < -65.0);
    }
}
