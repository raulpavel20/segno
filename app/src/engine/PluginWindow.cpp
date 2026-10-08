#include "engine/PluginWindow.h"

namespace fd::engine
{
namespace te = tracktion;

PluginWindow::PluginWindow(te::Plugin& plugin)
    : DocumentWindow(plugin.getName(), juce::Colours::black, DocumentWindow::closeButton,
                     // On Linux the window is added to the desktop after it is set up (as tracktion does).
                     !JUCE_LINUX),
      plugin_(plugin)
{
    if (auto* external = dynamic_cast<te::ExternalPlugin*>(&plugin))
        if (auto* instance = external->getAudioPluginInstance())
        {
            editor_.reset(instance->createEditorAndMakeActive());
            if (editor_ == nullptr)
                editor_ = std::make_unique<juce::GenericAudioProcessorEditor>(*instance);
        }
    setUsingNativeTitleBar(true);
    if (editor_ != nullptr)
        setContentNonOwned(editor_.get(), true);
    setResizable(editor_ != nullptr && editor_->isResizable(), false);
    setBoundsConstrained(getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());
#if JUCE_LINUX
    setAlwaysOnTop(true);
    addToDesktop();
#endif
}

PluginWindow::~PluginWindow()
{
    plugin_.edit.flushPluginStateIfNeeded(plugin_);
    clearContentComponent();
    editor_.reset();
}

std::unique_ptr<juce::Component> PluginWindow::create(te::Plugin& plugin)
{
    if (auto* external = dynamic_cast<te::ExternalPlugin*>(&plugin); external == nullptr || external->getAudioPluginInstance() == nullptr)
        return nullptr;
    auto window = std::make_unique<PluginWindow>(plugin);
    if (window->editor_ == nullptr)
        return nullptr;
    window->setVisible(true);
    window->toFront(true);
    return window;
}

void PluginWindow::closeButtonPressed()
{
    plugin_.windowState->closeWindowExplicitly();
}
} // namespace fd::engine
