// The dynamics core (M2 step A1): static curves, attack and release times, detection and the
// lookahead blocks.

#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <deque>
#include <limits>
#include <random>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
constexpr double kRate = 48000.0;

/// The textbook soft-knee compressor (Giannoulis, Massberg and Reiss, 2012), in double.
double referenceCompressor(double x, double t, double r, double w)
{
    if (2.0 * (x - t) < -w)
        return x;
    if (2.0 * std::abs(x - t) <= w && w > 0.0)
        return x + (1.0 / r - 1.0) * (x - t + w / 2.0) * (x - t + w / 2.0) / (2.0 * w);
    return t + (x - t) / r;
}

double referenceExpander(double x, double t, double r, double w, double range)
{
    double y = x;
    if (2.0 * (x - t) > w)
        y = x;
    else if (2.0 * std::abs(x - t) <= w && w > 0.0)
        y = x - (r - 1.0) * (x - t - w / 2.0) * (x - t - w / 2.0) / (2.0 * w);
    else
        y = t + (x - t) * r;
    return std::max(y, x + range);
}

/// Seconds from the first sample past `from` to the first past `to`, as `smoother` moves to `target`.
template <typename Smoother>
double crossingTime(Smoother& s, float target, float from, float to)
{
    const bool falling = to < from;
    long long start = -1;
    for (long long n = 0; n < static_cast<long long>(kRate * 30); ++n)
    {
        const float y = s.process(target);
        if (start < 0 && (falling ? y <= from : y >= from))
            start = n;
        if (falling ? y <= to : y >= to)
            return static_cast<double>(n - std::max(0LL, start)) / kRate;
    }
    return -1.0;
}
} // namespace

TEST_CASE("dynamics: the compressor curve follows the formula within 0.01 dB", "[fast]")
{
    for (double ratio : {1.0, 1.5, 2.0, 4.0, 10.0, 100.0})
        for (double knee : {0.0, 3.0, 6.0, 24.0})
            for (double threshold : {-40.0, -18.0, 0.0})
                for (double x = -80.0; x <= 12.0; x += 0.25)
                {
                    INFO("ratio " << ratio << " knee " << knee << " threshold " << threshold << " in " << x);
                    CHECK(compressorCurve(static_cast<float>(x), static_cast<float>(threshold), static_cast<float>(ratio),
                                          static_cast<float>(knee)) ==
                          Approx(referenceCompressor(x, threshold, ratio, knee)).margin(0.01));
                }
    // Infinite ratio: a limiter, flat above the threshold (and the knee).
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(compressorCurve(0.0f, -6.0f, inf, 0.0f) == Approx(-6.0f));
    CHECK(compressorCurve(-12.0f, -6.0f, inf, 0.0f) == Approx(-12.0f));
    CHECK(compressorCurve(0.0f, -6.0f, inf, 4.0f) == Approx(-6.0f));
    // Ratio 1 changes nothing.
    CHECK(compressorCurve(5.0f, -20.0f, 1.0f, 6.0f) == Approx(5.0f));
}

TEST_CASE("dynamics: the expander curve follows the formula within 0.01 dB, down to its range", "[fast]")
{
    for (double ratio : {1.0, 2.0, 4.0, 10.0})
        for (double knee : {0.0, 6.0})
            for (double range : {-20.0, -60.0, -1000.0})
                for (double x = -100.0; x <= 6.0; x += 0.5)
                {
                    INFO("ratio " << ratio << " knee " << knee << " range " << range << " in " << x);
                    CHECK(expanderCurve(static_cast<float>(x), -30.0f, static_cast<float>(ratio), static_cast<float>(knee),
                                        static_cast<float>(range)) ==
                          Approx(referenceExpander(x, -30.0, ratio, knee, range)).margin(0.01));
                }
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(expanderCurve(-50.0f, -30.0f, 2.0f, 0.0f, -inf) == Approx(-70.0f)); // no range limit
    CHECK(expanderCurve(-50.0f, -30.0f, 2.0f, 0.0f, -10.0f) == Approx(-60.0f));
}

TEST_CASE("dynamics: attack times are 10 to 90 %, releases to -60 dB, within 5 %", "[fast]")
{
    for (double ms : {1.0, 10.0, 100.0, 1000.0})
    {
        INFO(ms << " ms");
        GainSmoother attack;
        attack.setCoefficients(attackCoefficient(ms, kRate), 0.0);
        attack.reset(0.0f);
        // A step from 0 to -20 dB of reduction: 10 % is -2 dB, 90 % is -18 dB.
        CHECK(crossingTime(attack, -20.0f, -2.0f, -18.0f) == Approx(ms * 0.001).epsilon(0.05));

        GainSmoother release;
        release.setCoefficients(0.0, releaseCoefficient(ms, kRate));
        release.reset(-20.0f);
        // Back from -20 dB: done when a thousandth of it is left.
        CHECK(crossingTime(release, 0.0f, -20.0f, -0.02f) == Approx(ms * 0.001).epsilon(0.05));
    }
    CHECK(attackCoefficient(0.0, kRate) == 0.0); // instant

    // Gates: the attack is the gain going up.
    GainSmoother gate;
    gate.setCoefficients(releaseCoefficient(50.0, kRate), attackCoefficient(1.0, kRate));
    gate.reset(-1000.0f); // silence: clamped
    CHECK(gate.value() == minusInfinityDb);
    CHECK(crossingTime(gate, 0.0f, -90.0f, -10.0f) == Approx(0.001).epsilon(0.05));
}

TEST_CASE("dynamics: auto release is fast after a burst and slow after sustained reduction", "[fast]")
{
    const auto recovery = [](double burstSeconds) {
        AutoReleaseSmoother s;
        s.setCoefficients(attackCoefficient(1.0, kRate), releaseCoefficient(50.0, kRate),
                          releaseCoefficient(1500.0, kRate), attackCoefficient(500.0, kRate));
        s.reset(0.0f);
        for (int n = 0; n < static_cast<int>(burstSeconds * kRate); ++n)
            s.process(-12.0f);
        int n = 0;
        while (s.process(0.0f) < -0.012f && n < static_cast<int>(10 * kRate))
            ++n;
        return n / kRate;
    };
    const double afterBurst = recovery(0.02), afterSustain = recovery(3.0);
    CHECK(afterBurst < 0.1);    // near the fast release (50 ms)
    CHECK(afterSustain > 0.75); // near the slow release (1.5 s)
    CHECK(afterSustain < 1.6);
}

TEST_CASE("dynamics: peak and RMS detection, stereo link", "[fast]")
{
    LevelDetector peak;
    peak.prepare(kRate);
    CHECK(peak.processDb(-0.5f) == Approx(-6.02f).margin(0.01));
    CHECK(peak.processDb(0.0f) == minusInfinityDb);

    LevelDetector rms;
    rms.prepare(kRate, 10.0);
    rms.setMode(LevelDetector::Mode::Rms);
    float level = 0.0f;
    for (int n = 0; n < 48000; ++n)
        level = rms.processDb(static_cast<float>(std::sin(twoPi * 1000.0 * n / kRate)));
    CHECK(level == Approx(-3.01f).margin(0.1)); // a full-scale sine's RMS

    float levels[2] = {-20.0f, -6.0f};
    linkLevels(levels, 2, 1.0f);
    CHECK(levels[0] == Approx(-6.0f));
    float half[2] = {-20.0f, -6.0f};
    linkLevels(half, 2, 0.5f);
    CHECK(half[0] == Approx(-13.0f));
    CHECK(half[1] == Approx(-6.0f));
    float none[2] = {-20.0f, -6.0f};
    linkLevels(none, 2, 0.0f);
    CHECK(none[0] == Approx(-20.0f));
}

TEST_CASE("dynamics: lookahead delay, sliding minimum and moving average", "[fast]")
{
    DelayLine delay;
    delay.prepare(480);
    delay.setDelay(48);
    for (int n = 0; n < 100; ++n)
        CHECK(delay.process(n == 0 ? 1.0f : 0.0f) == (n == 48 ? 1.0f : 0.0f));

    std::mt19937 random(7);
    std::uniform_real_distribution<float> value(-1.0f, 1.0f);
    for (int length : {1, 2, 7, 64, 480})
    {
        SlidingMinimum minimum;
        minimum.prepare(480);
        minimum.setLength(length);
        MovingAverage average;
        average.prepare(480);
        average.setLength(length, 0.0f);
        std::deque<float> window(static_cast<size_t>(length), 0.0f);
        std::deque<float> recent;
        for (int n = 0; n < 5000; ++n)
        {
            const float x = value(random);
            recent.push_back(x);
            if (static_cast<int>(recent.size()) > length)
                recent.pop_front();
            window.push_back(x);
            window.pop_front();
            INFO("length " << length << " at " << n);
            CHECK(minimum.process(x) == *std::min_element(recent.begin(), recent.end()));
            double sum = 0.0;
            for (float w : window)
                sum += w;
            CHECK(average.process(x) == Approx(sum / length).margin(1e-5));
        }
    }
}

TEST_CASE("dynamics: the sidechain filter filters, glides and crossfades types", "[fast]")
{
    SidechainFilter f;
    f.prepare(kRate, 480);
    // Peak level of a sine through the filter, after it settles.
    auto levelOf = [&](double hz) {
        float peak = 0.0f;
        for (int n = 0; n < 9600; ++n)
        {
            float frame[2] = {static_cast<float>(std::sin(twoPi * hz * n / kRate)), 0.0f};
            f.process(frame, 2);
            if (n >= 4800)
                peak = std::max(peak, std::abs(frame[0]));
        }
        return gainToDb(peak);
    };
    f.reset(SidechainFilter::Type::Off, 1000.0f, 0.71f, 0.0f);
    CHECK(levelOf(100.0) == Approx(0.0f).margin(0.01));
    f.reset(SidechainFilter::Type::HighPass, 1000.0f, 0.71f, 0.0f);
    CHECK(levelOf(100.0) < -35.0f);
    CHECK(levelOf(10000.0) == Approx(0.0f).margin(0.1));
    f.reset(SidechainFilter::Type::Bell, 1000.0f, 1.0f, 12.0f);
    CHECK(levelOf(1000.0) == Approx(12.0f).margin(0.1));

    // A change of type: the output moves from one filter's to the other's within the fade,
    // without a step.
    f.reset(SidechainFilter::Type::Off, 1000.0f, 0.71f, 0.0f);
    float last = 0.0f, biggestStep = 0.0f;
    for (int n = 0; n < 4800; ++n)
    {
        if (n % 32 == 0)
            f.update(n < 2400 ? SidechainFilter::Type::Off : SidechainFilter::Type::LowPass, 200.0f, 0.71f, 0.0f, 30);
        float frame[1] = {static_cast<float>(std::sin(twoPi * 50.0 * n / kRate))};
        f.process(frame, 1);
        if (n > 0)
            biggestStep = std::max(biggestStep, std::abs(frame[0] - last));
        last = frame[0];
    }
    CHECK(biggestStep < 0.02f); // a 50 Hz sine moves at most 0.0066 per sample
}
