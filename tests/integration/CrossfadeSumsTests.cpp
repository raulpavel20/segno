// The sums the arrangement draws across crossfades (C6, fades.md §3): once a join's audio is read,
// a crossfade being dragged gets its sum at once on every step, and a step past what was read keeps
// the last sum, where it was made, until the wider read arrives.

#include "fd/audiofile/AudioFile.h"
#include "ui/CrossfadeSums.h"

#include <juce_events/juce_events.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

using Catch::Approx;

TEST_CASE("crossfade sums: each step of a drag at once, the last sum while a wider read loads", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<std::vector<float>> ch(1, std::vector<float>(30 * 48000));
    for (size_t i = 0; i < ch[0].size(); ++i)
        ch[0][i] = 0.5f * std::sin(0.01f * static_cast<float>(i));
    const auto path = (std::filesystem::temp_directory_path() / "fd-crossfade-sums.wav").string();
    REQUIRE(fd::audiofile::writeWav(path, ch, 48000.0));

    fd::app::CrossfadeSums sums;
    int ready = 0;
    sums.onReady = [&] { ++ready; };
    const fd::app::CrossfadeSums::Join join{.out = path, .in = path, .outAt = 10.0, .inAt = 12.0};
    using Shape = fd::model::CrossfadeShape;

    // Nothing until the join's audio is read.
    CHECK(sums.get(join, -0.05, 0.1, Shape::EqualPower).peaks == nullptr);
    sums.waitUntilIdle();
    CHECK(ready == 1);
    auto sum = sums.get(join, -0.05, 0.1, Shape::EqualPower);
    REQUIRE(sum.peaks != nullptr);
    CHECK(sum.from == Approx(-0.05));
    CHECK(sum.length == Approx(0.1));
    CHECK(sum.peaks->frames >= 4800);

    // A drag within what was read: every step's sum at once, its shape too.
    for (int step = 1; step <= 20; ++step)
    {
        const double length = 0.1 + 0.01 * step;
        sum = sums.get(join, -length * 0.5, length, step % 2 == 0 ? Shape::EqualPower : Shape::SCurve);
        REQUIRE(sum.peaks != nullptr);
        CHECK(sum.length == Approx(length));
    }
    CHECK(ready == 1); // nothing more was read

    // Past it: the last sum, where it was made, until the wider read arrives.
    sum = sums.get(join, -2.0, 4.0, Shape::EqualPower);
    REQUIRE(sum.peaks != nullptr);
    CHECK(sum.length == Approx(0.3));
    sums.waitUntilIdle();
    CHECK(ready == 2);
    sum = sums.get(join, -2.0, 4.0, Shape::EqualPower);
    REQUIRE(sum.peaks != nullptr);
    CHECK(sum.from == Approx(-2.0));
    CHECK(sum.length == Approx(4.0));

    // In place: a file crossfaded into itself at the same point with equal gain is that file.
    const fd::app::CrossfadeSums::Join same{.out = path, .in = path, .outAt = 10.0, .inAt = 10.0};
    (void)sums.get(same, -0.05, 0.1, Shape::EqualGain);
    sums.waitUntilIdle();
    sum = sums.get(same, -0.05, 0.1, Shape::EqualGain);
    REQUIRE(sum.peaks != nullptr);
    const std::int64_t first = (10 * 48000) - 2400; // 9.95 s
    for (const std::int64_t bin : {0, 37, 250})
    {
        const auto [lo, hi] = sum.peaks->range(bin * 16, bin * 16 + 16);
        float srcLo = 0.0f, srcHi = 0.0f; // a bin's range includes zero
        for (std::int64_t i = first + bin * 16; i < first + bin * 16 + 16; ++i)
        {
            srcLo = std::min(srcLo, ch[0][static_cast<size_t>(i)]);
            srcHi = std::max(srcHi, ch[0][static_cast<size_t>(i)]);
        }
        CHECK(lo == Approx(srcLo).margin(1e-4));
        CHECK(hi == Approx(srcHi).margin(1e-4));
    }

    // A file that can't be read: no sum, and no reading it again on every paint.
    const fd::app::CrossfadeSums::Join missing{.out = path + ".gone", .in = path, .outAt = 1.0, .inAt = 1.0};
    CHECK(sums.get(missing, -0.05, 0.1, Shape::EqualPower).peaks == nullptr);
    sums.waitUntilIdle();
    CHECK(sums.get(missing, -0.05, 0.1, Shape::EqualPower).peaks == nullptr);
    sums.waitUntilIdle();
    CHECK(ready == 4);
    std::filesystem::remove(path);
}
