#include "model/ChannelStripLayout.h"

#include <algorithm>

namespace fd::model
{
double channelStripWidth(const ChannelStripOptions& o, const ChannelStripMetrics& m)
{
    return o.narrow ? m.narrowWidth : m.width;
}

ChannelStripSlots layoutChannelStrip(double height, const ChannelStripOptions& o,
                                     const ChannelStripMetrics& m, int depth, bool fold)
{
    ChannelStripSlots s;
    const double width = channelStripWidth(o, m);
    const double left = m.padding, inner = width - 2.0 * m.padding;
    // The groups' bars, then its own colour: every strip keeps the rows of the deepest, so the rows
    // below line up across the mixer.
    const double barStep = m.colourHeight + m.groupBarGap;
    for (int k = 0; k < std::min(depth, o.groupRows); ++k)
        s.groupBars.push_back({0.0, k * barStep, width, m.colourHeight});
    const double top = std::max(0, o.groupRows) * barStep;
    s.colour = {0.0, top, width, m.colourHeight};
    double y = top + 2.0 * m.rowGap; // the first row on the grid, clear of the colour bar
    s.name = {left, y, inner, m.nameHeight};
    if (fold)
    {
        s.fold = {left, y + (m.nameHeight - m.foldSize) * 0.5, m.foldSize, m.foldSize};
        s.name = {left + m.foldSize + m.foldGap, y, std::max(0.0, inner - m.foldSize - m.foldGap),
                  m.nameHeight};
    }
    y += m.nameHeight + m.rowGap;
    s.input = {left, y, inner, m.rowHeight}; // returns and the master keep the space, so rows line up
    y += m.rowHeight + m.sectionGap;

    if (o.modules)
    {
        for (int i = 0; i < m.moduleRows; ++i)
            s.modules.push_back({left, y + i * m.rowHeight, inner, m.rowHeight});
        y += m.moduleRows * m.rowHeight + m.sectionGap;
    }
    if (o.sends && o.sendRows > 0)
    {
        const double pre = o.narrow ? 0.0 : 2.0 * m.rowHeight - m.rowGap; // "Pre" needs two cells
        for (int i = 0; i < o.sendRows; ++i)
        {
            const double rowY = y + i * (m.rowHeight + m.rowGap);
            const double barWidth = pre > 0.0 ? inner - pre - m.rowGap : inner;
            s.sends.push_back({left, rowY, barWidth, m.rowHeight});
            s.sendPre.push_back(pre > 0.0 ? HeaderRect{left + barWidth + m.rowGap, rowY, pre, m.rowHeight} : HeaderRect{});
        }
        y += o.sendRows * (m.rowHeight + m.rowGap) - m.rowGap + m.sectionGap;
    }
    s.pan = {left, y, inner, m.rowHeight};
    y += m.rowHeight + m.rowGap;
    // Mute and solo, square, side by side on the left.
    s.mute = {left, y, m.rowHeight, m.rowHeight};
    s.solo = {left + m.rowHeight + m.rowGap, y, m.rowHeight, m.rowHeight};
    if (!o.narrow)
        s.arm = {left + 2.0 * (m.rowHeight + m.rowGap), y, m.rowHeight, m.rowHeight};
    y += m.rowHeight + m.sectionGap;
    s.readout = {left, y, inner, m.readoutHeight};
    y += m.readoutHeight + m.rowGap;

    // The fader, the dB scale and the meter fill what's left above the value row.
    const double bottom = std::max(y + m.minFaderHeight, height - m.padding - m.valueHeight - m.rowGap);
    const double faderHeight = bottom - y;
    const double scale = o.narrow ? 0.0 : m.scaleWidth;
    const double group = m.faderWidth + (scale > 0.0 ? m.gap + scale : 0.0) + m.gap + m.meterWidth;
    double x = left + std::max(0.0, (inner - group) * 0.5);
    s.fader = {x, y, m.faderWidth, faderHeight};
    x += m.faderWidth + m.gap;
    if (scale > 0.0)
    {
        s.scale = {x, y, scale, faderHeight};
        x += scale + m.gap;
    }
    s.meter = {x, y, m.meterWidth, faderHeight};
    s.value = {left, bottom + m.rowGap, inner, m.valueHeight};
    return s;
}
} // namespace fd::model
