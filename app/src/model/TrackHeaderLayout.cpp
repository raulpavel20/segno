#include "model/TrackHeaderLayout.h"

#include <algorithm>

namespace fd::model
{
TrackHeaderSlots layoutTrackHeader(double width, double height, int buttons, bool meter, const TrackHeaderMetrics& m)
{
    TrackHeaderSlots s;
    s.colour = {0.0, 1.0, m.colourWidth, std::max(0.0, height - 2.0)};

    double right = width - m.paddingRight;
    if (meter)
    {
        s.meter = {right - m.meterWidth, m.paddingY, m.meterWidth, std::max(0.0, height - 2.0 * m.paddingY)};
        right -= m.meterWidth + m.meterGap;
    }
    const double left = m.paddingLeft;
    double y = m.paddingY;

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
