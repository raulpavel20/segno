#pragma once

#include <fd/device/Device.h>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

namespace fd::gallery
{
/// The design-gate gallery (ADR 0014): switch live between candidate themes and see a real
/// device panel, controls in every state, a meter, clips on the arrangement canvas and the one
/// permitted gradient (an automation lane).
class GalleryComponent final : public juce::Component, private juce::Timer
{
public:
    explicit GalleryComponent(ui::LookAndFeel& lnf);
    ~GalleryComponent() override;

    void selectTheme(int index);
    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Feeds the demo devices so meters move (also used once before snapshots).
    void runDemoAudio(int numSamples);

private:
    void timerCallback() override { runDemoAudio(1600); }

    ui::LookAndFeel& lnf_;
    juce::OwnedArray<ui::Toggle> themeButtons_;
    int themeIndex_ = 0;

    struct DeviceSlot
    {
        std::unique_ptr<Device> device;
        std::unique_ptr<ui::LocalParamBinding> binding;
        std::unique_ptr<ui::DeviceUiContext> context;
        std::unique_ptr<ui::DevicePanel> panel;
    };
    std::vector<DeviceSlot> chain_;
    std::unique_ptr<juce::Component> arrangement_;

    std::vector<float> demoL_, demoR_;
    std::int64_t demoPos_ = 0;
};
} // namespace fd::gallery
