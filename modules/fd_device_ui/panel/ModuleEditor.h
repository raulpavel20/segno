#pragma once

namespace fd::ui
{
/// A module's editor bigger than its panel (ADR 0053): the Wavetable's table editor. The host
/// shows it below the module, in the DAW's detail panel as a view of its own beside Modules and
/// Clip, in a plugin's window under the panel, with a header the host draws: the tab
/// (`tabName()`), what it edits (`title()`) and the editor's commands, Done last. The host owns
/// it while it shows; Done, or the module going, closes it.
///
/// A card like the module's panel, laid out in panes side by side: titled columns on the module
/// grid (its cells, rows and gaps), so the editor's controls look and line up as a panel's do.
/// The panes at the sides are a fixed number of columns wide; the one marked flexible takes the
/// width left over. Editors place their controls in a pane's cells; this class computes every
/// position, and the layout lint checks them.
class ModuleEditor : public juce::Component
{
public:
    using Kind = PanelSection::Kind;

    /// A control placed in a pane's cells. A negative `row` counts from the last row (-1); a
    /// `rows` of 0 or less runs down to that many rows above the bottom (0: to the last row).
    /// A `columns` of 0 or less runs to the pane's right edge, less that many columns.
    struct Placement
    {
        juce::Component* component = nullptr;
        Kind kind = Kind::Display;
        int pane = 0;
        int column = 0, row = 0, columns = 1, rows = 1;
    };

    ModuleEditor();
    ~ModuleEditor() override;

    /// The tab that shows it ("Table").
    [[nodiscard]] virtual juce::String tabName() const = 0;
    /// What it edits now ("Osc 1, Vowels (edited)").
    [[nodiscard]] virtual juce::String title() const = 0;
    /// Its commands for the host's header, in order: its own, then Done.
    [[nodiscard]] std::vector<juce::Component*> commands();
    /// The host: its title changed.
    std::function<void()> onTitleChanged;
    /// The host: Done was clicked, so the host closes (destroys) it.
    std::function<void()> onDone;
    /// The host: it asks to be shown (audio arrived for it while another view showed).
    std::function<void()> onShowRequested;
    void requestShow()
    {
        if (onShowRequested)
            onShowRequested();
    }
    /// The host is about to close it: it lets go of what it holds (a previewed note).
    virtual void closing() {}

    /// Its least height (the host's header not included): the rows its panes need. Also the
    /// height it opens at.
    [[nodiscard]] int minimumHeight() const;
    /// Its least width: the fixed panes and the flexible one at its least.
    [[nodiscard]] int minimumWidth() const;
    /// The content rows at its height.
    [[nodiscard]] int rows() const noexcept { return rows_; }

    // ---- for the layout lint --------------------------------------------------------------

    [[nodiscard]] const std::vector<Placement>& placements() const noexcept { return placements_; }
    [[nodiscard]] int paneCount() const noexcept { return static_cast<int>(panes_.size()); }
    /// A pane's area and its content rows' area (laid out), in the editor.
    [[nodiscard]] juce::Rectangle<int> paneBounds(int pane) const;
    [[nodiscard]] juce::Rectangle<int> paneContent(int pane) const;
    /// A pane's title row, and the components placed in it.
    [[nodiscard]] juce::Rectangle<int> paneTitleArea(int pane) const;
    [[nodiscard]] std::vector<juce::Component*> titleComponents(int pane) const;
    /// Where a pane's title text is drawn (empty without one).
    [[nodiscard]] juce::Rectangle<int> paneTitleText(int pane) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    /// A pane: a titled column `columns` cells of the module grid wide, or (0) the width left
    /// over, at least `minColumns`. Returns its index.
    int addPane(juce::String title, int columns, int minColumns = 0);
    void setPaneTitle(int pane, juce::String title);
    /// A component in a pane's title row, `width` wide: at its right end (a "+"), or after its
    /// title on the left (a view's tabs, where the pane has no title).
    void placeInTitle(int pane, juce::Component& component, int width, bool right);
    /// Places `component` (owned elsewhere) in a pane's cells (see Placement).
    void place(int pane, juce::Component& component, int column, int row, int columns, int rows,
               Kind kind = Kind::Display);
    /// A sub-label: named rows in a pane, as in a panel's section.
    void label(int pane, juce::String text, int column, int row, int columns);
    /// Removes a pane's placements and labels; their components stay with their owners, hidden.
    void clearPane(int pane);
    /// The rows the editor needs at the least: its least height.
    void setMinimumRows(int rows) noexcept { minimumRows_ = rows; }
    /// One of its own commands for the header, before Done (in the order added).
    void addCommand(juce::Component& command) { commands_.push_back(&command); }

private:
    class CapsLabel;
    struct Pane
    {
        juce::String title;
        int columns = 0, minColumns = 0;
        juce::Rectangle<int> bounds, content;
        struct Titled
        {
            juce::Component* component;
            int width;
            bool right;
        };
        std::vector<Titled> titled;
    };
    [[nodiscard]] juce::Rectangle<int> cellArea(const Placement& p) const;
    void layoutPlacement(const Placement& p);
    void layoutTitle(int pane);

    std::vector<Pane> panes_;
    std::vector<Placement> placements_;
    std::vector<std::unique_ptr<juce::Component>> labels_;
    std::vector<juce::Component*> commands_;
    Toggle done_{"Done"};
    int rows_ = 0, minimumRows_ = 1;
    std::vector<int> separators_; // x of the lines between panes
};
} // namespace fd::ui
