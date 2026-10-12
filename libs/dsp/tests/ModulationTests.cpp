// The modulation core (M3 A2): the delay line read between samples and while moving, the
// feedback path, and the LFOs free and locked to the song.
#include "fd/dsp/Math.h"
#include "fd/dsp/Modulation.h"
#include "fd/testkit/Allocations.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
double db(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-30));
}
} // namespace

TEST_CASE("modulated delay: whole-sample delays are exact copies", "[fast]")
{
    ModulatedDelay d;
    d.prepare(2000.0);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> x(4000);
    for (auto& v : x)
        v = noise(rng);
    for (const int delay : {ModulatedDelay::minimumDelay, 17, 100, 1999})
    {
        d.reset();
        for (size_t n = 0; n < x.size(); ++n)
        {
            d.push(x[n]);
            const float expected = n >= static_cast<size_t>(delay) ? x[n - static_cast<size_t>(delay)] : 0.0f;
            REQUIRE(d.read(delay) == expected);
        }
    }
    // Shorter than the kernel allows, or longer than prepared: clamped.
    d.reset();
    for (int n = 0; n < 100; ++n)
        d.push(static_cast<float>(n));
    CHECK(d.read(2.0) == d.read(ModulatedDelay::minimumDelay));
    CHECK(d.read(5000.0) == d.read(2000.0));
}

TEST_CASE("modulated delay: between samples, flat within 0.1 dB to 15 kHz and delayed exactly", "[fast]")
{
    for (const double rate : {44100.0, 48000.0})
        for (const double delay : {40.0, 40.125, 40.25, 40.5, 40.73, 40.999})
            for (const double freq : {100.0, 1000.0, 5000.0, 10000.0, 15000.0})
            {
                INFO(rate << " Hz, delay " << delay << ", " << freq << " Hz");
                ModulatedDelay d;
                d.prepare(100.0);
                const double w = 2.0 * pi * freq / rate;
                double err = 0.0, sig = 0.0;
                for (int n = 0; n < 4000; ++n)
                {
                    d.push(static_cast<float>(std::sin(w * n)));
                    if (n < 200)
                        continue;
                    const double y = d.read(delay);
                    const double ideal = std::sin(w * (n - delay));
                    // The gain within 0.1 dB, and the phase the delay's: compare with the ideal.
                    err += (y - ideal) * (y - ideal);
                    sig += ideal * ideal;
                }
                // A deviation of 0.1 dB in gain alone would be -38.8 dB of error.
                CHECK(db(std::sqrt(err / sig)) < (freq <= 10000.0 ? -90.0 : -38.8));
            }
}

TEST_CASE("modulated delay: a moving delay leaves no zipper (below -100 dB)", "[fast]")
{
    const double rate = 48000.0, freq = 1000.0;
    ModulatedDelay d;
    d.prepare(rate * 0.05);
    const double w = 2.0 * pi * freq / rate;
    double err = 0.0, sig = 0.0;
    for (int n = 0; n < static_cast<int>(rate * 2.0); ++n)
    {
        d.push(static_cast<float>(std::sin(w * n)));
        // A chorus's sweep: 10 ms +- 3 ms at 0.7 Hz, a new position every sample.
        const double delay = rate * (0.010 + 0.003 * std::sin(2.0 * pi * 0.7 * n / rate));
        if (n < 1000)
            continue;
        const double y = d.read(delay);
        const double ideal = std::sin(w * (n - delay));
        err += (y - ideal) * (y - ideal);
        sig += ideal * ideal;
    }
    CHECK(db(std::sqrt(err / sig)) < -100.0);
}

TEST_CASE("feedback path: filters as drawn, saturation bounded at any feedback", "[fast]")
{
    const double rate = 48000.0;
    FeedbackPath f;
    f.prepare(rate);
    f.set(200.0, 4000.0, 0.0);
    // Small signals: the filters' magnitude, as the panel draws it.
    for (const double freq : {100.0, 200.0, 1000.0, 4000.0, 8000.0})
    {
        f.reset();
        double in = 0.0, out = 0.0;
        for (int n = 0; n < 48000; ++n)
        {
            const float x = 0.1f * static_cast<float>(std::sin(2.0 * pi * freq * n / rate));
            const float y = f.process(x);
            if (n > 24000)
            {
                in += x * x;
                out += y * y;
            }
        }
        INFO(freq);
        CHECK(db(std::sqrt(out / in)) ==
              Approx(db(FeedbackPath::magnitude(freq, 200.0, 4000.0, rate))).margin(0.05));
    }
    CHECK(FeedbackPath::magnitude(1000.0, 0.0, 0.0, rate) == 1.0);

    // A loop with more than unity feedback stays bounded with saturation; small signals pass as
    // they are.
    f.set(0.0, 0.0, 1.0);
    f.reset();
    CHECK(f.process(0.001f) == Approx(0.001f).margin(1e-5f));
    ModulatedDelay d;
    d.prepare(4800.0);
    float peak = 0.0f;
    for (int n = 0; n < 48000 * 10; ++n)
    {
        const float x = n < 480 ? 0.5f : 0.0f;
        const float y = d.read(4800.0);
        d.push(x + f.process(1.5f * y));
        peak = std::max(peak, std::abs(y));
        REQUIRE(std::isfinite(y));
    }
    CHECK(peak < 2.0f);
    CHECK(peak > 0.5f); // it sustains rather than dying away
}

TEST_CASE("LFO: shapes, a free rate, the stereo offset", "[fast]")
{
    using S = ModulationLfo::Shape;
    CHECK(ModulationLfo::value(S::Sine, 0.25) == Approx(1.0f));
    CHECK(ModulationLfo::value(S::Triangle, 0.25) == Approx(1.0f));
    CHECK(ModulationLfo::value(S::Triangle, 0.75) == Approx(-1.0f));
    CHECK(ModulationLfo::value(S::Triangle, 0.5) == Approx(0.0f).margin(1e-6));
    CHECK(ModulationLfo::value(S::SawUp, 0.0) == Approx(-1.0f));
    CHECK(ModulationLfo::value(S::SawDown, 0.0) == Approx(1.0f));
    CHECK(ModulationLfo::value(S::Square, 0.49) == 1.0f);
    CHECK(ModulationLfo::value(S::Square, 0.51) == -1.0f);
    for (const auto shape :
         {S::Sine, S::Triangle, S::SawUp, S::SawDown, S::Square, S::SampleAndHold, S::Wander})
        for (double p = 0.0; p < 20.0; p += 0.01)
        {
            const float v = ModulationLfo::value(shape, p);
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
        }
    // Sample-and-hold holds through a cycle; wander moves without steps.
    CHECK(ModulationLfo::value(S::SampleAndHold, 3.1) == ModulationLfo::value(S::SampleAndHold, 3.9));
    CHECK(ModulationLfo::value(S::SampleAndHold, 3.1) != ModulationLfo::value(S::SampleAndHold, 4.1));
    float last = ModulationLfo::value(S::Wander, 0.0);
    for (double p = 0.0005; p < 10.0; p += 0.0005)
    {
        const float v = ModulationLfo::value(S::Wander, p);
        REQUIRE(std::abs(v - last) < 0.01f);
        last = v;
    }

    // Free-running: exactly the rate; the right side leads by the offset.
    ModulationLfo lfo;
    lfo.prepare(48000.0);
    lfo.setShape(S::Sine);
    lfo.setRate(3.0);
    lfo.setStereo(0.25);
    for (int n = 0; n < 48000; ++n)
    {
        const auto v = lfo.next();
        const double p = 3.0 * n / 48000.0;
        REQUIRE(v.left == Approx(std::sin(2.0 * pi * p)).margin(1e-6));
        REQUIRE(v.right == Approx(std::sin(2.0 * pi * (p + 0.25))).margin(1e-6));
    }
    CHECK(lfo.position() == Approx(3.0).margin(1e-9));
}

TEST_CASE("LFO: synced, locked to the song's position, after a jump too", "[fast]")
{
    using S = ModulationLfo::Shape;
    const double rate = 48000.0, bpm = 120.0, beats = 2.0; // one cycle every two beats: 1 s
    for (const auto shape : {S::Sine, S::SampleAndHold, S::Wander})
    {
        ModulationLfo a;
        a.prepare(rate);
        a.setShape(shape);
        a.setOffset(0.1);
        // Played from the start: 512-sample blocks, each synced at its start.
        const int block = 512;
        double ppq = 0.0;
        std::vector<float> played;
        for (int b = 0; b < 400; ++b)
        {
            a.sync(ppq, bpm, beats, true);
            for (int i = 0; i < block; ++i)
                played.push_back(a.next().left);
            ppq += block * bpm / 60.0 / rate;
        }
        // Jumped to block 300's position: the same values from there.
        ModulationLfo b;
        b.prepare(rate);
        b.setShape(shape);
        b.setOffset(0.1);
        const double at = 300.0 * block * bpm / 60.0 / rate;
        b.sync(at, bpm, beats, true);
        for (int i = 0; i < block; ++i)
            REQUIRE(b.next().left == Approx(played[static_cast<size_t>(300 * block + i)]).margin(1e-5));
        CHECK(b.position() == Approx((at + block * bpm / 60.0 / rate) / beats + 0.1).margin(1e-6));
    }

    // Stopped, it runs on at the tempo's rate from where it was.
    ModulationLfo lfo;
    lfo.prepare(rate);
    lfo.sync(4.0, bpm, beats, true);
    const double start = lfo.position();
    lfo.sync(99.0, bpm, beats, false); // not playing: the position isn't taken
    for (int n = 0; n < 24000; ++n)
        (void)lfo.next();
    CHECK(lfo.position() == Approx(start + 0.5).margin(1e-9));
}

TEST_CASE("song LFO: the same however the blocks fall; after a jump, it glides to the song", "[fast]")
{
    const double rate = 48000.0, bpm = 120.0, beats = 1.0; // a cycle a beat: two a second
    // Plays `total` samples in blocks of `block`, set() on 32-sample ticks; the host's position at
    // each block's start is `ppqAt(sample)`.
    const auto play = [&](int block, int total, const auto& ppqAt)
    {
        SongLfo lfo;
        lfo.prepare(rate);
        std::vector<double> out;
        for (int start = 0; start < total; start += block)
        {
            lfo.beginBlock(true, ppqAt(start), bpm);
            for (int i = start; i < std::min(total, start + block); ++i)
            {
                if (i % 32 == 0)
                    lfo.set(1.0, beats, i == 0);
                out.push_back(lfo.next());
            }
        }
        return out;
    };
    const auto song = [&](int sample) { return sample * bpm / 60.0 / rate; };
    const auto a = play(512, 48000, song);
    const auto b = play(37, 48000, song);
    REQUIRE(a.size() == b.size());
    CHECK(a == b);                                               // bit for bit
    CHECK(a.back() == Approx(song(47999) / beats).margin(1e-9)); // locked to the song

    // The song jumps by 0.3 of a beat at one second: no step there, the song's place soon after.
    const auto jumped =
        play(512, 72000, [&](int sample) { return song(sample) + (sample >= 48128 ? 0.3 : 0.0); });
    const double before = jumped[48127], after = jumped[48128];
    CHECK(std::abs(after - before) < 1e-3); // a sample's movement, not the jump
    CHECK(jumped.back() == Approx(song(71999) + 0.3).margin(1e-4));
}

TEST_CASE("modulation core: nothing allocates while it runs", "[fast]")
{
    if (!fd::testkit::allocationTrackingEnabled())
        SKIP("allocation tracking is off in sanitizer builds");
    ModulatedDelay d;
    d.prepare(48000.0);
    FeedbackPath f;
    f.prepare(48000.0);
    ModulationLfo lfo;
    lfo.prepare(48000.0);
    lfo.setShape(ModulationLfo::Shape::Wander);
    const auto before = fd::testkit::allocationsDuringProcess();
    {
        const fd::testkit::CountAllocations counting;
        f.set(100.0, 8000.0, 0.5);
        for (int n = 0; n < 10000; ++n)
        {
            lfo.sync(n / 1000.0, 120.0, 1.0, true);
            const auto v = lfo.next();
            const float y = d.read(1000.0 + 500.0 * v.left);
            d.push(f.process(0.5f * y) + 0.1f);
        }
    }
    CHECK(fd::testkit::allocationsDuringProcess() - before == 0);
}
