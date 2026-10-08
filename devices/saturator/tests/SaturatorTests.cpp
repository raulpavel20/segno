#include "Saturator.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using S = fd::devices::Saturator;

namespace
{
constexpr double sr = 48000.0;
constexpr int fftSize = 1 << 15;

/// Bin-centred stereo sine of `cycles` per FFT frame, two frames long.
AudioBuffer tone(int cycles, float amplitude)
{
    const double f = binCentredFrequency(cycles, fftSize, sr);
    return sine(f, sr, 2 * fftSize, amplitude, 2);
}

AudioBuffer renderWith(const AudioBuffer& in, std::initializer_list<std::pair<S::Param, float>> settings)
{
    S s;
    for (auto [p, v] : settings)
        s.params().set(p, v);
    return render(s, in, {.sampleRate = sr});
}

/// Analysis window: the second frame (steady state).
constexpr int kStart = fftSize;
} // namespace

TEST_CASE("Saturator at 0 dB drive is nearly transparent at moderate levels", "[fast]")
{
    const auto out = renderWith(tone(213, 0.1f), {});
    CHECK(thdPercent(out, 0, kStart, fftSize, 213) < 0.1);
}

TEST_CASE("Saturator dry path is exactly the input delayed by the latency", "[fast]")
{
    for (float quality : {0.0f, 1.0f, 2.0f})
    {
        S s;
        s.params().set(S::Mix, 0.0f);
        s.params().set(S::Drive, 24.0f);
        s.params().set(S::Quality, quality);
        const auto in = whiteNoise(24000, 4, 0.5f);
        const auto out = render(s, in, {.sampleRate = sr});
        const int l = s.latencySamples();
        AudioBuffer aligned(2, 24000 - l);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < aligned.numSamples(); ++i)
                aligned.at(c, i) = out.at(c, i + l);
        INFO("quality " << quality);
        CHECK(nullResidualDb(aligned, in.slice(0, aligned.numSamples())) == silenceDb);
    }
}

TEST_CASE("Saturator latency is constant across quality settings", "[fast]")
{
    S a, b;
    a.prepare({sr, 512});
    b.params().set(S::Quality, 2.0f);
    b.prepare({sr, 512});
    CHECK(a.latencySamples() == b.latencySamples());
    CHECK(a.latencySamples() == 73);
}

TEST_CASE("Saturator bias creates even harmonics without leaving DC", "[fast]")
{
    const auto out = renderWith(tone(213, 0.5f), {{S::Drive, 12.0f}, {S::Bias, 0.4f}});
    const auto h = harmonicLevelsDb(out, 0, kStart, fftSize, 213, 4);
    const auto symmetric = renderWith(tone(213, 0.5f), {{S::Drive, 12.0f}});
    const auto hs = harmonicLevelsDb(symmetric, 0, kStart, fftSize, 213, 4);
    CHECK(h[1] > -40.0);   // 2nd harmonic present with bias
    CHECK(hs[1] < -100.0); // and absent without
    CHECK(std::abs(dcOffset(out.slice(kStart, fftSize), 0)) < 1e-4);
}

TEST_CASE("Saturator meets the aliasing target at default quality", "[fast]")
{
    for (float curve = 0.0f; curve <= 5.0f; curve += 1.0f)
    {
        // Spec: 4x, +12 dB drive, ~2.5 kHz (1707 cycles), aliasing in the audible band < -75 dB.
        const auto out = renderWith(tone(1707, 0.5f), {{S::Drive, 12.0f}, {S::Curve, curve}});
        INFO("curve " << curve);
        CHECK(aliasingRatioDb(out, 0, kStart, fftSize, 1707, sr) < -75.0);
    }
}

TEST_CASE("Saturator harmonic profile per curve matches golden metrics", "[fast]")
{
    nlohmann::json metrics;
    const char* names[] = {"soft", "warm", "tape", "hard", "fold", "rectify"};
    for (int curve = 0; curve < 6; ++curve)
    {
        const auto out = renderWith(tone(213, 0.5f), {{S::Drive, 12.0f}, {S::Curve, static_cast<float>(curve)}});
        const auto h = harmonicLevelsDb(out, 0, kStart, fftSize, 213, 9);
        for (int k = 0; k < 9; ++k)
            metrics[std::string(names[curve]) + ".h" + std::to_string(k + 1) + "_db"] = std::max(h[static_cast<size_t>(k)], -140.0);
    }
    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/saturator/harmonics.json", metrics, 0.5);
    INFO(result.report);
    CHECK(result.passed);
}

TEST_CASE("Saturator transfer curve matches the shaper used for audio", "[fast]")
{
    CHECK(S::transfer(dsp::ShaperCurve::Soft, 0.0f, 0.0f, 0.5f) == Approx(std::tanh(0.5f)));
    CHECK(S::transfer(dsp::ShaperCurve::Hard, 12.0f, 0.0f, 0.5f) == Approx(1.0f));
    CHECK(S::transfer(dsp::ShaperCurve::Tape, 0.0f, 0.5f, 0.0f) == Approx(0.5f / 1.5f));
}

TEST_CASE("Saturator with bias keeps silence silent and starts without a thump", "[fast]")
{
    for (float curve = 0.0f; curve <= 5.0f; curve += 1.0f)
    {
        S s;
        s.params().set(S::Bias, -1.0f);
        s.params().set(S::Drive, 12.0f);
        s.params().set(S::Curve, curve);
        const auto out = render(s, silence(9600), {.sampleRate = sr});
        INFO("curve " << curve);
        CHECK(peakDb(out) < -100.0);
    }
}
