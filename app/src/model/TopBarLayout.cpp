#include "model/TopBarLayout.h"

#include <algorithm>
#include <array>

namespace fd::model
{
namespace
{
/// An item's width, in its narrower form when asked and it has one.
double widthOf(const BarItem& item, bool narrow) noexcept
{
    return narrow && item.narrowWidth > 0.0 ? item.narrowWidth : item.width;
}

/// The width of a section's shown items with their gaps.
double sectionWidth(const std::vector<BarItem>& items, const std::vector<bool>& shown, bool narrow,
                    BarSection section, const BarMetrics& m)
{
    double w = 0.0;
    bool first = true;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].section == section && shown[i])
        {
            if (!first)
                w += items[i].startsGroup ? m.groupGap : m.itemGap;
            w += widthOf(items[i], narrow);
            first = false;
        }
    return w;
}
} // namespace

std::vector<BarSlot> layoutTopBar(double width, const std::vector<BarItem>& items, const BarMetrics& m)
{
    std::vector<bool> shown(items.size(), true);
    bool narrow = false;
    auto widths = [&]
    {
        return std::array{sectionWidth(items, shown, narrow, BarSection::Left, m),
                          sectionWidth(items, shown, narrow, BarSection::Centre, m),
                          sectionWidth(items, shown, narrow, BarSection::Right, m)};
    };
    auto needed = [&](const std::array<double, 3>& w) {
        int sections = 0;
        double total = 2.0 * m.padding;
        for (double s : w)
            if (s > 0.0)
            {
                total += s;
                ++sections;
            }
        return total + std::max(0, sections - 1) * m.sectionGap;
    };

    // The narrower forms first, then drop the highest priority still shown until it fits (or
    // nothing more can go).
    narrow = needed(widths()) > width;
    for (auto w = widths(); needed(w) > width; w = widths())
    {
        int highest = 0;
        for (size_t i = 0; i < items.size(); ++i)
            if (shown[i])
                highest = std::max(highest, items[i].drop);
        if (highest == 0)
            break;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].drop == highest)
                shown[i] = false;
    }

    const auto w = widths();
    const double leftEnd = m.padding + w[0];
    const double rightStart = width - m.padding - w[2];
    // The centre on the bar's middle, moved in to keep its distance from the sides.
    double centre = (width - w[1]) * 0.5;
    if (w[0] > 0.0)
        centre = std::max(centre, leftEnd + m.sectionGap);
    if (w[2] > 0.0)
        centre = std::min(centre, rightStart - m.sectionGap - w[1]);
    if (w[0] > 0.0)
        centre = std::max(centre, leftEnd + m.sectionGap); // too narrow: the left side wins

    std::vector<BarSlot> slots(items.size());
    std::array<double, 3> x{m.padding, centre, rightStart};
    std::array<bool, 3> first{true, true, true};
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!shown[i])
            continue;
        const auto s = static_cast<size_t>(items[i].section);
        if (!first[s])
            x[s] += items[i].startsGroup ? m.groupGap : m.itemGap;
        const bool n = narrow && items[i].narrowWidth > 0.0;
        slots[i] = {x[s], widthOf(items[i], narrow), true, n};
        x[s] += slots[i].width;
        first[s] = false;
    }
    return slots;
}

BarSlot placeTitle(double width, double leftEnd, double rightStart, double textWidth, double gap)
{
    const double from = leftEnd + gap, to = rightStart - gap;
    const double w = std::min(textWidth, to - from);
    if (w <= 0.0)
        return {};
    const double x = std::clamp((width - w) * 0.5, from, to - w);
    return {x, w, true};
}
} // namespace fd::model
