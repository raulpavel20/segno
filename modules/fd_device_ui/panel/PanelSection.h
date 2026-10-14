#pragma once

namespace fd::ui
{
class DevicePanel;

/// A titled group of controls on the module grid (docs/design/device-panels.md, ADR 0022).
/// Controls are placed in cells (column, row) with spans; the section computes every position,
/// so all modules share one spacing. Clicking the title collapses the section to a narrow strip.
/// An optional power toggle in the title, bound to a parameter, dims the section's content.
class PanelSection final : public juce::Component
{
public:
    /// What a placement is, for the grid rules and the layout lint.
    enum class Kind
    {
        Knob,    // 1 x 3 cells, rows 0 or 3
        Line,    // toggles, combos, bars, segmented rows: 1 row per line
        Display, // any span
        Label    // a sub-label
    };

    struct Placement
    {
        juce::Component* component = nullptr;
        Kind kind = Kind::Display;
        int column = 0, row = 0, columns = 1, rows = 1;
    };

    PanelSection(DevicePanel& panel, std::string id, juce::String title, int columns,
                 std::optional<size_t> powerParam);
    ~PanelSection() override;

    /// Stable id: stored in projects and plugin state (docs/ids.md), never renamed.
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const juce::String& sectionTitle() const noexcept { return title_; }
    /// Retitles the section (e.g. the EQ's "Band 3"); the id stays.
    void setSectionTitle(juce::String title);
    [[nodiscard]] int columns() const noexcept { return columns_; }

    [[nodiscard]] bool isCollapsed() const noexcept { return collapsed_; }
    /// Collapses or expands the section. `byUser` tells the panel's host (view state changed).
    void setCollapsed(bool collapsed, bool byUser = false);
    /// The section's width on the grid: its columns with padding, or the collapsed strip.
    [[nodiscard]] int preferredWidth() const noexcept;

    /// False while the section's power parameter is off (true without one).
    [[nodiscard]] bool isPoweredOn() const;
    [[nodiscard]] juce::Component* powerToggle() const noexcept { return power_; }
    /// Binds the power toggle to another parameter (none: no toggle), e.g. the selected EQ band.
    void setPowerParam(std::optional<size_t> param);
    /// A power toggle for state that isn't a parameter (a rack's Morph, racks.md §18.3): `isOn`
    /// says whether it's on, read on every refresh; a click asks `onChange` to change it, and the
    /// toggle shows what `isOn` then says. Dims the content while off, as a parameter's does.
    void setPowerSwitch(std::function<bool()> isOn, std::function<void(bool on)> onChange,
                        const juce::String& tooltip);

    // ---- placement, in cells (each checks the grid rules) --------------------------------

    ParamKnob& knob(size_t param, int column, int row);
    /// A value bar, at least 2 columns wide.
    ParamKnob& bar(size_t param, int column, int row, int columns = 2);
    ParamToggle& toggle(size_t param, int column, int row, int columns = 1);
    /// A dropdown, at least 2 columns wide.
    ParamComboBox& combo(size_t param, int column, int row, int columns = 2);
    /// Segmented buttons filling columns x rows, the choices spread evenly over the rows.
    ParamSegmented& segmented(size_t param, int column, int row, int columns, int rows);
    /// Places a component owned elsewhere (usually a display member of the panel).
    void place(juce::Component& component, int column, int row, int columns, int rows, Kind kind = Kind::Display);
    /// A sub-label: a named group inside the section (use sparingly).
    void label(juce::String text, int column, int row, int columns);
    /// Removes every placement; controls the section created are destroyed.
    void clear();
    /// Marks `param` as set here by a control placed with place() (a display that edits it), so
    /// DevicePanel::showParameter finds it.
    void holdsParam(size_t param) { params_.push_back(param); }
    /// Whether a control here sets `param`.
    [[nodiscard]] bool holds(size_t param) const noexcept;

    [[nodiscard]] const std::vector<Placement>& placements() const noexcept { return placements_; }
    /// The content rows' area, in section coordinates.
    [[nodiscard]] juce::Rectangle<int> contentBounds() const;
    /// The cell span's area, relative to the content area.
    [[nodiscard]] static juce::Rectangle<int> cellBounds(int column, int row, int columns, int rows);

    /// Power dimming; the panel calls this on its refresh tick.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    friend class DevicePanel; // sets collapsed_ in bulk (view state, collapse all)
    class CapsLabel;
    void add(juce::Component& component, Kind kind, int column, int row, int columns, int rows);
    [[nodiscard]] juce::Rectangle<int> titleBounds() const;

    DevicePanel& panel_;
    std::string id_;
    juce::String title_;
    int columns_;
    std::optional<size_t> powerParam_;
    std::function<bool()> powerOn_;    // a power switch's state, when it isn't a parameter
    juce::Component* power_ = nullptr; // owned by the panel
    bool collapsed_ = false;

    juce::Component content_;
    std::vector<Placement> placements_;
    std::vector<juce::Component*> created_; // owned by the panel, destroyed by clear()
    std::vector<size_t> params_;            // what the controls here set
    std::vector<std::unique_ptr<juce::Component>> labels_;
};
} // namespace fd::ui
