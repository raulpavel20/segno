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
    [[nodiscard]] int preferredWidth() const;

    /// Optional bypass control in the header (null = hidden).
    std::function<void(bool enabled)> onEnabledChange;
    void setEnabledState(bool enabled);

    // ---- view state: collapsed sections, fold and page (never module state) ------------

    /// `{"collapsed":[ids],"folded":true,"page":id}` with defaults left out; empty when all default.
    [[nodiscard]] std::string viewState() const;
    /// Applies a stored view state. Tolerant: unknown ids and bad JSON are ignored. Does not
    /// call onViewChanged.
    void setViewState(std::string_view json);
    /// After the user collapses a section, folds the module or turns a page: the host stores
    /// viewState() and lays the panel out again at its new preferredWidth().
    std::function<void()> onViewChanged;

    // ---- pages (ADR 0051): a very wide module's sections in groups, tabs in the header ------

    /// 0 for a module without pages (every section on one row).
    [[nodiscard]] int pageCount() const noexcept { return static_cast<int>(pages_.size()); }
    [[nodiscard]] int page() const noexcept { return page_; }
    [[nodiscard]] const std::string& pageId(int index) const
    {
        return pages_.at(static_cast<size_t>(index)).id;
    }
    /// Turns to page `index` (clamped). The module keeps its width.
    void setPage(int index, bool byUser = false);
    /// The next (+1) or previous (-1) page, round from the last to the first (View > Next and
    /// Previous Module Page).
    void turnPage(int delta, bool byUser = false);
    /// The sections on page `index`: every section for a module without pages.
    [[nodiscard]] std::vector<PanelSection*> pageSections(int index) const;
    /// The sections shown: the page's.
    [[nodiscard]] std::vector<PanelSection*> shownSections() const { return pageSections(page_); }
    /// The page tabs (for the layout lint): null for a module without pages.
    [[nodiscard]] const TabRow* pageTabs() const noexcept { return pages_.empty() ? nullptr : &pageTabs_; }
    // ---- modulation (device-panels.md, "Modulation") ----------------------------------------

    /// A modulation source dragged from its handle, the pointer at `position` (in the panel):
    /// lights the knob there that can take it, and turns to a page whose tab it's held on.
    void dragSource(int source, juce::Point<int> position);
    /// Let go at `position`: a route to the knob there, if any; nothing when `cancelled`.
    void dropSource(int source, juce::Point<int> position, bool cancelled);

    /// Shows where `param` is set: turns to the page of the section that holds its control.
    /// False if no section does. Panels that show one of several (an oscillator) select it first.
    virtual bool showParameter(size_t param);

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
    /// The header's presets (ADR 0044): null where the host has none (snapshots, the gallery).
    [[nodiscard]] PresetBar* presetBar() const noexcept { return presetBar_.get(); }
    /// The meters of the meter strip (for the layout lint).
    [[nodiscard]] std::vector<juce::Component*> meters() const;
    /// The width the header needs: fold, power, name, presets and settings.
    [[nodiscard]] int headerMinimumWidth() const;
    /// Where the header's name is drawn (for the layout lint).
    [[nodiscard]] juce::Rectangle<int> nameArea() const noexcept { return nameArea_; }

    /// True where a mouse-down may start reordering in a chain: the header's background, or
    /// anywhere on the folded strip, never on a control.
    [[nodiscard]] bool isDragHandle(juce::Point<int> position) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

protected:
    /// Starts a page: the sections added after it are on it, up to the next. Before the first
    /// section, or not at all. The id is stored in view state: never rename it.
    void addPage(std::string id, juce::String title);
    /// A titled section, appended left to right. `powerParam` (a toggle) dims its content when
    /// off. The id is stored in projects and plugin state: never rename it.
    PanelSection& addSection(std::string id, juce::String title, int columns,
                             std::optional<size_t> powerParam = std::nullopt);
    /// A module-wide quality or display setting (oversampling, analyzer), right-aligned in the
    /// header with its name as a caption. Never one that shapes the sound's character.
    ParamComboBox& addHeaderCombo(size_t param);
    /// Shows a header setting only while `shown()` is true (the EQ's Resolution, which matters
    /// only while there's a delay); otherwise it takes no room. The panel's width keeps room for it.
    void showHeaderWhen(juce::Component& setting, std::function<bool()> shown);
    /// The delay the module adds, while it adds one: a badge at the header's right end ("+107
    /// ms", in the warning colour) whose tooltip says what it means for playing live. For modules
    /// whose delay is a choice (the EQ's linear phase; devices/eq/SPEC.md, "Latency").
    LatencyBadge& showLatency(std::function<double()> latencyMs);
    /// Names the sidechain picker for what this module does with it (the Spectrum's "Compare"),
    /// instead of "Sidechain".
    void setSidechainCaption(juce::String caption, juce::String tooltip);
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

    DeviceUiContext& context() noexcept { return context_; }

    /// Dims `controls` while `inactive()` is true: settings the current mode or switch ignores
    /// stay visible and adjustable but recede (ui-principles §3). Checked on every refresh.
    void dimWhen(std::vector<juce::Component*> controls, std::function<bool()> inactive);

private:
    friend class PanelSection;
    /// Owns `control` and adds it to `parent` (a section's content, or the panel's header).
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
    /// Turns to the page a dragged source has been held on long enough.
    void springLoad();

    DeviceUiContext& context_;
    Toggle power_;
    IconButton fold_{Icon::ChevronLeft};
    std::vector<std::unique_ptr<juce::Component>> controls_;
    std::vector<std::unique_ptr<PanelSection>> sections_;
    std::vector<juce::Component*> headerControls_;
    std::vector<juce::String> headerCaptions_;          // one per header control
    std::vector<std::function<bool()>> headerShown_; // one per header control: shown while true (or always)
    std::vector<bool> headerShowing_;                // laid out as shown
    std::vector<juce::Rectangle<int>> captionAreas_;    // laid out
    std::vector<std::pair<juce::Component*, juce::String>> meters_;

    std::unique_ptr<PresetBar> presetBar_;
    struct Page
    {
        std::string id;
        juce::String title;
    };
    std::vector<Page> pages_;
    std::vector<int> sectionPage_; // each section's page
    int page_ = 0;
    TabRow pageTabs_;
    int springTab_ = -1;       // the page tab a dragged source is held on
    double springSince_ = 0.0; // since when (ms)
    bool foldable_ = false, folded_ = false;
    juce::Rectangle<int> nameArea_;
    std::vector<int> separators_; // x of the lines between sections
};
} // namespace fd::ui
