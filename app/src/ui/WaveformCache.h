#pragma once

#include "model/Peaks.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <string>

namespace fd::app
{
/// Peaks per audio file, built on background threads (C3). Message thread only; paint code
/// (which runs with the message thread locked, ADR 0015) may call find().
class WaveformCache
{
public:
    WaveformCache();
    ~WaveformCache();

    /// Starts building `path`'s peaks unless they exist or are being built.
    void request(const std::string& path);
    /// The peaks if ready, else null. No side effects (safe in paint).
    [[nodiscard]] const model::Peaks* find(const std::string& path) const;
    /// Blocks until every requested file is built (tests and benchmarks).
    void waitUntilIdle();

    /// Called on the message thread when new peaks are ready.
    std::function<void()> onReady;

private:
    struct Entry
    {
        std::shared_ptr<const model::Peaks> peaks;
        bool building = false;
        bool failed = false;
    };
    void finished(const std::string& path, std::shared_ptr<const model::Peaks> peaks);

    std::map<std::string, Entry> entries_;
    juce::ThreadPool pool_{juce::ThreadPoolOptions{}.withThreadName("Peaks").withNumberOfThreads(2)};
    std::shared_ptr<WaveformCache*> self_ = std::make_shared<WaveformCache*>(this); // for async callbacks
};
} // namespace fd::app
