#pragma once

namespace fd::ui
{
// Shared shell components (docs/design/ui-foundations.md, "Shared components", ADR 0023).
// Views assemble these instead of drawing their own headers, rows, popovers and hints.

/// Tabs in a row: the detail panel's (Modules, Clip) and a module's pages (ADR 0051). Each is its
/// name in `Control` text with a text button's padding either side; the selected one in
/// `textPrimary`, a hovered one in `textSecondary`, the rest in `textTertiary`. A click selects.
/// A tab for something open that closes (a module's editor, ADR 0053) carries an × after its name,
/// `listIconSize`, lit on hover.
class TabRow final : public juce::Component, public juce::TooltipClient
{
public:
    TabRow();
    void setTabs(juce::StringArray tabs, int selected);
    void setSelected(int index);
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] const juce::StringArray& tabs() const noexcept { return tabs_; }
    /// After a click on a tab other than the selected one, which it then selects.
    std::function<void(int index)> onSelect;
    /// Gives tab `index` an × (-1: none), its tooltip saying what closing does.
    void setClosable(int index, juce::String tooltip);
    [[nodiscard]] int closable() const noexcept { return closable_; }
    /// After a click on the ×.
    std::function<void(int index)> onClose;
    /// Where the × is (empty without one).
    [[nodiscard]] juce::Rectangle<int> closeArea() const;
    juce::String getTooltip() override;
    /// Every tab side by side.
    [[nodiscard]] int preferredWidth() const;
    /// Each tab's area, in this component, from its left edge.
    [[nodiscard]] std::vector<juce::Rectangle<int>> tabAreas() const;
    /// The tab under `position` (in this component), or -1.
    [[nodiscard]] int tabAt(juce::Point<int> position) const;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::StringArray tabs_;
    int selected_ = 0, hovered_ = -1, closable_ = -1;
    bool closeHovered_ = false;
    juce::String closeTooltip_;
};

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
    /// Gives tab `index` an × (-1: none); `onTabClosed` fires when it's clicked.
    void setClosableTab(int index, juce::String tooltip = {});
    std::function<void(int index)> onTabClosed;
    /// The tabs (for tests and the layout lint).
    [[nodiscard]] TabRow& tabRow() noexcept { return tabs_; }
    void setSelectedTab(int index);
    [[nodiscard]] int selectedTab() const noexcept { return tabs_.selected(); }
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
    /// A view setting of a few values side by side (the chord kind drawn: piano-roll.md §20.2).
    void addOption(Segments& segments);
    /// A command left of the view options, a group gap from them (a Toggle as a button: the clip
    /// view's Quantise), sized by its owner (not owned). Hidden ones take no room: call resized()
    /// after showing or hiding one.
    void addCommand(juce::Component& command);
    /// Takes a command away again (a module's editor closing, ADR 0053).
    void removeCommand(juce::Component& command);
    /// At narrow widths, options and commands with a drop order give way, the lowest first, so the
    /// rest never reach the tabs (the clip view's Others, then the voicing, then Draw, then its
    /// Generate and Transform: piano-roll.md §4, §25). Each one dropped is still in the menu bar.
    /// 0, the default: never dropped.
    void setDropOrder(juce::Component& option, int order);
    /// Whether an option gave way to the width (for tests and the layout lint).
    [[nodiscard]] bool isDropped(const juce::Component& option) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String title_, context_, caption_;
    TabRow tabs_;
    struct Option
    {
        juce::Component* component;
        std::function<int()> width;
        int drop = 0;         // setDropOrder
        bool dropped = false; // gave way to the width
    };
    std::vector<Option> options_;
    struct Command
    {
        juce::Component* component;
        int drop = 0;
        bool dropped = false;
        int width = 0; // its owner's size, kept while it gives way

        [[nodiscard]] int shownWidth() const
        {
            return component->getWidth() > 0 ? component->getWidth() : width;
        }
    };
    std::vector<Command> commands_;
    int optionsLeft_ = 0; // the caption ends before the options
};

/// One label and value row (`number`: the value is a readout, set in the mono).
struct Property
{
    juce::String label, value;
    bool number = false;
    /// The row's name for the control placed in it (a field, a toggle), so rows can come and go.
    const char* key = nullptr;
    /// A lock after the label: what the row sets is held as it is (a clip's chance kept as played,
    /// piano-roll.md §26).
    bool locked = false;
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
/// The width a row needs to show `item` whole (its insets, the dot when `toggled`, the name, the
/// caption, the shortcut and the trailing room): a list as wide as its widest row.
[[nodiscard]] int listRowWidth(const ListItem& item);

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
    /// Before the name: a part of the browser (the star of Favourites), as its rail shows it.
    std::optional<Icon> icon;
};
void paintTreeRow(juce::Graphics& g, juce::Rectangle<int> row, const TreeItem& item);
/// Where a tree row's icon is (empty without one): after its disclosure, before its name.
[[nodiscard]] juce::Rectangle<int> treeRowIcon(juce::Rectangle<int> row, const TreeItem& item);
/// Where a tree row's disclosure and star are (for clicks).
[[nodiscard]] juce::Rectangle<int> treeRowMark(juce::Rectangle<int> row, const TreeItem& item);
[[nodiscard]] juce::Rectangle<int> treeRowStar(juce::Rectangle<int> row);
/// Where a tree row's caption goes: at the right, when it and the name both fit whole; empty when
/// the name needs the room (the name comes first; the strip and the tooltip say the rest).
[[nodiscard]] juce::Rectangle<int> treeRowCaption(juce::Rectangle<int> row, const TreeItem& item);

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
/// The one shadow a floating thing casts (ui-principles §4), under `bounds`: a popover's, the
/// inspector's while it's over the arrangement (selection.md §8.1).
void paintShadow(juce::Graphics& g, juce::Rectangle<int> bounds);

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

/// Something in `area` that doesn't belong with what's around it: fine diagonal 1 px lines in
/// `colour`, `shell.hatchSpacing` apart, clipped to the area. A piano roll row outside the key
/// while it shows the key's rows (docs/design/piano-roll.md §21.2).
void paintHatch(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);

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
