#pragma once

#include "model/AudioAnalysis.h"

#include <atomic>
#include <deque>
#include <functional>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace fd::engine
{
/// Finds what's in audio files in the background (model::analyseFile: transients, how percussive,
/// the tempo; audio-editor.md §7), one file at a time, each once while it's unchanged on disk.
class FileAnalyser final : private juce::Thread
{
public:
    FileAnalyser();
    ~FileAnalyser() override;
    FileAnalyser(const FileAnalyser&) = delete;
    FileAnalyser& operator=(const FileAnalyser&) = delete;

    /// The file's analysis; null while it's being found (asking starts it) or if it can't be read.
    /// Thread-safe.
    [[nodiscard]] std::shared_ptr<const model::FileAnalysis> get(const juce::File& file);
    /// The file's analysis if it's been found: nothing starts (the snapshot reads with this).
    [[nodiscard]] std::shared_ptr<const model::FileAnalysis> peek(const juce::File& file);
    /// The file's analysis now: found here and kept if it wasn't (a short file placed, whose tempo
    /// says whether it warps, audio-editor.md §7.2). Null if it can't be read.
    [[nodiscard]] std::shared_ptr<const model::FileAnalysis> now(const juce::File& file);
    /// Called on the message thread when a file's analysis is ready (its full path).
    std::function<void(const std::string& path)> onFound;
    /// Waits until every file asked for has been analysed and delivered (tests).
    void waitUntilIdle();

private:
    struct Entry
    {
        juce::Time modified;
        std::shared_ptr<const model::FileAnalysis> found;
        bool failed = false, queued = false;
    };
    void run() override;

    std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    std::deque<std::string> queue_;
    std::atomic<int> pending_{0};
    std::atomic<bool> stopping_{false};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
} // namespace fd::engine
