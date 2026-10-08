#pragma once

#include "engine/DevicePlugin.h"

namespace fd::app
{
/// M0 DAW skeleton: plays a looping audio file through a native Utility in a docked device
/// chain. Drop a WAV/AIFF/FLAC onto the window to replace the file. This is a pipeline proof,
/// not the real shell (that is M1: arrangement view, transport, device chain).
class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit MainComponent(tracktion::Engine& engine);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void loadFile(const juce::File& file);
    void showAudioSettings();
    static juce::File createDemoLoop();

    tracktion::Engine& engine_;
    std::unique_ptr<tracktion::Edit> edit_;
    tracktion::Plugin::Ptr plugin_;
    std::unique_ptr<engine::DevicePluginBinding> binding_;
    std::unique_ptr<ui::DeviceUiContext> context_;
    std::unique_ptr<ui::DevicePanel> panel_;

    ui::Toggle play_{"Play"};
    juce::TextButton audioSettings_{"Audio settings"};
    juce::String fileName_;
};
} // namespace fd::app
