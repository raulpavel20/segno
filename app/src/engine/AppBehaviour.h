#pragma once

#include "engine/PluginWindow.h"

#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
/// tracktion_engine's hooks into the app UI. Warnings appear in the shell (no modal dialogs,
/// ui-principles §1); background tasks run to completion before the call returns (M1 uses them
/// only for rendering).
class AppBehaviour final : public tracktion::UIBehaviour
{
public:
    std::function<void(const juce::String&)> onWarning;

    void showWarningMessage(const juce::String& message) override
    {
        if (onWarning)
            onWarning(message);
    }

    std::unique_ptr<juce::Component> createPluginWindow(tracktion::PluginWindowState& state) override
    {
        if (auto* ws = dynamic_cast<tracktion::Plugin::WindowState*>(&state))
            return PluginWindow::create(ws->plugin);
        return nullptr;
    }

    void runTaskWithProgressBar(tracktion::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }
    }
};
} // namespace fd::engine
