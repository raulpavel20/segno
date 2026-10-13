#include "fd/dsp/IirOversampler.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/Allocations.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>

using namespace fd::dsp;
using namespace fd::testkit;

namespace
{
constexpr double sr = 48000.0;

/// The half-band's response at `f` (a share of the oversampled rate).
std::complex<double> halfband(const std::vector<double>& c, double f)
{
    const auto z1 = std::polar(1.0, -2.0 * pi * f), z2 = z1 * z1;
    std::complex<double> a0{1.0, 0.0}, a1{1.0, 0.0};
    for (size_t i = 0; i < c.size(); ++i)
        (i % 2 == 0 ? a0 : a1) *= (c[i] + z2) / (1.0 + c[i] * z2);
    return 0.5 * (a0 + z1 * a1);
}

/// Up- then down-sample with nothing in between.
AudioBuffer roundTrip(IirOversampler& os, const AudioBuffer& in, int block = 256)
{
    AudioBuffer out(in.numChannels(), in.numSamples());
    std::vector<const float*> ip(static_cast<size_t>(in.numChannels()));
    std::vector<float*> op(static_cast<size_t>(in.numChannels()));
    for (int pos = 0; pos < in.numSamples(); pos += block)
    {
        const int n = std::min(block, in.numSamples() - pos);
        for (int c = 0; c < in.numChannels(); ++c)
        {
            ip[static_cast<size_t>(c)] = in.channel(c) + pos;
            op[static_cast<size_t>(c)] = out.channel(c) + pos;
        }
        os.upsample(ip.data(), in.numChannels(), n);
        os.downsample(op.data(), in.numChannels(), n);
    }
    return out;
}

double db(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-30));
}
} // namespace

TEST_CASE("IIR half-band: 106 dB down past its transition, flat before it", "[fast]")
{
    const auto c = IirHalfband::design(100.0, 0.0232);
    REQUIRE(c.size() == static_cast<size_t>(IirOversampler::numCoefficients));
    for (size_t i = 0; i < c.size(); ++i)
    {
        CHECK(c[i] > 0.0);
        CHECK(c[i] < 1.0);
        if (i > 0)
            CHECK(c[i] > c[i - 1]);
    }
    double stop = 0.0, ripple = 0.0;
    for (double f = 0.2732; f <= 0.5; f += 0.0005)
        stop = std::max(stop, std::abs(halfband(c, f)));
    for (double f = 0.0; f <= 0.2268; f += 0.0005)
        ripple = std::max(ripple, std::abs(db(std::abs(halfband(c, f)))));
    CHECK(db(stop) < -100.0);
    CHECK(ripple < 0.001);
}

TEST_CASE("IIR oversampler: a few samples of latency, as measured", "[fast]")
{
    IirOversampler os;
    os.prepare(512, 1);
    CHECK(os.latencySamples() == 4);
    const auto in = whiteNoise(16384, 1, 0.25f, 1);
    CHECK(estimateLatency(in, roundTrip(os, in), 400) == os.latencySamples());
}

TEST_CASE("IIR oversampler: the round trip is flat to 20 kHz", "[fast]")
{
    IirOversampler os;
    os.prepare(512, 1);
    const auto ir = roundTrip(os, impulse(4096, 1));
    for (const double hz : {20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0})
    {
        INFO(hz << " Hz");
        CHECK(std::abs(db(std::abs(responseAt(ir.channel(0), ir.numSamples(), hz, sr)))) < 0.01);
    }
}

TEST_CASE("IIR oversampler: images and aliases 100 dB down", "[fast]")
{
    IirOversampler os;
    os.prepare(512, 1);
    const int size = 16384;
    SECTION("upsampling: a 10 kHz tone's image at 38 kHz")
    {
        const double tone = binCentredFrequency(3413, size, sr); // about 10 kHz
        const auto in = sine(tone, sr, size + 1024, 0.5f, 1);
        AudioBuffer up(1, 2 * in.numSamples());
        for (int pos = 0; pos < in.numSamples(); pos += 256)
        {
            const float* ip[] = {in.channel(0) + pos};
            const auto* out = os.upsample(ip, 1, 256);
            for (int i = 0; i < 512; ++i)
                up.at(0, 2 * pos + i) = out[0][i];
        }
        // At twice the rate the bins are as wide, so the tone is still bin 3413, and the image the
        // half-band takes out (48 kHz less the tone) is the bin as far below `size`.
        const auto power = powerSpectrum(up, 0, 2048, 2 * size);
        CHECK(10.0 * std::log10(power[static_cast<size_t>(size - 3413)] / power[3413]) < -100.0);
    }
    SECTION("downsampling: a 30 kHz tone (at twice the rate) folding to 18 kHz")
    {
        const int n = size + 1024;
        AudioBuffer high(1, 2 * n);
        const double tone = 30000.0;
        for (int i = 0; i < 2 * n; ++i)
            high.at(0, i) = 0.5f * static_cast<float>(std::sin(2.0 * pi * tone * i / (2.0 * sr)));
        AudioBuffer out(1, n);
        for (int pos = 0; pos < n; pos += 256)
        {
            // The tone goes in where upsample leaves its output, as a device's processing would.
            const float zeros[256] = {};
            const float* z[] = {zeros};
            auto* buffers = os.upsample(z, 1, 256);
            for (int i = 0; i < 512; ++i)
                buffers[0][i] = high.at(0, 2 * pos + i);
            float* op[] = {out.channel(0) + pos};
            os.downsample(op, 1, 256);
        }
        CHECK(db(rms(out.slice(1024, n - 1024))) < db(0.5 / std::sqrt(2.0)) - 100.0);
    }
}

TEST_CASE("IIR oversampler: roundTrip is exactly the trip up and down", "[fast]")
{
    IirOversampler wet, dry;
    wet.prepare(64, 2);
    dry.prepare(64, 2);
    const auto in = pinkNoise(8192, 5, 0.5f, 2);
    const auto expected = roundTrip(wet, in, 64);
    AudioBuffer got = in;
    for (int pos = 0; pos < got.numSamples(); pos += 64)
    {
        float* io[] = {got.channel(0) + pos, got.channel(1) + pos};
        dry.roundTrip(io, 2, 64);
    }
    CHECK(bitIdentical(expected, got));
}

TEST_CASE("half-band decimator: the oversampler's way down, a pair at a time", "[fast]")
{
    // The same signal at twice the rate, down through the oversampler and through the decimator.
    IirOversampler os;
    os.prepare(256, 1);
    HalfbandDecimator decimator;
    const auto high = whiteNoise(2 * 4096, 7, 0.5f, 1);
    AudioBuffer expected(1, 4096), got(1, 4096);
    for (int pos = 0; pos < 4096; pos += 256)
    {
        const float zeros[256] = {};
        const float* z[] = {zeros};
        auto* buffers = os.upsample(z, 1, 256);
        for (int i = 0; i < 512; ++i)
            buffers[0][i] = high.at(0, 2 * pos + i);
        float* op[] = {expected.channel(0) + pos};
        os.downsample(op, 1, 256);
    }
    for (int i = 0; i < 4096; ++i)
        got.at(0, i) = decimator.process(high.at(0, 2 * i), high.at(0, 2 * i + 1));
    CHECK(bitIdentical(expected, got));
    decimator.reset();
    CHECK(decimator.process(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("IIR oversampler: no allocation while processing", "[fast]")
{
    IirOversampler os;
    os.prepare(64, 2);
    const auto in = whiteNoise(64, 3, 0.25f, 2);
    AudioBuffer out(2, 64);
    const auto before = allocationsDuringProcess();
    {
        const CountAllocations counting;
        for (int k = 0; k < 100; ++k)
        {
            const float* ip[] = {in.channel(0), in.channel(1)};
            float* op[] = {out.channel(0), out.channel(1)};
            os.upsample(ip, 2, 64);
            os.downsample(op, 2, 64);
            os.roundTrip(op, 2, 64);
        }
    }
    CHECK(allocationsDuringProcess() == before);
}
