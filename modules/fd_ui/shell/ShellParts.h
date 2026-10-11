#pragma once

namespace fd::ui
{
// Shared shell components (docs/design/ui-foundations.md, "Shared components", ADR 0023).
// Views assemble these instead of drawing their own headers, rows, popovers and hints.

/// A panel's header row: a section-style title, or clickable tabs, on the left; context text
/// after them (e.g. the track's name); on the right, the panel's view options and a caption (a
/// hint).
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
    /// A view option on the right, after any added before it (not owned; it must outlive the
    /// header or be removed first). Hidden ones take no room: call resized() after showing or
    /// hiding one.
    void addOption(ViewOption& option);
    /// A view setting with a value (the piano roll's grid step), as wide as its value: call
    /// resized() when the value changes.
    void addOption(ViewChoice& choice);
    /// A command left of the view options, a group gap from them (a Toggle as a button: the clip
    /// view's Quantise), sized by its owner (not owned). Hidden ones take no room: call resized()
    /// after showing or hiding one.
    void addCommand(juce::Component& command);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] std::vector<juce::Rectangle<int>> tabAreas() const;

    juce::String title_, context_, caption_;
    juce::StringArray tabs_;
    int selected_ = 0, hovered_ = -1;
    struct Option
    {
        juce::Component* component;
        std::function<int()> width;
    };
    std::vector<Option> options_;
    std::vector<juce::Component*> commands_;
    int optionsLeft_ = 0; // the caption ends before the options
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
    /// Room kept at the right for a control the caller draws there (a preset's star).
    int trailing = 0;
};
/// Returns the room kept for `trailing` (empty without), where the caller draws its control.
juce::Rectangle<int> paintListRow(juce::Graphics& g, juce::Rectangle<int> row, const ListItem& item);

/// One row of a tree (the browser, ADR 0046): a ListRow's look, indented by depth, with a
/// disclosure (or a + for a command row), the name, a caption at the right (a count, a length),
/// and a star column kept on every row so captions line up. A flat row (a search result) has no
/// disclosure column.
struct TreeItem
{
    enum class Mark
    {
        None,
        Closed,
        Open,
        Add,
    };
    juce::String name, caption;
    int depth = 0;
    Mark mark = Mark::None;
    bool flat = false;
    bool selected = false, hovered = false;
    bool dim = false;       // gone, or "All 240 samples"
    bool secondary = false; // a command row ("Add folder…")
    bool starrable = false, starred = false;
};
void paintTreeRow(juce::Graphics& g, juce::Rectangle<int> row, const TreeItem& item);
/// Where a tree row's disclosure and star are (for clicks).
[[nodiscard]] juce::Rectangle<int> treeRowMark(juce::Rectangle<int> row, const TreeItem& item);
[[nodiscard]] juce::Rectangle<int> treeRowStar(juce::Rectangle<int> row);

/// Closes a popover on any mouse press outside it (docs/design/ui-foundations.md): a global
/// mouse listener while attached. Presses in other top-level windows (a popup menu the popover
/// opened, a plugin's window) don't count, nor do components `ignore` exempts (the button that
/// toggles the popover itself).
class OutsideClick final : private juce::MouseListener
{
public:
    OutsideClick(juce::Component& popover, std::function<void()> close);
    ~OutsideClick() override;
    OutsideClick(const OutsideClick&) = delete;
    OutsideClick& operator=(const OutsideClick&) = delete;

    void attach();
    void detach();
    std::function<bool(const juce::Component&)> ignore;
    /// The rule (tests call it): a press on `component` closes the popover unless it's the
    /// popover, inside it, in another window or ignored.
    void clickedOn(juce::Component& component);

private:
    void mouseDown(const juce::MouseEvent& e) override;

    juce::Component& popover_;
    std::function<void()> close_;
    bool attached_ = false;
};

/// Floating chrome: the one shadow, the panel surface and radius, a hairline edge.
void paintPopover(juce::Graphics& g, juce::Rectangle<int> bounds);

/// A message in the corner of `host` (bottom right) that the caller times out.
[[nodiscard]] juce::Rectangle<int> toastArea(juce::Rectangle<int> host);
/// A toast; with `action`, its label ("Show") on the right in `active`: a click on the toast runs it.
void paintToast(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, const juce::String& action = {});

/// A centred hint where there is nothing to show.
void paintEmptyState(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text);

/// Work in progress on what it changes (a bounce on its clips, an export): a line along the
/// bottom of `area`: a `surface3` track, `fraction` (0..1) of it in `value` from the left. Callers
/// show it only after motion.progressDelay.
void paintProgress(juce::Graphics& g, juce::Rectangle<float> area, float fraction);

/// What a gesture is changing, next to the pointer while it drags (a fade's length): `text` in
/// the `Value` style on a `surface3` tag a row high, above and right of `anchor` by the item gap,
/// kept within `within`.
void paintValueTag(juce::Graphics& g, juce::Point<float> anchor, juce::Rectangle<int> within, const juce::String& text);

/// Titled sections one under another (the inspector): each a caps title on the row grid with its
/// content under it, and a 1 px line centred in the group gap between neighbours, like a module
/// panel's sections turned vertical. A layout helper: the owner keeps its components and places
/// them in content(i); paint() draws the titles and lines.
class SectionStack
{
public:
    struct Section
    {
        juce::String title;
        int contentHeight = 0;
    };
    /// Places `sections` from the top of `area`; returns what's left below them.
    juce::Rectangle<int> layout(juce::Rectangle<int> area, const std::vector<Section>& sections);
    /// Where section `index`'s content goes (empty if there is no such section).
    [[nodiscard]] juce::Rectangle<int> content(size_t index) const;
    /// Section `index`'s title row (for a command at its right: Settings' Reset).
    [[nodiscard]] juce::Rectangle<int> titleArea(size_t index) const;
    [[nodiscard]] size_t size() const noexcept { return placed_.size(); }
    void paint(juce::Graphics& g) const;

private:
    struct Placed
    {
        juce::String title;
        juce::Rectangle<int> titleArea, contentArea, line;
    };
    std::vector<Placed> placed_;
};

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
