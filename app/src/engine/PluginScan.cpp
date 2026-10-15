#include "engine/PluginScan.h"

#include <fd_ui/fd_ui.h>

namespace fd::engine
{
PluginScan::PluginScan(te::Engine& engine, Options options, std::function<void(const juce::String&)> progress,
                       std::function<void(Result)> done)
    : juce::Thread("Plugin scan"), engine_(engine), progress_(std::move(progress)), onDone_(std::move(done))
{
    auto& pm = engine_.getPluginManager();
    // The dead man's pedal: a plugin that crashes this process (LV2, read here) is skipped next
    // time.
    const auto pedal = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile(ui::brand::name)
                           .getChildFile("plugin-scan-in-progress.txt");
    pedal.getParentDirectory().createDirectory();
    for (auto* format : pm.pluginFormatManager.getFormats())
        if (format->canScanForPlugins() && !format->getName().containsIgnoreCase("internal"))
        {
            juce::FileSearchPath path;
            if (options.systemFolders)
                path = format->getDefaultLocationsToSearch();
            for (const auto& folder : options.folders)
                if (const juce::File f(juce::String::fromUTF8(folder.c_str())); f.isDirectory())
                    path.addIfNotAlreadyThere(f);
            if (path.getNumPaths() > 0)
                scanners_.push_back(std::make_unique<juce::PluginDirectoryScanner>(
                    pm.knownPluginList, *format, path, true, pedal, false));
        }
    startThread();
}

PluginScan::~PluginScan()
{
    signalThreadShouldExit();
    // tracktion's scanner waits on the child: let it give up now.
    if (auto& abort = engine_.getPluginManager().abortCurrentPluginScan)
        abort();
    stopThread(10000);
    cancelPendingUpdate();
}

juce::String PluginScan::current() const
{
    std::lock_guard lock(mutex_);
    return current_;
}

void PluginScan::run()
{
    auto& list = engine_.getPluginManager().knownPluginList;
    // A file the child crashed on is skipped from now on (JUCE's list keeps it, listed in
    // Settings with Try again): what joins that list is what this scan skipped.
    const auto skippedBefore = list.getBlacklistedFiles();
    for (auto& scanner : scanners_)
    {
        for (;;)
        {
            if (threadShouldExit())
                return;
            {
                std::lock_guard lock(mutex_);
                current_ = scanner->getNextPluginFileThatWillBeScanned();
            }
            triggerAsyncUpdate();
            juce::String name;
            if (!scanner->scanNextFile(true, name))
                break;
        }
    }
    if (threadShouldExit())
        return;
    int skipped = 0;
    for (const auto& file : list.getBlacklistedFiles())
        skipped += skippedBefore.contains(file) ? 0 : 1;
    scanners_.clear(); // the scan's end: tracktion lets its child go
    {
        std::lock_guard lock(mutex_);
        current_.clear();
        result_ = {list.getNumTypes(), skipped};
    }
    done_ = true;
    triggerAsyncUpdate();
}

void PluginScan::handleAsyncUpdate()
{
    const auto file = current();
    if (done_.load())
    {
        if (!reported_.exchange(true) && onDone_)
        {
            Result result;
            {
                std::lock_guard lock(mutex_);
                result = result_;
            }
            onDone_(result);
        }
        return;
    }
    if (progress_ && file.isNotEmpty())
        progress_(file);
}

bool PluginScan::serveAsChild(const juce::String& commandLine)
{
    return te::PluginManager::startChildProcessPluginScan(commandLine);
}
} // namespace fd::engine
