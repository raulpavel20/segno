#pragma once

#include "model/TrackHeaderLayout.h"

#include <vector>

namespace fd::model
{
/// Where each part of a channel strip goes in the mixer view (docs/design/mixer.md §6). Pure:
/// the strips place their controls from it, so every strip lines up with its neighbours. Rows
/// that a strip doesn't use (a return's sends, the master's mute and solo) keep their space, so
/// faders and meters start at the same height across the mixer.
struct ChannelStripMetrics
{
    double width = 96.0, narrowWidth = 64.0;
    double padding = 8.0, colourHeight = 3.0;
    double nameHeight = 20.0, rowHeight = 20.0, rowGap = 4.0, sectionGap = 8.0;
    int moduleRows = 6;
    double readoutHeight = 16.0, valueHeight = 20.0;
    double faderWidth = 24.0, scaleWidth = 28.0, meterWidth = 12.0, gap = 4.0; // 72 together: centred on the grid
    double minFaderHeight = 80.0;
    // Groups (groups.md §9): a bar of each group's colour across the top of its members' strips,
    // one row a level, outermost first; a group's fold triangle before its name.
    double groupBarGap = 1.0;
    double foldSize = 12.0, foldGap = 4.0;
};

struct ChannelStripOptions
{
    bool modules = true; // the module names section
    bool sends = true;   // the sends section
    bool narrow = false; // 64 px instead of 96; no dB scale, no Pre switches
    int sendRows = 0;    // the number of return tracks (the same for every strip)
    int groupRows = 0;   // the deepest group nesting shown: rows of group bars (the same for every strip)
};

struct ChannelStripSlots
{
    std::vector<HeaderRect> groupBars; // the groups it's in, outermost first, above its own colour
    HeaderRect colour, name;
    HeaderRect fold;                        // a group's fold triangle, before its name
    HeaderRect input;                       // what a track listens to (recording.md §3): a row under the name
    std::vector<HeaderRect> modules;        // one row per module name (moduleRows of them)
    std::vector<HeaderRect> sends, sendPre; // one per return: the bar and its Pre switch
    HeaderRect pan, mute, solo, arm, readout; // arm: beside mute and solo, unless narrow
    HeaderRect fader, scale, meter;         // side by side, filling the height that's left
    HeaderRect value;                       // the volume, typed
};

/// The slots of a strip `height` tall with `options`, in `depth` groups, with a fold triangle for a
/// group (`fold`); its width comes from the options.
[[nodiscard]] ChannelStripSlots layoutChannelStrip(double height, const ChannelStripOptions& options,
                                                   const ChannelStripMetrics& metrics = {}, int depth = 0,
                                                   bool fold = false);
/// A strip's width with these options.
[[nodiscard]] double channelStripWidth(const ChannelStripOptions& options, const ChannelStripMetrics& metrics = {});
} // namespace fd::model
