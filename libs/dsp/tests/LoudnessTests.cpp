// Loudness and true peak (M2 step A1): BS.1770's K-weighting, EBU Tech 3341's reference cases
// for momentary, short-term and integrated loudness, and inter-sample peaks.

#include "fd/dsp/Loudness.h"
#include "fd/dsp/Math.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
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

TEST_CASE("loudness: EBU Tech 3342 cases 1 to 4 (loudness range), at 48 and 44.1 kHz", "[fast]")
{
    // Tech 3342 allows 1 LU either way; synthetic tones should land far closer.
    for (double rate : {48000.0, 44100.0})
    {
        INFO(rate << " Hz");
        CHECK(measure({{-20.0, 20.0}, {-30.0, 20.0}}, rate).loudnessRange() == Approx(10.0).margin(0.1));
        CHECK(measure({{-20.0, 20.0}, {-15.0, 20.0}}, rate).loudnessRange() == Approx(5.0).margin(0.1));
        CHECK(measure({{-40.0, 20.0}, {-20.0, 20.0}}, rate).loudnessRange() == Approx(20.0).margin(0.1));
        CHECK(measure({{-50.0, 20.0}, {-35.0, 20.0}, {-20.0, 20.0}, {-35.0, 20.0}, {-50.0, 20.0}}, rate)
                  .loudnessRange() == Approx(15.0).margin(0.1));
    }
    // Repeating a test signal leaves it as it was (Tech 3342, §4).
    CHECK(measure({{-20.0, 20.0}, {-30.0, 20.0}, {-20.0, 20.0}, {-30.0, 20.0}}).loudnessRange() ==
          Approx(10.0).margin(0.1));
    CHECK(std::isnan(measure({{-20.0, 2.0}}).loudnessRange())); // no short-term value yet
}

namespace
{
/// What a LoudnessStore should read, worked out the long way from its steps (negative: not
/// heard): BS.1770's gating over every block of 4 heard steps, and Tech 3342's reference
/// algorithm, sorting included, over every window of 30.
struct Reference
{
    double integrated = NAN, range = NAN, maxMomentary = NAN, maxShortTerm = NAN;
    float truePeak = 0.0f;
};

Reference reference(const std::vector<double>& steps, const std::vector<float>& peaks)
{
    const auto loudness = [](double ms) { return -0.691 + 10.0 * std::log10(ms); };
    const auto windows = [&](int length)
    {
        std::vector<double> out;
        for (size_t end = static_cast<size_t>(length - 1); end < steps.size(); ++end)
        {
            double sum = 0.0;
            bool whole = true;
            for (size_t i = end + 1 - static_cast<size_t>(length); i <= end && whole; ++i)
                whole = steps[i] >= 0.0, sum += steps[i];
            if (whole && sum > 0.0)
                out.push_back(sum / length);
        }
        return out;
    };
    Reference r;
    std::vector<double> blocks;
    for (const double b : windows(4))
        if (loudness(b) > -70.0)
            blocks.push_back(b);
    if (!blocks.empty())
    {
        double mean = 0.0;
        for (const double b : blocks)
            mean += b;
        const double gate = loudness(mean / static_cast<double>(blocks.size())) - 10.0;
        double sum = 0.0;
        int n = 0;
        for (const double b : blocks)
            if (loudness(b) > gate)
                sum += b, ++n;
        r.integrated = loudness(sum / n);
        r.maxMomentary = loudness(*std::max_element(blocks.begin(), blocks.end()));
    }
    std::vector<double> shortTerm;
    for (const double w : windows(30))
        if (loudness(w) > -70.0)
            shortTerm.push_back(loudness(w));
    if (!shortTerm.empty())
    {
        double power = 0.0;
        for (const double l : shortTerm)
            power += std::pow(10.0, l / 10.0);
        const double gate = 10.0 * std::log10(power / static_cast<double>(shortTerm.size())) - 20.0;
        std::vector<double> kept;
        for (const double l : shortTerm)
            if (l >= gate)
                kept.push_back(l);
        std::sort(kept.begin(), kept.end());
        const auto at = [&](double p)
        {
            return kept[static_cast<size_t>(std::lround((static_cast<double>(kept.size()) - 1) * p / 100.0))];
        };
        r.range = at(95.0) - at(10.0);
        r.maxShortTerm = *std::max_element(shortTerm.begin(), shortTerm.end());
    }
    for (size_t i = 0; i < steps.size(); ++i)
        if (steps[i] >= 0.0)
            r.truePeak = std::max(r.truePeak, peaks[i]);
    return r;
}

void checkAgainstReference(const LoudnessStore& store, const std::vector<double>& steps,
                           const std::vector<float>& peaks)
{
    const auto r = reference(steps, peaks);
    const auto same = [](float got, double want)
    {
        INFO("got " << got << ", want " << want);
        if (std::isnan(want))
            CHECK(std::isnan(got));
        else
            CHECK(got == Approx(want).margin(0.02)); // the histograms' 0.01 LU bins
    };
    same(store.integrated(), r.integrated);
    same(store.range(), r.range);
    same(store.maxMomentary(), r.maxMomentary);
    same(store.maxShortTerm(), r.maxShortTerm);
    CHECK(store.truePeakMax() == r.truePeak);
}
} // namespace

TEST_CASE("loudness store: every reading follows the steps, whichever order they come in and however often "
          "they're replaced",
          "[fast]")
{
    constexpr int length = 900; // 90 s
    LoudnessStore store;
    store.prepare(length);
    std::vector<double> steps(length, -1.0);
    std::vector<float> peaks(length, 0.0f);
    std::mt19937 random(7);
    // A song-like line: a quiet intro, a loud chorus, a fade; each step's level jitters.
    const auto levelAt = [](int i)
    {
        return i < 150 ? -32.0 : i < 450 ? -18.0 : i < 700 ? -12.0 : -12.0 - (i - 700) * 0.2;
    };
    std::normal_distribution<double> jitter(0.0, 1.5);
    const auto stepAt = [&](int i, double extra)
    {
        const double ms = std::pow(10.0, (levelAt(i) + extra + jitter(random) + 0.691) / 10.0);
        const float peak = static_cast<float>(std::pow(10.0, (levelAt(i) + 12.0 + extra) / 20.0));
        store.set(i, ms, peak);
        steps[static_cast<size_t>(i)] = static_cast<float>(ms); // as the store keeps it
        peaks[static_cast<size_t>(i)] = peak;
    };

    SECTION("in order, as Running counts")
    {
        for (int i = 0; i < length; ++i)
            stepAt(i, 0.0);
        checkAgainstReference(store, steps, peaks);
        CHECK(store.heardCount() == length);
        CHECK(store.extent() == length);
    }
    SECTION("jumping around, gaps left, parts replaced louder and quieter, as Song counts")
    {
        std::uniform_int_distribution<int> where(0, length - 1), run(1, 60);
        std::uniform_real_distribution<double> change(-6.0, 6.0);
        for (int pass = 0; pass < 120; ++pass)
        {
            const int from = where(random), n = run(random);
            const double extra = change(random);
            for (int i = from; i < std::min(length, from + n); ++i)
                stepAt(i, extra);
        }
        checkAgainstReference(store, steps, peaks);
        // The loudest step made quieter: the true-peak maximum finds the next.
        const auto loudest = std::max_element(peaks.begin(), peaks.end()) - peaks.begin();
        stepAt(static_cast<int>(loudest), -20.0);
        checkAgainstReference(store, steps, peaks);
    }
    SECTION("cleared: nothing, then counting again")
    {
        for (int i = 0; i < length; ++i)
            stepAt(i, 0.0);
        store.clear();
        CHECK(std::isnan(store.integrated()));
        CHECK(std::isnan(store.range()));
        CHECK(store.truePeakMax() == 0.0f);
        CHECK(store.extent() == 0);
        CHECK_FALSE(store.heard(10));
        std::fill(steps.begin(), steps.end(), -1.0);
        std::fill(peaks.begin(), peaks.end(), 0.0f);
        for (int i = 100; i < 200; ++i)
            stepAt(i, 0.0);
        checkAgainstReference(store, steps, peaks);
    }
}

TEST_CASE("loudness store: the history reads each moment's momentary and short-term loudness", "[fast]")
{
    LoudnessStore store;
    store.prepare(100);
    const double ms = std::pow(10.0, (-20.0 + 0.691) / 10.0); // -20 LUFS
    for (int i = 0; i < 40; ++i)
        store.set(i, ms, 0.5f);
    CHECK(std::isnan(store.momentaryAt(2))); // fewer than four steps before it
    CHECK(store.momentaryAt(3) == Approx(-20.0).margin(0.001));
    CHECK(std::isnan(store.shortTermAt(28))); // fewer than thirty
    CHECK(store.shortTermAt(39) == Approx(-20.0).margin(0.001));
    CHECK(std::isnan(store.momentaryAt(42))); // not heard
    CHECK(store.truePeakAt(5) == 0.5f);
    CHECK(store.truePeakAt(50) == 0.0f);
    // Outside the store: nothing, never a crash.
    CHECK(std::isnan(store.shortTermAt(-1)));
    CHECK(std::isnan(store.shortTermAt(1000)));
    store.set(1000, ms, 1.0f); // past its room: ignored
    CHECK(store.extent() == 40);
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

namespace
{
/// The largest true peak (dBTP) of a signal, as the meters read it: the larger of the sample peak
/// and the interpolated one.
double truePeakDb(const std::vector<float>& x)
{
    TruePeakDetector d;
    d.prepare(1);
    float peak = 0.0f;
    for (const float v : x)
        peak = std::max({peak, std::abs(v), d.processSample(0, v)});
    return gainToDb(peak);
}

/// A sine at `hz`, `amplitude` (fraction of full scale) and `degrees`, tapered in and out over
/// 10 ms (Tech 3341 cases 15-19).
std::vector<float> taperedSine(double rate, double hz, double amplitude, double degrees)
{
    const auto n = static_cast<size_t>(rate); // a second
    const auto taper = static_cast<size_t>(0.01 * rate);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double fade =
            std::min({1.0, static_cast<double>(i) / taper, static_cast<double>(n - 1 - i) / taper});
        x[i] = static_cast<float>(fade * amplitude * std::sin(twoPi * hz * i / rate + degrees * pi / 180.0));
    }
    return x;
}

/// Tech 3341 cases 20-23: a sine at rate/6, amplitude 0.5, holding one period of a sine at
/// rate/4, amplitude 1.0, made at four times the rate, low-passed and taken every fourth sample
/// from `offset`.
std::vector<float> onePeriodBurst(double rate, int offset)
{
    const double high = 4.0 * rate;
    std::vector<double> x;
    double phase = 0.0;
    const auto steps = static_cast<size_t>(high); // a second
    bool burstDone = false;
    for (size_t i = 0; i < steps; ++i)
    {
        x.push_back(0.5 * std::sin(phase));
        phase += twoPi * (rate / 6.0) / high;
        // At an upward zero crossing half way through: one period at rate/4, then on as before.
        if (!burstDone && i > steps / 2 && phase >= twoPi)
        {
            phase -= twoPi;
            for (int k = 0; k < 16; ++k) // a period at rate/4 is 16 samples at 4x
                x.push_back(std::sin(twoPi * k / 16.0));
            burstDone = true;
        }
        if (phase >= twoPi)
            phase -= twoPi;
    }
    // The anti-alias low-pass at the target rate's Nyquist: a long Kaiser-windowed sinc.
    constexpr int taps = 511;
    std::vector<double> h(taps);
    double sum = 0.0;
    for (int k = 0; k < taps; ++k)
    {
        const double t = (k - (taps - 1) / 2.0) / 4.0;
        const double w = 0.5 - 0.5 * std::cos(twoPi * k / (taps - 1)); // Hann
        h[static_cast<size_t>(k)] = (std::abs(t) < 1e-12 ? 1.0 : std::sin(pi * t) / (pi * t)) * w;
        sum += h[static_cast<size_t>(k)];
    }
    std::vector<float> out;
    for (size_t i = static_cast<size_t>(taps + offset); i + taps < x.size(); i += 4)
    {
        double y = 0.0;
        for (int k = 0; k < taps; ++k)
            y += h[static_cast<size_t>(k)] * x[i - static_cast<size_t>(k)];
        out.push_back(static_cast<float>(y / sum)); // unity gain at DC
    }
    return out;
}
} // namespace

TEST_CASE("true peak: EBU Tech 3341 cases 15 to 23, at 44.1, 48 and 96 kHz", "[fast]")
{
    // Each within +0.2 / -0.4 dB of the level the standard states.
    const auto within = [](double got, double want)
    {
        INFO("read " << got << " dBTP, want " << want);
        CHECK(got <= want + 0.2);
        CHECK(got >= want - 0.4);
    };
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        INFO(rate << " Hz");
        within(truePeakDb(taperedSine(rate, rate / 4.0, 0.5, 0.0)), -6.0);  // 15
        within(truePeakDb(taperedSine(rate, rate / 4.0, 0.5, 45.0)), -6.0); // 16
        within(truePeakDb(taperedSine(rate, rate / 6.0, 0.5, 60.0)), -6.0); // 17
        within(truePeakDb(taperedSine(rate, rate / 8.0, 0.5, 67.5)), -6.0); // 18
        within(truePeakDb(taperedSine(rate, rate / 4.0, 1.41, 45.0)), 3.0); // 19
        for (int offset = 0; offset < 4; ++offset)                          // 20 to 23
        {
            INFO("offset " << offset);
            within(truePeakDb(onePeriodBurst(rate, offset)), 0.0);
        }
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
