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

/// Knob bound to a parameter: text, ranges, default and stepping come from its ParamSpec.
class ParamKnob final : public Knob, public ParamControl
{
public:
    ParamKnob(DeviceUiContext& context, size_t paramIndex);
    void refresh() override;

private:
    DeviceUiContext& context_;
    size_t index_;
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
/// Segmented buttons for a Choice parameter, laid out in `columns` columns.
class ParamSegmented final : public juce::Component, public ParamControl
{
public:
    ParamSegmented(DeviceUiContext& context, size_t paramIndex, int columns);
    void refresh() override;
    void resized() override;

private:
    DeviceUiContext& context_;
    size_t index_;
    int columns_;
    juce::OwnedArray<Toggle> buttons_;
};

/// Flat dropdown for a Choice parameter.
class ParamComboBox final : public juce::ComboBox, public ParamControl
{
public:
    ParamComboBox(DeviceUiContext& context, size_t paramIndex);
    void refresh() override;

private:
    DeviceUiContext& context_;
    size_t index_;
};
} // namespace fd::ui
