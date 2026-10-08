#include "fd/dsp/MatchedFilters.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;

double digitalDb(const BiquadCoeffs& c, double f)
{
    return 20.0 * std::log10(std::max(biquadMagnitude(c, f, sr), 1e-12));
}

double analogDb(const AnalogBiquad& p, double f, double fc)
{
    return 10.0 * std::log10(std::max(p.magnitudeSquared(f / fc), 1e-24));
}

} // namespace

TEST_CASE("matched designs meet the EQ accuracy envelope", "[fast]")
{
    // Measured envelope (EQ spec): worst |digital - analog| up to 20 kHz at 48 kHz.
    auto worstOver = [](MatchedType type, double fc, std::initializer_list<double> qs) {
        double worst = 0.0;
        for (double gain : {-12.0, -3.0, 6.0, 15.0})
            for (double q : qs)
            {
                const auto p = analogPrototype(type, gain, q);
                worst = std::max(worst, matchErrorDb(p, designMatched(p, fc, sr), fc, sr, 300));
            }
        return worst;
    };
    for (double fc : {100.0, 1000.0, 4000.0, 8000.0, 12000.0})
    {
        INFO("fc " << fc);
        CHECK(worstOver(MatchedType::Bell, fc, {0.3, 0.5, 0.71, 1.0, 2.0, 6.0}) < 0.8);
    }
    CHECK(worstOver(MatchedType::Bell, 16000.0, {0.3, 0.5, 0.71, 1.0, 2.0, 6.0}) < 1.6);
    for (auto type : {MatchedType::LowShelf, MatchedType::HighShelf, MatchedType::Tilt})
    {
        for (double fc : {100.0, 1000.0, 4000.0, 8000.0})
        {
            INFO("shelf type " << static_cast<int>(type) << " fc " << fc);
            CHECK(worstOver(type, fc, {0.3, 0.5, 0.71, 1.0}) < 0.7);
        }
        CHECK(worstOver(type, 12000.0, {0.3, 0.5, 0.71, 1.0}) < 2.0);
    }
}

TEST_CASE("matched designs are never worse than bilinear", "[fast]")
{
    for (auto type : {MatchedType::Bell, MatchedType::LowShelf, MatchedType::HighShelf, MatchedType::Lowpass,
                      MatchedType::Highpass, MatchedType::Bandpass, MatchedType::Tilt})
        for (double fc : {500.0, 6000.0, 15000.0, 21000.0})
            for (double q : {0.4, 1.0, 4.0})
            {
                const auto p = analogPrototype(type, 9.0, q);
                CHECK(matchErrorDb(p, designMatched(p, fc, sr), fc, sr) <=
                      matchErrorDb(p, designBilinear(p, fc, sr), fc, sr) + 1e-9);
            }
}

TEST_CASE("matched designs hit their defining gains exactly", "[fast]")
{
    CHECK(digitalDb(designMatched(MatchedType::Bell, 16000.0, 9.0, 1.0, sr), 16000.0) == Approx(9.0).margin(1e-6));
    CHECK(digitalDb(designMatched(MatchedType::Lowpass, 1000.0, 0.0, std::sqrt(0.5), sr), 1000.0) == Approx(-3.0103).margin(1e-3));
    CHECK(digitalDb(designMatched(MatchedType::LowShelf, 200.0, -6.0, 0.71, sr), 0.001) == Approx(-6.0).margin(1e-3));
    CHECK(digitalDb(designMatched(MatchedType::Tilt, 1000.0, 6.0, 0.71, sr), 1000.0) == Approx(0.0).margin(1e-3));
    CHECK(biquadMagnitude(designMatched(MatchedType::Notch, 3000.0, 0.0, 2.0, sr), 3000.0, sr) < 1e-6);
}

TEST_CASE("bilinear (RBJ) bells cramp near Nyquist where matched ones do not", "[fast]")
{
    // The reason for matched designs: a 16 kHz bell at 48 kHz.
    const auto proto = analogPrototype(MatchedType::Bell, 9.0, 1.0);
    const auto rbj = designBiquad(BiquadType::Peak, sr, 16000.0, 1.0, 9.0);
    const auto matched = designMatched(proto, 16000.0, sr);
    const double f = 20000.0;
    const double analog = analogDb(proto, f, 16000.0);
    CHECK(std::abs(digitalDb(rbj, f) - analog) > 1.0);
    CHECK(std::abs(digitalDb(matched, f) - analog) < 0.1);
}

TEST_CASE("matched filters are stable", "[fast]")
{
    for (auto type : {MatchedType::Bell, MatchedType::LowShelf, MatchedType::HighShelf, MatchedType::Lowpass,
                      MatchedType::Highpass, MatchedType::Bandpass, MatchedType::Notch, MatchedType::Tilt})
        for (double fc : {10.0, 1000.0, 23900.0})
            for (double q : {0.1, 1.0, 40.0})
            {
                const auto c = designMatched(type, fc, 12.0, q, sr);
                // Poles inside the unit circle: |a2| < 1 and |a1| < 1 + a2.
                CHECK(std::abs(c.a2) < 1.0);
                CHECK(std::abs(c.a1) < 1.0 + c.a2);
                CHECK(std::isfinite(c.b0));
            }
}

TEST_CASE("first-order and Butterworth cascades have the nominal slopes", "[fast]")
{
    // Asymptotic slope measured an octave pair well above the cutoff.
    auto slopeDbPerOct = [](auto&& magDb, double f) { return magDb(f) - magDb(2.0 * f); };

    const auto lp1 = designMatchedFirstOrder(false, 200.0, sr);
    CHECK(slopeDbPerOct([&](double f) { return digitalDb(lp1, f); }, 3200.0) == Approx(6.0).margin(0.5));

    for (int order : {2, 4, 6, 8, 12, 16})
    {
        double qs[8];
        const int n = butterworthQs(order, qs, 8);
        REQUIRE(n == order / 2);
        auto mag = [&](double f) {
            double db = 0.0;
            for (int i = 0; i < n; ++i)
                db += digitalDb(designMatched(MatchedType::Lowpass, 200.0, 0.0, qs[i], sr), f);
            return db;
        };
        INFO("order " << order);
        CHECK(mag(200.0) == Approx(-3.0103).margin(0.05));
        CHECK(slopeDbPerOct(mag, 1600.0) == Approx(6.0206 * order).margin(0.5));
    }
}
