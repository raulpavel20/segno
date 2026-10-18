#include "fd/music/Arpeggio.h"

#include "fd/music/Following.h"

#include <array>
#include <numeric>

namespace fd::music
{
namespace
{
std::int64_t wrap(std::int64_t n, std::int64_t period) noexcept
{
    return ((n % period) + period) % period;
}

/// 0, c−1, 1, c−2, …: from the outside in.
int converge(int count, std::int64_t i) noexcept
{
    return i % 2 == 0 ? static_cast<int>(i / 2) : count - 1 - static_cast<int>(i / 2);
}
} // namespace

int arpIndex(ArpStyle style, int count, std::int64_t n, std::uint64_t roll) noexcept
{
    if (count <= 0)
        return -1;
    if (count == 1)
        return style == ArpStyle::Chord ? -1 : 0;
    switch (style)
    {
    case ArpStyle::Up:
    case ArpStyle::Played:
        return static_cast<int>(wrap(n, count));
    case ArpStyle::Down:
        return count - 1 - static_cast<int>(wrap(n, count));
    case ArpStyle::UpDown:
    case ArpStyle::DownUp:
    {
        // Up and back without repeating the ends: a period of 2c − 2.
        const std::int64_t i = wrap(n, 2 * count - 2);
        const int up = static_cast<int>(i < count ? i : 2 * count - 2 - i);
        return style == ArpStyle::UpDown ? up : count - 1 - up;
    }
    case ArpStyle::Converge:
        return converge(count, wrap(n, count));
    case ArpStyle::Diverge:
        return converge(count, count - 1 - wrap(n, count));
    case ArpStyle::Chord:
        return -1;
    case ArpStyle::Random:
        return static_cast<int>(mix(roll, static_cast<std::uint64_t>(n)) % static_cast<std::uint64_t>(count));
    case ArpStyle::Shuffled:
    {
        std::array<int, 64> order{};
        const int c = count < 64 ? count : 64;
        std::iota(order.begin(), order.begin() + c, 0);
        for (int i = c - 1; i > 0; --i) // Fisher–Yates from the roll and the count
        {
            const auto j =
                static_cast<int>(mix(roll, static_cast<std::uint64_t>(c), static_cast<std::uint64_t>(i)) %
                                 static_cast<std::uint64_t>(i + 1));
            std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(j)]);
        }
        return order[static_cast<std::size_t>(wrap(n, c))];
    }
    }
    return 0;
}
} // namespace fd::music
