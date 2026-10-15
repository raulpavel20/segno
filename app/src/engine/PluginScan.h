#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::engine
{
namespace te = tracktion;

/// Scans plugin folders for third-party plugins (VST3, and LV2 on Linux) in the background (M3
/// C5; daw-shell.md, "Scanning"; settings.md, Plugins).
/// - **A plugin that crashes takes only a child process down:** where the engine scans out of
///   process (`EngineOptions::scanPluginsOutOfProcess`), tracktion looks at each VST3 in a child,
///   this program run again as a scanner (`serveAsChild`). A file the child crashes on (twice:
///   tracktion tries a fresh child once) is skipped: JUCE's list keeps it (Settings > Plugins:
///   Try again), and the scan goes on.
/// - **The window stays responsive:** the scan runs on a thread of its own, the child doing the
///   loading; progress and the end reach the message thread.
/// - LV2 is described in this process (tracktion sends only VST3 to the child); the dead man's
///   pedal skips one that crashed it, next time.
class PluginScan final : private juce::Thread, private juce::AsyncUpdater
{
public:
    struct Options
    {
        /// Folders to scan besides the system's (Settings > Plugins).
        std::vector<std::string> folders;
        /// The formats' own folders too (~/.vst3, /usr/lib/vst3 …). Off only in tests.
        bool systemFolders = true;
    };
    /// What a finished scan found: the plugins known now, and the files skipped this time.
    struct Result
    {
        int found = 0, skipped = 0;
    };

    /// Starts scanning. `progress` gets each file's name as it's looked at, `done` the result
    /// once; both on the message thread, never after it's destroyed.
    PluginScan(te::Engine& engine, Options options, std::function<void(const juce::String& file)> progress,
               std::function<void(Result)> done);
    /// Stops a scan under way: the file being scanned is abandoned, nothing else is skipped.
    ~PluginScan() override;

    [[nodiscard]] bool isDone() const noexcept { return done_.load(); }
    /// The file being looked at now (empty between files).
    [[nodiscard]] juce::String current() const;

    /// This process as tracktion's scanning child, if `commandLine` asks for one: it serves the
    /// parent's requests until the parent goes away, then exits. Call first thing at start-up,
    /// with a message loop to run (a JUCEApplication's, or `runDispatchLoop`).
    static bool serveAsChild(const juce::String& commandLine);

private:
    void run() override;
    void handleAsyncUpdate() override;

    te::Engine& engine_;
    std::function<void(const juce::String&)> progress_;
    std::function<void(Result)> onDone_;
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners_;
    mutable std::mutex mutex_;
    juce::String current_;
    Result result_;
    std::atomic<bool> done_{false}, reported_{false};
};
} // namespace fd::engine
