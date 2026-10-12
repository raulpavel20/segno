#include "model/BraceDrag.h"

#include <algorithm>
#include <cmath>

namespace fd::model
{
BraceZone braceZoneAt(double x, double x0, double x1, double edgePixels) noexcept
{
    if (!(x1 > x0))
        return BraceZone::None;
    const double edge = std::min(edgePixels, (x1 - x0) / 3.0);
    if (std::abs(x - x0) <= edge)
        return BraceZone::Start;
    if (std::abs(x - x1) <= edge)
        return BraceZone::End;
    return x > x0 && x < x1 ? BraceZone::Body : BraceZone::None;
}

BraceRange dragBrace(BraceZone grabbed, BraceRange atPress, double then, double now,
                     const std::function<double(double)>& snap, double shortest)
{
    auto [a, b] = atPress;
    switch (grabbed)
    {
    case BraceZone::None: // drawn new
        a = snap(std::min(then, now));
        b = snap(std::max(then, now));
        break;
    case BraceZone::Body:
    {
        const double moved = snap(a + (now - then)) - a;
        a = std::max(0.0, a + moved);
        b = a + (atPress.second - atPress.first);
        break;
    }
    case BraceZone::Start: a = std::min(snap(now), b - shortest); break;
    case BraceZone::End: b = std::max(snap(now), a + shortest); break;
    }
    return {a, b};
}

BraceRelease releaseBrace(BraceZone grabbed, bool moved, std::optional<BraceRange> dragged) noexcept
{
    if (moved && dragged && dragged->second > dragged->first)
        return {.range = dragged, .switchOn = grabbed == BraceZone::None};
    if (!moved && grabbed != BraceZone::None)
        return {.toggle = true};
    return {};
}
} // namespace fd::model
