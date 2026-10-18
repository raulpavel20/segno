#include "model/TrackHeaderLayout.h"

#include <algorithm>

namespace fd::model
{
TrackHeaderSlots layoutTrackHeader(double width, double height, int buttons, bool meter,
                                   const TrackHeaderMetrics& m, int depth, bool fold)
{
    TrackHeaderSlots s;
    // The groups' bars, a step apart (closer past the deepest indent), then its own.
    depth = std::max(0, depth);
    const double indent = m.groupIndent * std::min(depth, m.maxIndentDepth);
    const double step = depth > 0 ? indent / depth : 0.0;
    for (int k = 0; k < depth; ++k)
        s.groupBars.push_back({step * k, 1.0, m.colourWidth, std::max(0.0, height - 2.0)});
    s.colour = {indent, 1.0, m.colourWidth, std::max(0.0, height - 2.0)};

    double right = width - m.paddingRight;
    if (meter)
    {
        s.meter = {right - m.meterWidth, m.paddingY, m.meterWidth, std::max(0.0, height - 2.0 * m.paddingY)};
        right -= m.meterWidth + m.meterGap;
    }
    double left = indent + m.paddingLeft;
    double y = m.paddingY;
    if (fold)
    {
        s.fold = {left, y + (m.nameHeight - m.foldSize) * 0.5, m.foldSize, m.foldSize};
        left += m.foldSize + m.foldGap; // the name, its subtitle and bars line up after it
    }

    // The name row, with the buttons right-aligned beside it (centred on the name).
    double nameRight = right;
    if (buttons > 0 && height >= m.buttonsMinHeight)
    {
        const double top = y + (m.nameHeight - m.buttonSize) * 0.5;
        double x = right - m.buttonSize;
        for (int i = 0; i < buttons && x >= left; ++i)
        {
            s.buttons.push_back({x, top, m.buttonSize, m.buttonSize});
            x -= m.buttonSize + m.buttonGap;
        }
        std::reverse(s.buttons.begin(), s.buttons.end()); // left to right
        if (!s.buttons.empty())
            nameRight = s.buttons.front().x - m.buttonGap;
    }
    s.name = {left, y, std::max(0.0, nameRight - left), m.nameHeight};
    y += m.nameHeight;
    // The buttons stand taller than the name's row: what's below clears them by a gap, as it
    // runs under them.
    if (!s.buttons.empty())
        y = std::max(y, s.buttons.front().bottom() + m.buttonGap);

    if (height >= m.subtitleMinHeight)
    {
        s.subtitle = {left, y, std::max(0.0, right - left), m.subtitleHeight};
        y += m.subtitleHeight;
    }
    if (height >= m.controlsMinHeight)
        s.controls = {left, y + m.rowGap, std::max(0.0, right - left), m.rowHeight};
    return s;
}
} // namespace fd::model
