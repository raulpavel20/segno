#pragma once

namespace fd::ui
{
/// Base for every module's panel (ui-principles §7, docs/design/device-panels.md, ADR 0022).
/// A fixed-height panel: a header (fold, power, name, module-wide settings) and titled sections
/// of controls on the module grid, with a meter strip at the right edge. Panels declare
/// sections and placements; this class computes all geometry. The same panel runs in plugin
/// editors and in the DAW's chain.
class DevicePanel : public juce::Component, private juce::Timer
{
public:
    explicit DevicePanel(DeviceUiContext& context);
    ~DevicePanel() override;

    /// Natural width: the sections (expanded or collapsed) and the meter strip, at least what
    /// the header needs; the folded strip when folded. The height is fixed by the theme.
    [[nodiscard]] virtual int preferredWidth() const;

    /// Optional bypass control in the header (null = hidden).
    std::function<void(bool enabled)> onEnabledChange;
    void setEnabledState(bool enabled);

    // ---- view state: collapsed sections and fold (never module state) -------------------

    /// `{"collapsed":[ids],"folded":true}` with defaults left out; empty when all default.
    [[nodiscard]] std::string viewState() const;
    /// Applies a stored view state. Tolerant: unknown ids and bad JSON are ignored. Does not
    /// call onViewChanged.
    void setViewState(std::string_view json);
    /// After the user collapses a section or folds the module: the host stores viewState()
    /// and lays the panel out again at its new preferredWidth().
    std::function<void()> onViewChanged;

    /// Whether the module can fold to a strip (the DAW's chain; not plugin editors).
    void setFoldable(bool foldable);
    [[nodiscard]] bool isFoldable() const noexcept { return foldable_; }
    [[nodiscard]] bool isFolded() const noexcept { return folded_; }
    void setFolded(bool folded, bool byUser = false);
    /// Collapses or expands every section.
    void setAllSectionsCollapsed(bool collapsed, bool byUser = false);

    [[nodiscard]] std::vector<PanelSection*> sections() const;
    [[nodiscard]] PanelSection* findSection(std::string_view id) const;
    /// The module-wide settings in the header (for the layout lint).
    [[nodiscard]] const std::vector<juce::Component*>& headerControls() const noexcept { return headerControls_; }
    /// The meters of the meter strip (for the layout lint).
    [[nodiscard]] std::vector<juce::Component*> meters() const;
    /// The width the header needs: fold, power, name and settings.
    [[nodiscard]] int headerMinimumWidth() const;

    /// True where a mouse-down may start reordering in a chain: the header's background, or
    /// anywhere on the folded strip, never on a control.
    [[nodiscard]] bool isDragHandle(juce::Point<int> position) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

protected:
    /// A titled section, appended left to right. `powerParam` (a toggle) dims its content when
    /// off. The id is stored in projects and plugin state: never rename it.
    PanelSection& addSection(std::string id, juce::String title, int columns,
                             std::optional<size_t> powerParam = std::nullopt);
    /// A module-wide quality or display setting (oversampling, analyzer), right-aligned in the
    /// header. Never one that shapes the sound's character.
    ParamComboBox& addHeaderCombo(size_t param);
    /// The meter strip at the right edge: meters owned by the panel, each captioned ("In",
    /// "Out") in the title row.
    void setMeters(std::vector<std::pair<juce::Component*, juce::String>> meters);

    /// Owns `control` (refreshed if it is a ParamControl) without placing it, e.g. a display's
    /// accessory.
    template <typename T>
    T& own(std::unique_ptr<T> control)
    {
        auto& ref = *control;
        controls_.push_back(std::move(control));
        return ref;
    }

    // ---- legacy layout (panels not yet on the grid) -----------------------------------------

    /// Lays out the body below the header. Panels on the grid don't override it.
    virtual void layoutBody(juce::Rectangle<int> body);
    /// Registers a bound control for periodic refresh. Returns it for convenience.
    template <typename T>
    T& bind(std::unique_ptr<T> control)
    {
        auto& ref = *control;
        addAndMakeVisible(ref);
        controls_.push_back(std::move(control));
        return ref;
    }

    DeviceUiContext& context() noexcept { return context_; }

    /// Dims `controls` while `inactive()` is true: settings the current mode or switch ignores
    /// stay visible and adjustable but recede (ui-principles §3). Checked on every refresh.
    void dimWhen(std::vector<juce::Component*> controls, std::function<bool()> inactive);

private:
    friend class PanelSection;
    /// Owns `control` and adds it to `parent` (a section's content).
    template <typename T>
    T& adopt(std::unique_ptr<T> control, juce::Component& parent)
    {
        auto& ref = *control;
        parent.addAndMakeVisible(ref);
        controls_.push_back(std::move(control));
        return ref;
    }
    void destroy(juce::Component* control);
    /// A section collapsed or expanded: lay out again, and tell the host when the user did it.
    void viewChanged(bool byUser);

    struct Dimming
    {
        std::vector<juce::Component*> controls;
        std::function<bool()> inactive;
    };
    void updateDimming();
    std::vector<Dimming> dimming_;

    void timerCallback() override;

    DeviceUiContext& context_;
    Toggle power_;
    IconButton fold_{Icon::ChevronLeft};
    std::vector<std::unique_ptr<juce::Component>> controls_;
    std::vector<std::unique_ptr<PanelSection>> sections_;
    std::vector<juce::Component*> headerControls_;
    std::vector<std::pair<juce::Component*, juce::String>> meters_;

    bool foldable_ = false, folded_ = false;
    juce::Rectangle<int> nameArea_;
    std::vector<int> separators_; // x of the lines between sections
};
} // namespace fd::ui
