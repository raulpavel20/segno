// The modulators' signals (docs/design/modulators.md §12): pure functions of where a modulator is.
#include "fd/dsp/Modulators.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using Catch::Approx;
namespace mod = fd::dsp::mod;

TEST_CASE("modulators: Sync's note values in beats", "[fast][modulators]")
{
    CHECK(mod::syncBeats(0, 4.0) == 0.0);           // Off
    CHECK(mod::syncBeats(11, 4.0) == Approx(1.0));  // 1/4
    CHECK(mod::syncBeats(7, 4.0) == Approx(0.375)); // 1/16D
    CHECK(mod::syncBeats(16, 4.0) == Approx(4.0));  // 1 bar of 4/4
    CHECK(mod::syncBeats(16, 3.0) == Approx(3.0));  // 1 bar of 3/4
    CHECK(mod::syncBeats(19, 4.0) == Approx(32.0)); // 8 bars
    for (int i = 2; i < mod::numSyncValues; ++i)
        CHECK(mod::syncBeats(i, 4.0) > mod::syncBeats(i - 1, 4.0)); // fastest to slowest
}

TEST_CASE("modulators: the dice depend only on their inputs", "[fast][modulators]")
{
    CHECK(mod::dice(1, 2, 3) == mod::dice(1, 2, 3));
    CHECK(mod::dice(1, 2, 3) != mod::dice(1, 2, 4));
    CHECK(mod::dice(1, 2, 3) != mod::dice(2, 2, 3));
    double sum = 0.0;
    for (std::uint64_t i = 0; i < 4000; ++i)
    {
        const float d = mod::dice(7, 0, i);
        REQUIRE(d >= -1.0f);
        REQUIRE(d < 1.0f);
        sum += d;
    }
    CHECK(std::abs(sum / 4000.0) < 0.05); // centred
}

TEST_CASE("modulators: the LFO's shapes, at known points", "[fast][modulators]")
{
    using S = mod::Shape;
    CHECK(mod::lfo(S::Sine, 0.0, 0.0, 0.0) == Approx(0.0).margin(1e-6));
    CHECK(mod::lfo(S::Sine, 0.25, 0.0, 0.0) == Approx(1.0));
    CHECK(mod::lfo(S::Triangle, 0.25, 0.0, 0.0) == Approx(1.0));
    CHECK(mod::lfo(S::Triangle, 0.75, 0.0, 0.0) == Approx(-1.0));
    CHECK(mod::lfo(S::SawUp, 0.0, 0.0, 0.0) == Approx(-1.0));
    CHECK(mod::lfo(S::SawUp, 0.5, 0.0, 0.0) == Approx(0.0).margin(1e-6));
    CHECK(mod::lfo(S::SawDown, 0.0, 0.0, 0.0) == Approx(1.0));
    CHECK(mod::lfo(S::Square, 0.2, 0.0, 0.0) == Approx(1.0));
    CHECK(mod::lfo(S::Square, 0.7, 0.0, 0.0) == Approx(-1.0));
    // Whole cycles later, the same.
    CHECK(mod::lfo(S::Triangle, 3.25, 0.0, 0.0) == Approx(1.0));
    CHECK(mod::lfo(S::Triangle, -0.75, 0.0, 0.0) == Approx(1.0));
    // A drawn curve, 0..1, plays as -1 ... +1; without one, a sine.
    fd::dsp::CurveTable ramp{};
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i) / static_cast<float>(ramp.size() - 1);
    CHECK(mod::lfo(S::Drawn, 0.5, 0.0, 0.0, &ramp) == Approx(0.0).margin(0.01));
    CHECK(mod::lfo(S::Drawn, 0.25, 0.0, 0.0) == Approx(1.0));
}

TEST_CASE("modulators: Slant moves the cycle's middle; Smooth rounds corners", "[fast][modulators]")
{
    using S = mod::Shape;
    CHECK(mod::slanted(0.5, 0.0) == Approx(0.5));
    CHECK(mod::slanted(0.75, 0.5) == Approx(0.5)); // the middle at (1 + 0.5) / 2
    CHECK(mod::slanted(0.25, -0.5) == Approx(0.5));
    for (double s : {-0.9, -0.3, 0.0, 0.4, 0.9})
        for (int i = 1; i < 100; ++i)
            CHECK(mod::slanted(i / 100.0, s) > mod::slanted((i - 1) / 100.0, s)); // in order
    // A triangle's peak, a quarter of the way in, moves with the middle: half of (1 + slant) / 2.
    CHECK(mod::lfo(S::Triangle, 0.375, 0.5, 0.0) == Approx(1.0));
    CHECK(mod::lfo(S::Triangle, 0.125, -0.5, 0.0) == Approx(1.0));

    // A square's edge, smoothed, passes through the middle instead of jumping.
    CHECK(mod::lfo(S::Square, 0.5, 0.0, 0.5) == Approx(0.0).margin(0.05));
    CHECK(std::abs(mod::lfo(S::Square, 0.49, 0.0, 0.5)) < 0.5f);
    CHECK(mod::lfo(S::Square, 0.25, 0.0, 0.5) == Approx(1.0)); // the middle of a half stays put
    // Unsmoothed, it jumps.
    CHECK(mod::lfo(S::Square, 0.4999, 0.0, 0.0) == Approx(1.0));
}

TEST_CASE("modulators: Per Target's LFO copies spread evenly", "[fast][modulators]")
{
    CHECK(mod::spread(0, 1) == 0.0);
    CHECK(mod::spread(1, 2) == Approx(0.5));
    CHECK(mod::spread(3, 4) == Approx(0.75));
    CHECK(mod::spread(5, 4) == Approx(0.25)); // past the count, round again
}

TEST_CASE("modulators: Random, from a roll's dice", "[fast][modulators]")
{
    mod::Wander held{.smooth = 0.0};
    // Held: the same value through a step, another the next.
    CHECK(mod::random(3, 0, 5.1, held) == mod::random(3, 0, 5.9, held));
    CHECK(mod::random(3, 0, 5.5, held) != mod::random(3, 0, 6.5, held));
    // The same roll, the same values; another copy or roll, others.
    CHECK(mod::random(3, 0, 7.3, {}) == mod::random(3, 0, 7.3, {}));
    CHECK(mod::random(3, 1, 7.3, {}) != mod::random(3, 0, 7.3, {}));
    CHECK(mod::random(4, 0, 7.3, {}) != mod::random(3, 0, 7.3, {}));

    // Gliding the whole step: no jumps, and each step ends on its value.
    mod::Wander glide{.smooth = 1.0};
    float previous = mod::random(9, 0, 0.0, glide);
    float largest = 0.0f;
    for (int i = 1; i <= 4000; ++i)
    {
        const float v = mod::random(9, 0, i / 400.0, glide);
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
        largest = std::max(largest, std::abs(v - previous));
        previous = v;
    }
    CHECK(largest < 0.02f);
    CHECK(mod::random(9, 0, 3.9999, glide) == Approx(mod::random(9, 0, 3.5, held)).margin(0.001));

    // Cyclic: up and down in turn.
    mod::Wander turn{.smooth = 0.0, .cyclic = 1.0};
    for (int n = 0; n < 20; ++n)
    {
        const float here = mod::random(5, 0, n + 0.5, turn), next = mod::random(5, 0, n + 1.5, turn);
        CHECK(here * next < 0.0f);
    }

    // Slant -1: rises at once; it still glides down.
    mod::Wander quickRise{.smooth = 1.0, .slant = -1.0};
    for (int n = 1; n < 30; ++n)
    {
        const float from = mod::random(11, 0, n - 0.5, held), to = mod::random(11, 0, n + 0.5, held);
        const float early = mod::random(11, 0, n + 0.01, quickRise);
        if (to > from)
            CHECK(early == Approx(to));
        else
            CHECK(early > to);
    }
}

TEST_CASE("modulators: Jitter moves where steps start, in order", "[fast][modulators]")
{
    double within = 0.0;
    std::int64_t last = mod::stepAt(2, 0, 0.0, 1.0, within);
    int moved = 0;
    for (int i = 1; i <= 4000; ++i)
    {
        const double x = i / 100.0;
        const auto n = mod::stepAt(2, 0, x, 1.0, within);
        CHECK(n >= last); // never back
        CHECK(n <= last + 1);
        CHECK(within >= 0.0);
        CHECK(within <= 1.0);
        if (n != static_cast<std::int64_t>(std::floor(x)))
            ++moved;
        last = n;
    }
    CHECK(moved > 200); // the starts did move
    // Without jitter, steps start on whole numbers.
    CHECK(mod::stepAt(2, 0, 4.999, 0.0, within) == 4);
    CHECK(mod::stepAt(2, 0, 5.0, 0.0, within) == 5);
}

TEST_CASE("modulators: Steps play their values, their chances and glide", "[fast][modulators]")
{
    const std::array<float, 4> values{-1.0f, 0.5f, 1.0f, 0.0f};
    const std::array<float, 4> sure{1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(mod::steps(values, sure, 1, 0, 0.5, 0.0, 0.0) == Approx(-1.0));
    CHECK(mod::steps(values, sure, 1, 0, 2.5, 0.0, 0.0) == Approx(1.0));
    CHECK(mod::steps(values, sure, 1, 0, 6.5, 0.0, 0.0) == Approx(1.0));  // it repeats
    CHECK(mod::steps(values, sure, 1, 0, -1.5, 0.0, 0.0) == Approx(1.0)); // and before its start
    // Glide: half way through gliding into step 2, between step 1's value and step 2's.
    const float half = mod::steps(values, sure, 1, 0, 2.25, 0.5, 0.0);
    CHECK(half == Approx(0.75).margin(0.01));
    CHECK(mod::steps(values, sure, 1, 0, 2.75, 0.5, 0.0) == Approx(1.0));

    // A step at no chance sits at 0; at half, about half the time, each copy its own.
    const std::array<float, 4> never{1.0f, 0.0f, 1.0f, 1.0f};
    CHECK(mod::steps(values, never, 1, 0, 1.5, 0.0, 0.0) == Approx(0.0));
    int played = 0, differ = 0;
    for (std::int64_t n = 0; n < 2000; ++n)
    {
        const bool a = mod::stepPlays(8, 0, n, 0.5), b = mod::stepPlays(8, 1, n, 0.5);
        played += a ? 1 : 0;
        differ += a != b ? 1 : 0;
        CHECK(mod::stepPlays(8, 0, n, 0.5) == a); // the same roll, the same fate
    }
    CHECK(played > 900);
    CHECK(played < 1100);
    CHECK(differ > 800);
}
