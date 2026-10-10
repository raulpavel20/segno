#pragma once

#include <vector>

namespace fd::model
{
/// Where each item of the top bar goes (docs/design/daw-shell.md, "Top bar"). Pure: the bar
/// declares its items and places them from this, never by hand. Three sections over the whole
/// width: the left one from the left edge, the right one up to the right edge, and the centre one
/// on the bar's middle, moved in only when a side needs the room. When the bar is too narrow,
/// items drop by priority.
enum class BarSection { Left, Centre, Right };

struct BarItem
{
    BarSection section = BarSection::Left;
    double width = 0.0;
    /// A group gap before it (an item gap otherwise); ignored for its section's first shown item.
    bool startsGroup = false;
    /// 0: always shown; otherwise items drop highest first, all of one priority together.
    int drop = 0;
};

struct BarMetrics
{
    double padding = 8.0;     // the bar's left and right edges
    double itemGap = 4.0;     // between items of a group
    double groupGap = 12.0;   // between groups
    double sectionGap = 24.0; // at least, between sections
};

struct BarSlot
{
    double x = 0.0, width = 0.0;
    bool shown = false;
    [[nodiscard]] double right() const noexcept { return x + width; }
};

/// The slots of `items` (given left to right, sections in order) in a bar `width` wide.
[[nodiscard]] std::vector<BarSlot> layoutTopBar(double width, const std::vector<BarItem>& items, const BarMetrics& metrics = {});

/// A title between a bar's sides (the title bar's project name, docs/design/menu-bar.md §4): as
/// wide as its text up to the room between `leftEnd` and `rightStart` (each kept `gap` away),
/// centred on the bar when it fits there, moved in when a side needs the room. Narrower than its
/// text means the caller elides it; not shown when there's no room at all.
[[nodiscard]] BarSlot placeTitle(double width, double leftEnd, double rightStart, double textWidth, double gap);
} // namespace fd::model
