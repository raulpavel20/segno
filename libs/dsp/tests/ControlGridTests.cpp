#include "fd/dsp/ControlGrid.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace fd::dsp;

namespace
{
/// Absolute sample positions at which ticks occur when processing `total` samples
/// split into the given block sizes (cycled).
std::vector<long> tickPositions(int interval, int total, const std::vector<int>& blocks)
{
    ControlGrid grid(interval);
    std::vector<long> ticks;
    long absolute = 0;
    size_t b = 0;
    while (absolute < total)
    {
        const int n = std::min(blocks[b++ % blocks.size()], static_cast<int>(total - absolute));
        int covered = 0;
        grid.forEachSegment(n, [&](int offset, int length, bool isTick) {
            CHECK(offset == covered);
            CHECK(length > 0);
            covered += length;
            if (isTick)
                ticks.push_back(absolute + offset);
        });
        CHECK(covered == n);
        absolute += n;
    }
    return ticks;
}
} // namespace

TEST_CASE("ticks fall on absolute multiples of the interval", "[fast]")
{
    const auto ticks = tickPositions(32, 1000, {64});
    REQUIRE(ticks.size() == 32); // 0, 32, ..., 992
    for (size_t i = 0; i < ticks.size(); ++i)
        CHECK(ticks[i] == static_cast<long>(i) * 32);
}

TEST_CASE("tick positions do not depend on block sizes", "[fast]")
{
    const auto reference = tickPositions(32, 5000, {512});
    CHECK(tickPositions(32, 5000, {1}) == reference);
    CHECK(tickPositions(32, 5000, {7}) == reference);
    CHECK(tickPositions(32, 5000, {480}) == reference);
    CHECK(tickPositions(32, 5000, {3, 100, 31, 33, 4096}) == reference);
}

TEST_CASE("reset restarts the grid", "[fast]")
{
    ControlGrid grid(16);
    grid.forEachSegment(10, [](int, int, bool) {});
    CHECK(grid.position() == 10);
    grid.reset();
    bool firstIsTick = false;
    grid.forEachSegment(4, [&](int offset, int, bool isTick) {
        if (offset == 0)
            firstIsTick = isTick;
    });
    CHECK(firstIsTick);
}
