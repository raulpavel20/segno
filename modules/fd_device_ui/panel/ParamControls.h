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
void showParamMenu(DeviceUiContext& context, size_t paramIndex, juce::Component& control);

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

private:
    DeviceUiContext& context_;
    size_t index_;
    std::function<size_t()> which_;
};

/// Toggle bound to a Toggle-kind parameter.
class ParamToggle final : public Toggle, public ParamControl
{
public:
    ParamToggle(DeviceUiContext& context, size_t paramIndex);
    void refresh() override;
    void clicked() override;

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

/// Flat dropdown for a Choice parameter.
class ParamComboBox final : public juce::ComboBox, public ParamControl
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
    [[nodiscard]] const juce::String& text() const noexcept { return text_; }
    /// The delay came, went or changed (the header lays out again).
    std::function<void()> onChange;

private:
    juce::String moduleName_;
    std::function<double()> latencyMs_;
    juce::String text_;
};
} // namespace fd::ui
