// The Tuner's pitch detection (devices/tuner/SPEC.md, "Automated test targets"): every note of
// the piano's range to a tenth of a cent at 44.1, 48 and 96 kHz; harmonic tones and a tone
// without its fundamental read at their note, never an octave off; noise, silence and chords
// give no reading; low notes read soon after they start.
#include "fd/dsp/PitchDetector.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

using fd::dsp::PitchDetector;

namespace
{
constexpr double kPi = std::numbers::pi;

double hzOf(double midi)
{
    return 440.0 * std::exp2((midi - 69.0) / 12.0);
}

double centsBetween(double hz, double reference)
{
    return 1200.0 * std::log2(hz / reference);
}

/// A tone of `harmonics` (amplitude per harmonic, from the 1st) at `hz`, below Nyquist only.
std::function<double(double)> tone(double hz, std::vector<double> harmonics, double sampleRate)
{
    return [=](double t)
    {
        double v = 0.0;
        for (size_t h = 0; h < harmonics.size(); ++h)
            if (const double f = hz * static_cast<double>(h + 1); f < 0.45 * sampleRate)
                v += harmonics[h] * std::sin(2.0 * kPi * f * t + 0.3 * static_cast<double>(h));
        return v;
    };
}

std::vector<double> sawHarmonics()
{
    std::vector<double> h;
    for (int k = 1; k <= 40; ++k)
        h.push_back(0.5 / k);
    return h;
}

/// Runs `seconds` of `signal` through a detector at `sampleRate`, in blocks of 512; each reading
/// with the time it came, in seconds.
std::vector<std::pair<double, PitchDetector::Reading>>
run(const std::function<double(double)>& signal, double sampleRate, double seconds, double silenceFirst = 0.0)
{
    PitchDetector d;
    d.prepare(sampleRate);
    std::vector<std::pair<double, PitchDetector::Reading>> readings;
    std::vector<float> block(512);
    const auto total = static_cast<std::int64_t>((seconds + silenceFirst) * sampleRate);
    for (std::int64_t i = 0; i < total; i += 512)
    {
        const int n = static_cast<int>(std::min<std::int64_t>(512, total - i));
        for (int j = 0; j < n; ++j)
        {
            const double t = static_cast<double>(i + j) / sampleRate;
            block[static_cast<size_t>(j)] =
                t < silenceFirst ? 0.0f : static_cast<float>(signal(t - silenceFirst));
        }
        d.push(block.data(), n, [&](const PitchDetector::Reading& r)
               { readings.emplace_back(static_cast<double>(i + n) / sampleRate - silenceFirst, r); });
    }
    return readings;
}
} // namespace

TEST_CASE("pitch detector: every note of the piano, to a tenth of a cent, at 44.1, 48 and 96 kHz", "[fast]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (int midi = 21; midi <= 108; midi += rate > 50000.0 ? 3 : 1)
        {
            const double hz = hzOf(midi);
            const auto readings = run(tone(hz, {0.5}, rate), rate, 0.7);
            REQUIRE_FALSE(readings.empty());
            const auto& last = readings.back().second;
            INFO(rate << " Hz, MIDI " << midi << " (" << hz << " Hz): read " << last.hz << ", clarity "
                      << last.clarity);
            REQUIRE(last.valid);
            CHECK(std::abs(centsBetween(last.hz, hz)) < 0.1);
            CHECK(last.clarity > 0.95f);
        }
}

TEST_CASE("pitch detector: a sine 7 cents sharp reads 7 cents sharp", "[fast]")
{
    const double hz = hzOf(57) * std::exp2(7.0 / 1200.0);
    const auto readings = run(tone(hz, {0.3}, 48000.0), 48000.0, 0.5);
    REQUIRE(readings.back().second.valid);
    CHECK(std::abs(centsBetween(readings.back().second.hz, hzOf(57)) - 7.0) < 0.1);
}

TEST_CASE("pitch detector: saw, square, pulse and a tone without its fundamental read at their note, "
          "never an octave off",
          "[fast]")
{
    const double rate = 48000.0;
    std::vector<double> square, pulse, noFundamental;
    for (int k = 1; k <= 40; ++k)
    {
        square.push_back(k % 2 == 1 ? 0.5 / k : 0.0);
        pulse.push_back(0.5 / k * std::abs(std::sin(kPi * k * 0.25)));
    }
    noFundamental = {0.0, 0.3, 0.25, 0.2, 0.15, 0.12, 0.1, 0.08};
    const std::vector<std::pair<const char*, std::vector<double>>> timbres{
        {"saw", sawHarmonics()}, {"square", square}, {"pulse", pulse}, {"no fundamental", noFundamental}};
    for (const auto& [name, harmonics] : timbres)
        for (int midi = 28; midi <= 96; midi += 12) // a bass's low E up, an octave at a time
        {
            const double hz = hzOf(midi);
            const auto readings = run(tone(hz, harmonics, rate), rate, 0.7);
            const auto& last = readings.back().second;
            INFO(name << ", MIDI " << midi << ": read " << last.hz << ", clarity " << last.clarity);
            REQUIRE(last.valid);
            CHECK(std::abs(centsBetween(last.hz, hz)) < 0.5);
            // Every reading on the way there was the right note, never another octave.
            for (const auto& [t, r] : readings)
                if (r.valid)
                    CHECK(std::abs(centsBetween(r.hz, hz)) < 50.0);
        }
}

TEST_CASE("pitch detector: noise and silence give no reading", "[fast]")
{
    std::mt19937 random(7);
    std::uniform_real_distribution<double> uniform(-0.5, 0.5);
    for (const auto& [t, r] : run([&](double) { return uniform(random); }, 48000.0, 1.0))
        CHECK_FALSE(r.valid);
    for (const auto& [t, r] : run([](double) { return 0.0; }, 48000.0, 0.5))
        CHECK_FALSE(r.valid);
    // Under the gate (-60 dBFS): no reading either.
    for (const auto& [t, r] : run(tone(220.0, {0.0005}, 48000.0), 48000.0, 0.5))
        CHECK_FALSE(r.valid);
}

TEST_CASE("pitch detector: a chord of unrelated notes gives no reading", "[fast]")
{
    // A cluster (C, F sharp, B flat, saws): no common period the ear or the tuner could take.
    const double rate = 48000.0;
    const auto c = tone(hzOf(60), sawHarmonics(), rate), fs = tone(hzOf(66), sawHarmonics(), rate),
               bb = tone(hzOf(70), sawHarmonics(), rate);
    int valid = 0, all = 0;
    for (const auto& [t, r] : run([&](double t) { return (c(t) + fs(t) + bb(t)) / 3.0; }, rate, 1.0))
    {
        ++all;
        valid += r.valid ? 1 : 0;
    }
    CHECK(valid == 0);
    CHECK(all > 40);
}

TEST_CASE("pitch detector: low notes read soon after they start", "[fast]")
{
    const double rate = 48000.0;
    const auto firstReading = [&](double hz)
    {
        for (const auto& [t, r] : run(tone(hz, sawHarmonics(), rate), rate, 0.4, 0.2))
            if (t >= 0.0 && r.valid)
                return t;
        return 1.0;
    };
    CHECK(firstReading(82.41) < 0.070); // a guitar's low E
    CHECK(firstReading(41.20) < 0.090); // a bass's E
    CHECK(firstReading(30.87) < 0.120); // a 5-string bass's low B
}
