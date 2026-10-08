#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
/// A third-party plugin's own editor in a floating window (the only floating windows in the app,
/// ui-principles §2). tracktion creates it through UIBehaviour::createPluginWindow.
class PluginWindow final : public juce::DocumentWindow
{
public:
    explicit PluginWindow(tracktion::Plugin& plugin);
    ~PluginWindow() override;

    /// The window for `plugin`, shown; null if the plugin has no editor.
    static std::unique_ptr<juce::Component> create(tracktion::Plugin& plugin);

    void closeButtonPressed() override;

private:
    tracktion::Plugin& plugin_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
};
} // namespace fd::engine
