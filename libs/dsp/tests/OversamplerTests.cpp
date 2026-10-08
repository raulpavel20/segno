#include "fd/dsp/Math.h"
#include "fd/dsp/Oversampler.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::dsp;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;

/// Up- then down-sample with nothing in between (the round trip a device would do).
AudioBuffer roundTrip(Oversampler& os, const AudioBuffer& in, int block = 256)
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
} // namespace

TEST_CASE("oversampler latency is whole and identical for every factor", "[fast]")
{
    Oversampler os;
    os.prepare(512, 1);
    const int latency = os.latencySamples();
    CHECK(latency > 0);
    const auto in = whiteNoise(16384, 1, 0.25f, 1);
    for (int factor : {2, 4, 8})
    {
        os.setFactor(factor);
        CHECK(os.latencySamples() == latency);
        INFO("factor " << factor);
        CHECK(estimateLatency(in, roundTrip(os, in), 400) == latency);
    }
}

TEST_CASE("oversampler round trip is transparent in the passband", "[fast]")
{
    Oversampler os;
    os.prepare(512, 2);
    for (int factor : {2, 4, 8})
    {
        os.setFactor(factor);
        for (double f : {100.0, 1000.0, 10000.0, 20000.0})
        {
            os.reset();
            const auto in = sine(f, sr, 48000, 0.5f, 2);
            const auto out = roundTrip(os, in);
            const int l = os.latencySamples();
            AudioBuffer aligned(2, 48000 - l - 1000);
            AudioBuffer ref(2, aligned.numSamples());
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < aligned.numSamples(); ++i)
                {
                    aligned.at(c, i) = out.at(c, i + l + 1000);
                    ref.at(c, i) = in.at(c, i + 1000);
                }
            INFO("factor " << factor << ", " << f << " Hz");
            // Below 20 kHz the passband is flat to well under 0.01 dB.
            const double tolerance = f < 19000.0 ? -80.0 : -40.0;
            CHECK(nullResidualDb(aligned, ref) < tolerance + toDb(0.5));
        }
    }
}

TEST_CASE("oversampler rejects images and content above the base Nyquist", "[fast]")
{
    Oversampler os;
    os.prepare(4096, 1);
    os.setFactor(2);
    // A tone above base Nyquist in the oversampled domain must not come back down.
    constexpr int n = 8192;
    std::vector<float> silence(n, 0.0f);
    const float* in[] = {silence.data()};
    auto* up = os.upsample(in, 1, 4096);
    for (int i = 0; i < 8192; ++i)
        up[0][i] = static_cast<float>(0.5 * std::sin(2.0 * pi * 30000.0 * i / (2.0 * sr)));
    std::vector<float> out(4096);
    float* o[] = {out.data()};
    os.downsample(o, 1, 4096);
    float peakOut = 0.0f;
    for (int i = 1000; i < 4096; ++i)
        peakOut = std::max(peakOut, std::abs(out[static_cast<size_t>(i)]));
    CHECK(20.0 * std::log10(peakOut / 0.5f + 1e-20) < -100.0);
}
