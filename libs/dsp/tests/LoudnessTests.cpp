// Loudness and true peak (M2 step A1): BS.1770's K-weighting, EBU Tech 3341's reference cases
// for momentary, short-term and integrated loudness, and inter-sample peaks.

#include "fd/dsp/Loudness.h"
#include "fd/dsp/Math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
/// A 1 kHz sine in both channels: (level in dBFS, seconds) after each other.
struct Segment
{
    double dbfs, seconds;
};

LoudnessMeter measure(const std::vector<Segment>& segments, double rate = 48000.0, int channels = 2)
{
    LoudnessMeter meter;
    meter.prepare(rate, channels);
    std::vector<float> left, right;
    double phase = 0.0;
    for (const auto& s : segments)
    {
        const double amplitude = std::pow(10.0, s.dbfs / 20.0);
        const auto n = static_cast<size_t>(s.seconds * rate);
        for (size_t i = 0; i < n; ++i)
        {
            const auto x = static_cast<float>(amplitude * std::sin(phase));
            phase += twoPi * 1000.0 / rate;
            left.push_back(x);
            right.push_back(x);
        }
    }
    // Fed in uneven blocks, as hosts do.
    size_t at = 0;
    for (int block = 1; at < left.size(); block = block * 7 % 1021 + 1)
    {
        const auto n = std::min(left.size() - at, static_cast<size_t>(block));
        const float* in[2] = {left.data() + at, right.data() + at};
        meter.process(in, static_cast<int>(n));
        at += n;
    }
    return meter;
}
} // namespace

TEST_CASE("loudness: K-weighting is BS.1770's filter at 48 kHz", "[fast]")
{
    const auto k = kWeighting(48000.0);
    CHECK(k[0].b0 == Approx(1.53512485958697).margin(1e-8));
    CHECK(k[0].b1 == Approx(-2.69169618940638).margin(1e-8));
    CHECK(k[0].b2 == Approx(1.19839281085285).margin(1e-8));
    CHECK(k[0].a1 == Approx(-1.69065929318241).margin(1e-8));
    CHECK(k[0].a2 == Approx(0.73248077421585).margin(1e-8));
    CHECK(k[1].b0 == 1.0);
    CHECK(k[1].b1 == -2.0);
    CHECK(k[1].b2 == 1.0);
    CHECK(k[1].a1 == Approx(-1.99004745483398).margin(1e-8));
    CHECK(k[1].a2 == Approx(0.99007225036621).margin(1e-8));
    // About +0.7 dB at 1 kHz, which BS.1770's -0.691 offset takes back.
    const double at1k = biquadMagnitude(k[0], 1000.0, 48000.0) * biquadMagnitude(k[1], 1000.0, 48000.0);
    CHECK(20.0 * std::log10(at1k) == Approx(0.691).margin(0.01));
}

TEST_CASE("loudness: EBU Tech 3341 cases 1 and 2 (steady sines), at 48 and 44.1 kHz", "[fast]")
{
    for (double rate : {48000.0, 44100.0})
        for (double level : {-23.0, -33.0})
        {
            INFO(rate << " Hz, " << level << " dBFS");
            const auto m = measure({{level, 20.0}}, rate);
            CHECK(m.momentary() == Approx(level).margin(0.1));
            CHECK(m.shortTerm() == Approx(level).margin(0.1));
            CHECK(m.integrated() == Approx(level).margin(0.1));
            CHECK(m.maxShortTerm() == Approx(level).margin(0.1));
        }
}

TEST_CASE("loudness: EBU Tech 3341 cases 3 to 5 (gating)", "[fast]")
{
    CHECK(measure({{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}}).integrated() == Approx(-23.0).margin(0.1));
    CHECK(measure({{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}}).integrated() ==
          Approx(-23.0).margin(0.1));
    CHECK(measure({{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}}).integrated() == Approx(-23.0).margin(0.1));
}

TEST_CASE("loudness: mono, silence, short audio and reset", "[fast]")
{
    // A full-scale 1 kHz sine in one channel: -3.01 LUFS.
    CHECK(measure({{0.0, 5.0}}, 48000.0, 1).integrated() == Approx(-3.01).margin(0.1));

    const auto silent = measure({{-200.0, 5.0}});
    CHECK(std::isinf(silent.integrated()));
    CHECK(silent.momentary() < -150.0f);

    const auto brief = measure({{-20.0, 0.3}}); // under 400 ms: no block yet
    CHECK(std::isinf(brief.momentary()));
    CHECK(std::isinf(brief.integrated()));

    auto m = measure({{-23.0, 5.0}});
    CHECK(m.shortTerm() == Approx(-23.0).margin(0.1));
    m.reset();
    CHECK(std::isinf(m.integrated()));
    CHECK(std::isinf(m.maxMomentary()));
}

TEST_CASE("true peak: inter-sample peaks are found within 0.2 dB", "[fast]")
{
    // A quarter-rate sine at 45 degrees: every sample is at 0.707, the wave reaches 1.0 between them.
    TruePeakDetector tp;
    tp.prepare(1);
    float samplePeak = 0.0f, truePeak = 0.0f;
    for (int n = 0; n < 4800; ++n)
    {
        const auto x = static_cast<float>(std::sin(pi / 2.0 * n + pi / 4.0));
        samplePeak = std::max(samplePeak, std::abs(x));
        if (const float v = tp.processSample(0, x); n > 64)
            truePeak = std::max(truePeak, v);
    }
    CHECK(gainToDb(samplePeak) == Approx(-3.01f).margin(0.01));
    CHECK(gainToDb(truePeak) == Approx(0.0f).margin(0.2));

    // Sines up to a quarter of the rate, at any phase, read their true amplitude.
    for (double hz : {100.0, 997.0, 5000.0, 9000.0, 12000.0})
        for (double phase : {0.0, 0.3, 1.1, 2.0})
        {
            INFO(hz << " Hz, phase " << phase);
            TruePeakDetector d;
            d.prepare(1);
            float peak = 0.0f;
            for (int n = 0; n < 48000; ++n)
            {
                const auto x = static_cast<float>(0.5 * std::sin(twoPi * hz * n / 48000.0 + phase));
                const float v = d.processSample(0, x);
                if (n > 64) // past the interpolator's ringing on the abrupt start
                    peak = std::max(peak, v);
            }
            CHECK(gainToDb(peak) == Approx(-6.02f).margin(0.2));
        }
}

TEST_CASE("true peak: per-sample values arrive after the reported latency", "[fast]")
{
    TruePeakDetector tp;
    tp.prepare(1);
    int loudest = -1;
    float largest = 0.0f;
    for (int n = 0; n < 64; ++n)
        if (const float v = tp.processSample(0, n == 0 ? 1.0f : 0.0f); v > largest)
        {
            largest = v;
            loudest = n;
        }
    CHECK(std::abs(loudest - TruePeakDetector::latencySamples()) <= 1);
    CHECK(largest == Approx(1.0f).margin(0.05));
}
