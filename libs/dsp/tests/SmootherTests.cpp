#include "fd/dsp/Smoother.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::dsp;
using Catch::Approx;

TEST_CASE("linear smoother ramps exactly to the target", "[fast]")
{
    LinearSmoother s;
    s.reset(0.0f);
    s.setTarget(1.0f, 4);
    CHECK(s.next() == Approx(0.25f));
    CHECK(s.next() == Approx(0.5f));
    CHECK(s.next() == Approx(0.75f));
    CHECK(s.next() == 1.0f); // snaps exactly
    CHECK_FALSE(s.isSmoothing());
    CHECK(s.next() == 1.0f);
}

TEST_CASE("linear smoother with zero ramp jumps", "[fast]")
{
    LinearSmoother s;
    s.reset(2.0f);
    s.setTarget(5.0f, 0);
    CHECK(s.current() == 5.0f);
    CHECK(s.next() == 5.0f);
}

TEST_CASE("retargeting mid-ramp continues from the current value", "[fast]")
{
    LinearSmoother s;
    s.reset(0.0f);
    s.setTarget(1.0f, 10);
    for (int i = 0; i < 5; ++i)
        s.next();
    const float mid = s.current();
    s.setTarget(0.0f, 5);
    CHECK(s.next() == Approx(mid - mid / 5.0f));
}

TEST_CASE("multiplicative smoother passes the geometric midpoint", "[fast]")
{
    MultiplicativeSmoother s;
    s.reset(100.0f);
    s.setTarget(10000.0f, 2);
    CHECK(s.next() == Approx(1000.0f).epsilon(1e-4));
    CHECK(s.next() == 10000.0f);
}

TEST_CASE("glide: starts and ends with no step in its speed, and settles", "[fast]")
{
    Glide g;
    g.setTime(12.0, 48000.0);
    g.snap(0.0);
    g.setTarget(1.0);
    const double first = g.next();
    const double second = g.next();
    CHECK(first < 1e-5);           // it barely moves at first...
    CHECK(second - first > first); // ...then speeds up
    for (int i = 0; i < 48000 / 10; ++i)
        (void)g.next(); // 100 ms: over eight time constants
    CHECK(g.current() == Approx(1.0).margin(1e-2));
    CHECK(g.target() == 1.0);
}
