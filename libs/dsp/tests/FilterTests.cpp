#include "fd/dsp/Filters.h"
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

double magDb(const BiquadCoeffs& c, double f)
{
    return 20.0 * std::log10(biquadMagnitude(c, f, sr));
}
} // namespace

TEST_CASE("RBJ designs have their defining gains", "[fast]")
{
    CHECK(magDb(designBiquad(BiquadType::Peak, sr, 1000, 1.0, 6.0), 1000) == Approx(6.0).margin(1e-6));
    CHECK(magDb(designBiquad(BiquadType::Peak, sr, 1000, 1.0, -12.0), 1000) == Approx(-12.0).margin(1e-6));
    CHECK(magDb(designBiquad(BiquadType::Lowpass, sr, 1000, std::sqrt(0.5)), 1000) == Approx(-3.0103).margin(1e-3));
    CHECK(magDb(designBiquad(BiquadType::Highpass, sr, 1000, std::sqrt(0.5)), 1000) == Approx(-3.0103).margin(1e-3));
    CHECK(magDb(designBiquad(BiquadType::LowShelf, sr, 200, 0.707, 6.0), 10.0) == Approx(6.0).margin(0.05));
    CHECK(magDb(designBiquad(BiquadType::HighShelf, sr, 5000, 0.707, -6.0), 20000.0) == Approx(-6.0).margin(0.1));
    CHECK(biquadMagnitude(designBiquad(BiquadType::Notch, sr, 1000, 2.0), 1000, sr) < 1e-9);
    CHECK(biquadMagnitude(designBiquad(BiquadType::Allpass, sr, 1000, 0.7), 300, sr) == Approx(1.0));
}

TEST_CASE("processed impulse response matches the analytic response", "[fast]")
{
    for (auto type : {BiquadType::Lowpass, BiquadType::Peak, BiquadType::HighShelf, BiquadType::Bandpass})
    {
        const auto c = designBiquad(type, sr, 2000, 0.9, 4.5);
        Biquad bq;
        bq.setCoeffs(c);
        auto ir = impulse(8192, 1);
        for (int i = 0; i < ir.numSamples(); ++i)
            ir.at(0, i) = bq.process(ir.at(0, i));

        for (double f : {50.0, 1000.0, 2000.0, 8000.0, 18000.0})
        {
            const double measured = std::abs(responseAt(ir.channel(0), ir.numSamples(), f, sr));
            CHECK(measured == Approx(biquadMagnitude(c, f, sr)).epsilon(1e-5));
        }
    }
}

TEST_CASE("DC blocker removes DC and passes audio", "[fast]")
{
    DcBlocker dcb;
    dcb.prepare(sr, 5.0);
    auto b = dc(sr, 0.5f, 1);
    for (int i = 0; i < b.numSamples(); ++i)
        b.at(0, i) = dcb.process(b.at(0, i));
    CHECK(std::abs(b.at(0, b.numSamples() - 1)) < 1e-4);

    dcb.reset();
    auto s = sine(1000.0, sr, 48000, 0.5f, 1);
    for (int i = 0; i < s.numSamples(); ++i)
        s.at(0, i) = dcb.process(s.at(0, i));
    CHECK(peakDb(s.slice(24000, 24000)) == Approx(-6.02).margin(0.01));
}

TEST_CASE("one-pole low-pass is -3 dB at its cutoff", "[fast]")
{
    OnePoleLowpass lp;
    lp.setCutoff(1000.0, sr);
    auto ir = impulse(16384, 1);
    for (int i = 0; i < ir.numSamples(); ++i)
        ir.at(0, i) = lp.process(ir.at(0, i));
    const double mag = std::abs(responseAt(ir.channel(0), ir.numSamples(), 1000.0, sr));
    CHECK(20.0 * std::log10(mag) == Approx(-3.0103).margin(1e-3));
}

TEST_CASE("crossover: the bands sum flat, each at -6 dB where they cross, 24 dB/oct", "[fast]")
{
    constexpr double hz = 200.0;
    Crossover x;
    x.prepare(sr);
    x.setFrequency(hz);
    constexpr int n = 1 << 15;
    std::vector<float> low(n), high(n), sum(n);
    for (int i = 0; i < n; ++i)
    {
        const auto b = x.process(i == 0 ? 1.0f : 0.0f);
        low[static_cast<size_t>(i)] = b.low;
        high[static_cast<size_t>(i)] = b.high;
        sum[static_cast<size_t>(i)] = b.low + b.high;
    }
    const auto levelDb = [&](const std::vector<float>& ir, double f)
    { return 20.0 * std::log10(std::abs(responseAt(ir.data(), n, f, sr))); };
    for (const double f : {20.0, 100.0, 200.0, 400.0, 2000.0, 15000.0})
        CHECK(std::abs(levelDb(sum, f)) < 0.01); // an all-pass
    CHECK(levelDb(low, hz) == Approx(-6.02).margin(0.05));
    CHECK(levelDb(high, hz) == Approx(-6.02).margin(0.05));
    CHECK(levelDb(low, 4.0 * hz) < -46.0); // two octaves up: about 48 dB down
    CHECK(levelDb(high, hz / 4.0) < -46.0);
}
