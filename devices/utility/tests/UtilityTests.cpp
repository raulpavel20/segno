#include "Utility.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using U = fd::devices::Utility;

namespace
{
constexpr double sr = 48000.0;

/// Renders 1 s of `input` with the given parameters set before processing starts.
AudioBuffer renderWith(const AudioBuffer& input, std::initializer_list<std::pair<U::Param, float>> settings)
{
    U u;
    for (auto [p, v] : settings)
        u.params().set(p, v);
    return render(u, input, {.sampleRate = sr});
}

/// The last half second (after every smoother has settled).
AudioBuffer settled(const AudioBuffer& b)
{
    return b.slice(b.numSamples() / 2, b.numSamples() / 2);
}
} // namespace

TEST_CASE("Utility defaults are transparent", "[fast]")
{
    const auto in = stereoSines(440, 660, sr, 48000);
    CHECK(nullResidualDb(renderWith(in, {}), in) == silenceDb);
}

TEST_CASE("Utility gain", "[fast]")
{
    const auto in = sine(1000, sr, 48000, 0.5f);
    CHECK(peak(settled(renderWith(in, {{U::Gain, -6.0206f}}))) == Approx(0.25).epsilon(1e-4));
    CHECK(peak(settled(renderWith(in, {{U::Gain, 6.0206f}}))) == Approx(1.0).epsilon(1e-4));
    CHECK(peak(renderWith(in, {{U::Gain, -70.0f}})) == 0.0); // -inf
}

TEST_CASE("Utility mono and zero width collapse to L == R", "[fast]")
{
    const auto in = stereoSines(440, 660, sr, 48000);
    for (auto out : {settled(renderWith(in, {{U::Mono, 1.0f}})), settled(renderWith(in, {{U::Width, 0.0f}}))})
    {
        AudioBuffer l(1, out.numSamples()), r(1, out.numSamples());
        for (int i = 0; i < out.numSamples(); ++i)
        {
            l.at(0, i) = out.at(0, i);
            r.at(0, i) = out.at(1, i);
        }
        CHECK(bitIdentical(l, r));
    }
}

TEST_CASE("Utility polarity inversion", "[fast]")
{
    const auto in = stereoSines(440, 660, sr, 48000);
    const auto out = settled(renderWith(in, {{U::InvertL, 1.0f}}));
    const auto ref = settled(in);
    for (int i = 0; i < out.numSamples(); i += 97)
    {
        CHECK(out.at(0, i) == -ref.at(0, i));
        CHECK(out.at(1, i) == ref.at(1, i));
    }
}

TEST_CASE("Utility balance attenuates the opposite side", "[fast]")
{
    const auto in = stereoSines(440, 660, sr, 48000);
    const auto right = settled(renderWith(in, {{U::Balance, 1.0f}}));
    CHECK(peak(right, 0) == 0.0);
    CHECK(peak(right, 1) == Approx(peak(settled(in), 1)));
    const auto halfLeft = settled(renderWith(in, {{U::Balance, -0.5f}}));
    CHECK(peak(halfLeft, 1) == Approx(0.5 * peak(settled(in), 1)).epsilon(1e-4));
}

TEST_CASE("Utility DC filter removes DC", "[fast]")
{
    const auto in = dc(48000, 0.5f);
    const auto out = renderWith(in, {{U::DcFilter, 1.0f}});
    CHECK(std::abs(out.at(0, out.numSamples() - 1)) < 1e-3);
}

TEST_CASE("Utility mute silences after the ramp", "[fast]")
{
    const auto out = renderWith(sine(1000, sr, 48000), {{U::Mute, 1.0f}});
    CHECK(peak(out) == 0.0);

    // Muting mid-stream ramps down instead of clicking.
    U u;
    const auto in = sine(1000, sr, 48000);
    const auto ramped = render(u, in, {.sampleRate = sr, .beforeBlock = [](Device& d, std::int64_t start) {
                                           if (start >= 24000)
                                               d.params().set(U::Mute, 1.0f);
                                       }});
    CHECK(peak(ramped.slice(24576 + 1000, 20000)) == 0.0);
    CHECK(clickScore(ramped, 0) < 8.0);
}

TEST_CASE("Utility publishes output peaks", "[fast]")
{
    U u;
    render(u, stereoSines(440, 660, sr, 4800, 0.5f), {.sampleRate = sr});
    CHECK(u.analysis().takePeak(U::PeakOutL) == Approx(0.5f).margin(1e-3));
    CHECK(u.analysis().takePeak(U::PeakOutL) == 0.0f); // taken
}
