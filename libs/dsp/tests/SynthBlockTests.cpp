#include "fd/dsp/SynthBlocks.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/Metrics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::dsp;
using namespace fd::testkit;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;

double svfMagDb(StateVariableFilter::Mode mode, double cutoff, double res, double f)
{
    StateVariableFilter svf;
    AudioBuffer ir(1, 16384);
    for (int i = 0; i < ir.numSamples(); ++i)
        ir.at(0, i) = svf.process(i == 0 ? 1.0f : 0.0f, cutoff, res, sr, mode);
    return 20.0 * std::log10(std::abs(responseAt(ir.channel(0), ir.numSamples(), f, sr)));
}
} // namespace

TEST_CASE("SVF responses at the cutoff", "[fast]")
{
    // resonance 0.0 -> Q 0.5; Q = 0.5 * 50^r, so r for Q = 0.707: log(1.414)/log(50).
    const double rButterworth = std::log(std::sqrt(2.0)) / std::log(50.0);
    CHECK(svfMagDb(StateVariableFilter::Mode::Lowpass, 1000.0, rButterworth, 1000.0) == Approx(-3.01).margin(0.05));
    CHECK(svfMagDb(StateVariableFilter::Mode::Highpass, 1000.0, rButterworth, 1000.0) == Approx(-3.01).margin(0.05));
    CHECK(svfMagDb(StateVariableFilter::Mode::Lowpass, 1000.0, rButterworth, 4000.0) < -22.0); // 12 dB/oct
    CHECK(svfMagDb(StateVariableFilter::Mode::Bandpass, 1000.0, 0.5, 1000.0) == Approx(20.0 * std::log10(0.5 * std::pow(50.0, 0.5))).margin(0.1));
}

TEST_CASE("ladder self-oscillates at the cutoff with bounded level", "[fast]")
{
    // Kick it with a short impulse, let it ring at full resonance, and measure the pitch.
    constexpr int size = 1 << 15;
    for (double cutoff : {220.0, 880.0, 3520.0})
    {
        LadderFilter lp;
        AudioBuffer out(1, 3 * size);
        for (int i = 0; i < out.numSamples(); ++i)
            out.at(0, i) = lp.process(i < 8 ? 0.5f : 0.0f, cutoff, 1.0, sr);
        CHECK(allFinite(out));
        CHECK(peak(out.slice(size, 2 * size)) > 0.1); // sustained
        CHECK(peak(out) < 4.0);                       // bounded
        // Pitch from the strongest spectral bin (parabolic interpolation).
        const auto mags = magnitudeSpectrum(out.slice(2 * size, size), 0, size);
        size_t k = 1;
        for (size_t i = 2; i + 1 < mags.size(); ++i)
            if (mags[i] > mags[k])
                k = i;
        const double a = mags[k - 1], b = mags[k], c = mags[k + 1];
        const double binF = (static_cast<double>(k) + 0.5 * (a - c) / (a - 2 * b + c)) * sr / size;
        INFO("cutoff " << cutoff << " oscillates at " << binF);
        CHECK(std::abs(1200.0 * std::log2(binF / cutoff)) < 25.0); // within a quarter semitone
    }
}

TEST_CASE("ADSR timing: attack to peak, release to -60 dB", "[fast]")
{
    Adsr env;
    env.setSampleRate(sr);
    env.set({.attack = 0.010, .decay = 0.2, .sustain = 0.5, .release = 0.100});
    env.noteOn();
    int samples = 0;
    while (env.stage() == Adsr::Stage::Attack)
    {
        env.next();
        ++samples;
    }
    CHECK(samples / sr == Approx(0.010).epsilon(0.05));
    for (int i = 0; i < 48000; ++i)
        env.next();
    CHECK(env.level() == Approx(0.5).margin(1e-4)); // sustained
    env.noteOff();
    samples = 0;
    while (env.level() > 0.5e-3) // -60 dB below the release start
    {
        env.next();
        ++samples;
    }
    CHECK(samples / sr == Approx(0.100).epsilon(0.05));
}

TEST_CASE("ADSR retriggers from its current level", "[fast]")
{
    Adsr env;
    env.setSampleRate(sr);
    env.set({.attack = 0.05, .decay = 0.1, .sustain = 0.6, .release = 0.5});
    env.noteOn();
    for (int i = 0; i < 2000; ++i)
        env.next();
    env.noteOff();
    for (int i = 0; i < 100; ++i)
        env.next();
    const double before = env.level();
    env.noteOn();
    const double after = env.next();
    CHECK(after >= before);
    CHECK(after - before < 0.01); // continuous
}

TEST_CASE("LFO waves and rate", "[fast]")
{
    Lfo lfo;
    lfo.reset();
    int crossings = 0;
    float prev = lfo.advance(5.0, sr, 1, Lfo::Wave::Sine); // the start is not a crossing
    for (int i = 0; i < 48000; ++i)
    {
        const float v = lfo.advance(5.0, sr, 1, Lfo::Wave::Sine);
        if (prev <= 0.0f && v > 0.0f)
            ++crossings;
        prev = v;
        CHECK(std::abs(v) <= 1.0f);
    }
    CHECK(crossings == 5);
}
