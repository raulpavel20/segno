#pragma once

namespace fd::ui
{
/// Base for every device's panel (ui-principles §7): fixed height, header with power, name and
/// actions; body laid out by the device on the 4 px grid. The same panel runs in plugin
/// editors and in the DAW's device chain.
class DevicePanel : public juce::Component, private juce::Timer
{
public:
    explicit DevicePanel(DeviceUiContext& context);
    ~DevicePanel() override;

    /// Natural width of the body (the height is fixed by the theme).
    [[nodiscard]] virtual int preferredWidth() const = 0;

    /// Optional bypass control in the header (null = hidden).
    std::function<void(bool enabled)> onEnabledChange;
    void setEnabledState(bool enabled);

    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    /// Lay out the body area (below the header).
    virtual void layoutBody(juce::Rectangle<int> body) = 0;

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
    std::vector<std::unique_ptr<juce::Component>> controls_;
};
} // namespace fd::ui
