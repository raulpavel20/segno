#pragma once

#include <vector>

namespace fd::model
{
/// Where each part of a track header goes (docs/design/ui-foundations.md, "Track headers",
/// ADR 0023). Pure: the arrangement paints from it, and the mixer (C3) and recording (C5) fill
/// its slots instead of placing controls by hand. Which slots show depends on the height.
struct HeaderRect
{
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    [[nodiscard]] bool isEmpty() const noexcept { return w <= 0.0 || h <= 0.0; }
    [[nodiscard]] double right() const noexcept { return x + w; }
    [[nodiscard]] double bottom() const noexcept { return y + h; }
};

struct TrackHeaderMetrics
{
    double colourWidth = 3.0;
    double paddingLeft = 12.0, paddingRight = 8.0, paddingY = 6.0;
    double nameHeight = 16.0, subtitleHeight = 14.0, rowHeight = 20.0, rowGap = 4.0;
    double buttonSize = 20.0, buttonGap = 4.0;
    double meterWidth = 8.0, meterGap = 4.0;
    // The smallest header height that shows each optional slot.
    double buttonsMinHeight = 32.0, subtitleMinHeight = 48.0, controlsMinHeight = 80.0;
    // Groups (groups.md §6.1, §6.3): each group a row is in adds a colour bar and one step of
    // indent, up to `maxIndentDepth` steps; deeper, the bars draw closer and the indent stays. A
    // group's fold triangle sits before its name.
    double groupIndent = 8.0;
    int maxIndentDepth = 4;
    double foldSize = 12.0, foldGap = 4.0;
};

struct TrackHeaderSlots
{
    std::vector<HeaderRect> groupBars; // the colours of the groups it's in, outermost first, on the left
    HeaderRect colour;                 // the track's colour, after its groups' (on the left edge in none)
    HeaderRect fold;                   // a group's fold triangle, before its name
    HeaderRect name;                 // always
    std::vector<HeaderRect> buttons; // mute, solo, arm...: right-aligned beside the name
    HeaderRect subtitle;             // the instrument, or "No instrument"
    HeaderRect controls;             // volume and pan
    HeaderRect meter;                // on the right edge, full height
};

/// The slots of a `width` x `height` header with `buttons` buttons and an optional meter, in
/// `depth` groups, with a fold triangle for a group (`fold`). Coordinates are relative to the
/// header's top left; slots that don't fit are empty.
[[nodiscard]] TrackHeaderSlots layoutTrackHeader(double width, double height, int buttons, bool meter,
                                                 const TrackHeaderMetrics& metrics = {}, int depth = 0,
                                                 bool fold = false);
} // namespace fd::model
