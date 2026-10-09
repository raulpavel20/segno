#pragma once

namespace fd::ui
{
// Shared shell components (docs/design/ui-foundations.md, "Shared components", ADR 0023).
// Views assemble these instead of drawing their own headers, rows, popovers and hints.

/// A panel's header row: a section-style title, or clickable tabs, on the left; context text
/// after them (e.g. the track's name); a caption on the right (a hint).
class PanelHeader final : public juce::Component
{
public:
    PanelHeader();

    void setTitle(juce::String title);
    /// Tabs instead of a title; `onTabSelected` fires when one is clicked.
    void setTabs(juce::StringArray tabs, int selected);
    void setSelectedTab(int index);
    [[nodiscard]] int selectedTab() const noexcept { return selected_; }
    std::function<void(int index)> onTabSelected;
    void setContext(juce::String text);
    void setCaption(juce::String text);

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] std::vector<juce::Rectangle<int>> tabAreas() const;

    juce::String title_, context_, caption_;
    juce::StringArray tabs_;
    int selected_ = 0, hovered_ = -1;
};

/// One label and value row (`number`: the value is a readout, set in the mono).
struct Property
{
    juce::String label, value;
    bool number = false;
};

/// Label and value rows on the shell's row grid, from the top of `area`. Returns what's left
/// below them.
juce::Rectangle<int> paintProperties(juce::Graphics& g, juce::Rectangle<int> area, const std::vector<Property>& rows);

/// One row of a list: an optional dot (toggled), the name, a caption after it, a shortcut on
/// the right; selected rows are highlighted, disabled ones recede.
struct ListItem
{
    juce::String name, caption, shortcut;
    bool toggled = false, enabled = true, selected = false;
};
void paintListRow(juce::Graphics& g, juce::Rectangle<int> row, const ListItem& item);

/// Floating chrome: the one shadow, the panel surface and radius, a hairline edge.
void paintPopover(juce::Graphics& g, juce::Rectangle<int> bounds);

/// A message in the corner of `host` (bottom right) that the caller times out.
[[nodiscard]] juce::Rectangle<int> toastArea(juce::Rectangle<int> host);
void paintToast(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text);

/// A centred hint where there is nothing to show.
void paintEmptyState(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text);

/// Lays out a bar (the transport) left to right and right to left, on the shell's gaps: items
/// within a group are `itemGap` apart, groups `groupGap`.
class BarLayout
{
public:
    explicit BarLayout(juce::Rectangle<int> area) : area_(area) {}
    /// The next item from the left (`width` < 0: a square as tall as the bar).
    juce::Rectangle<int> left(int width = -1);
    juce::Rectangle<int> right(int width = -1);
    /// The next item starts a new group.
    void groupLeft() { groupLeft_ = true; }
    void groupRight() { groupRight_ = true; }
    [[nodiscard]] juce::Rectangle<int> remaining() const noexcept { return area_; }

private:
    juce::Rectangle<int> area_;
    bool firstLeft_ = true, firstRight_ = true, groupLeft_ = false, groupRight_ = false;
};
} // namespace fd::ui
