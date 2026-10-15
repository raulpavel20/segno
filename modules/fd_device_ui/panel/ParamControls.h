#pragma once

namespace fd::ui
{
/// A control bound to one device parameter. DevicePanel calls refresh() periodically so
/// changes from automation, presets or the host show up.
class ParamControl
{
public:
    virtual ~ParamControl() = default;
    virtual void refresh() = 0;
};

/// A parameter control's right-click menu (ui-principles §7): its automation (in the DAW) and
/// reset to default.
void showParamMenu(DeviceUiContext& context, size_t paramIndex, juce::Component& control,
                   const std::function<void(juce::PopupMenu&)>& extra = {});
/// A parameter's modulation items (device-panels.md, "Modulation"): its routes with Remove, Remove
/// route for a route's own control, and Modulate with ▸ the sources by group. Each action is
/// wrapped by `whileAlive` (it runs only while the control is there).
void addModulationItems(ModulationModel& model, size_t paramIndex, juce::PopupMenu& menu,
                        const std::function<std::function<void()>(std::function<void()>)>& whileAlive);

/// Knob bound to a parameter: text, ranges, default and stepping come from its ParamSpec.
class ParamKnob final : public Knob, public ParamControl
{
public:
    ParamKnob(DeviceUiContext& context, size_t paramIndex);
    void refresh() override;
    /// Turns another parameter from now on: its name, value text, default, steps and automation.
    void bind(size_t paramIndex);
    /// Turns whichever parameter `which` names, checked on every refresh: one knob for two
    /// parameters a switch chooses between (the Delay's time: milliseconds, or note values while
    /// Sync is on).
    void follow(std::function<size_t()> which);
    [[nodiscard]] size_t param() const noexcept { return index_; }
    /// A knob that's also a modulation source (a macro): its name strip is the source's handle
    /// (device-panels.md, "Modulation").
    void setModulationSource(int source);

    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    DeviceUiContext& context_;
    size_t index_;
    std::function<size_t()> which_;
    int source_ = -1;
    std::optional<int> ringRoute_; // the route a drag on the ring sets
    float ringAmount_ = 0.0f;
};

/// Toggle bound to a Toggle-kind parameter.
class ParamToggle final : public Toggle, public ParamControl
{
public:
    ParamToggle(DeviceUiContext& context, size_t paramIndex);
    void refresh() override;
    void clicked() override;
    /// The parameter it sets.
    [[nodiscard]] size_t param() const noexcept { return index_; }

private:
    DeviceUiContext& context_;
    size_t index_;
};
/// Segmented buttons for a Choice parameter, `columns` buttons per row.
class ParamSegmented final : public juce::Component, public ParamControl
{
public:
    ParamSegmented(DeviceUiContext& context, size_t paramIndex, int columns);
    void refresh() override;
    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    /// The buttons (for the layout lint).
    [[nodiscard]] const juce::OwnedArray<Toggle>& buttons() const noexcept { return buttons_; }

private:
    DeviceUiContext& context_;
    size_t index_;
    int columns_;
    juce::OwnedArray<Toggle> buttons_;
    AutomationState automation_ = AutomationState::None;
};

/// Which of several the sections after it show (an oscillator, a filter, an LFO): segmented
/// buttons in a choice's look, `columns` a row, but view state, never a parameter.
class Selector final : public juce::Component
{
public:
    Selector(juce::StringArray choices, int columns);
    void setSelected(int index, juce::NotificationType notification = juce::dontSendNotification);
    [[nodiscard]] int selected() const noexcept { return selected_; }
    /// After a click on another choice.
    std::function<void(int index)> onSelect;
    void resized() override;
    /// The buttons (for the layout lint).
    [[nodiscard]] const juce::OwnedArray<Toggle>& buttons() const noexcept { return buttons_; }

private:
    int columns_, selected_ = 0;
    juce::OwnedArray<Toggle> buttons_;
};

/// Lays out segmented buttons on the grid's own gaps, `columns` a row (ParamSegmented, Selector).
void layoutSegments(const juce::OwnedArray<Toggle>& buttons, int columns, juce::Rectangle<int> area);

/// Flat dropdown in the module look, not bound to a parameter (an editor's setting, ADR 0053),
/// optionally with its name inside before its choice ("Fill  Spectral"), as a bar shows its name.
class ChoiceBox : public juce::ComboBox
{
public:
    /// Shows `name` inside, before the choice (empty: none).
    void setInlineName(juce::String name);
    /// The name it shows inside (empty without).
    [[nodiscard]] juce::String inlineName() const { return inlineName_; }
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String inlineName_;
};

/// Flat dropdown for a Choice parameter.
class ParamComboBox final : public ChoiceBox, public ParamControl
{
public:
    ParamComboBox(DeviceUiContext& context, size_t paramIndex);
    void refresh() override;
    void paintOverChildren(juce::Graphics& g) override;
    /// Each choice's line in the menu, where it says more than the choice's name (the EQ's
    /// Processing and Resolution name their delay). The box itself shows the name.
    void setMenuText(std::function<juce::String(int choice)> text) { menuText_ = std::move(text); }
    /// A choice's line in the menu.
    [[nodiscard]] juce::String menuText(int choice) const
    {
        return menuText_ ? menuText_(choice) : getItemText(choice);
    }
    void showPopup() override;
    /// Its menu closed with `chosen` (an item id; 0 for none): it may open again, and takes the
    /// choice. Public for tests.
    void menuClosed(int chosen);
    /// Shows its parameter's short name inside, before its choice ("Mode  Super"), where nothing
    /// else names it (device-panels.md, "Controls on the grid").
    void setInlineName(bool shown);

private:
    DeviceUiContext& context_;
    size_t index_;
    AutomationState automation_ = AutomationState::None;
    std::function<juce::String(int)> menuText_;
};

/// The delay a module adds, at the header's right end while it adds one: "+107 ms" in the
/// warning colour, and in its tooltip what that means for playing live (devices/eq/SPEC.md,
/// "Latency"). For modules whose delay is a choice.
class LatencyBadge final : public juce::Component, public juce::SettableTooltipClient, public ParamControl
{
public:
    LatencyBadge(juce::String moduleName, std::function<double()> latencyMs);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    /// Its width on the header: none while the module adds no delay.
    [[nodiscard]] int preferredWidth() const;
    /// Its width showing `text`.
    [[nodiscard]] static int widthFor(const juce::String& text);
    [[nodiscard]] const juce::String& text() const noexcept { return text_; }
    /// The delay came, went or changed (the header lays out again).
    std::function<void()> onChange;

private:
    juce::String moduleName_;
    std::function<double()> latencyMs_;
    juce::String text_;
};
/// A module's live figure in its header (the Wavetable Synth's CPU, W13): its value in the mono,
/// `textSecondary`, as wide as the widest it can show, so the header never moves as it changes.
class HeaderReadout final : public juce::Component, public juce::SettableTooltipClient, public ParamControl
{
public:
    HeaderReadout(std::function<juce::String()> text, juce::String widest);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    [[nodiscard]] int preferredWidth() const;
    [[nodiscard]] const juce::String& text() const noexcept { return text_; }

private:
    std::function<juce::String()> textOf_;
    juce::String widest_, text_;
};
} // namespace fd::ui
