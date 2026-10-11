#pragma once

#include "model/Browser.h"

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>

namespace fd::engine
{
/// The files of the browser's places (docs/design/browser.md §9): found and read in the
/// background, kept in an index file so search works from the first second. A file whose size
/// and time haven't changed isn't read again. The index is the message thread's; batches arrive
/// there.
class FileIndexer final : private juce::Thread
{
public:
    FileIndexer();
    ~FileIndexer() override;
    FileIndexer(const FileIndexer&) = delete;
    FileIndexer& operator=(const FileIndexer&) = delete;

    /// Reads the index kept in `file` and keeps it there from now on (none: in memory only).
    void setIndexFile(const juce::File& file);
    /// The folders to index, and folders under them to leave out (the Segno folder's presets and
    /// unsaved projects). Places no longer listed leave the index; new ones are indexed.
    void setPlaces(std::vector<juce::File> places, std::vector<juce::File> excluded = {});
    /// Looks at every place again (or only `folder`, opened in the browser).
    void refresh(const juce::File& folder = {});

    [[nodiscard]] const model::BrowserIndexFile& index() const noexcept { return index_; }
    [[nodiscard]] bool isIndexing() const noexcept { return busy_.load() || pending_.load() > 0; }
    /// Called on the message thread when files came, went or changed.
    std::function<void()> onChanged;
    /// Waits until the work asked for is done and delivered (tests).
    void waitUntilIdle();

    /// What a file is, read from its header (a sample's length, rate, channels, loop tempo).
    [[nodiscard]] static std::optional<model::IndexedFile> read(const juce::File& file);

private:
    void run() override;
    void deliver(const std::string& place, std::vector<model::IndexedFile> files);
    void save() const;

    model::BrowserIndexFile index_;
    juce::File indexFile_;
    std::vector<juce::File> places_, excluded_;

    std::mutex mutex_; // guards the work list
    std::vector<juce::File> work_;
    std::map<std::string, std::vector<model::IndexedFile>> known_; // the worker's copy, by place
    std::atomic<bool> busy_{false};                                // the worker has a place to look at
    std::atomic<int> pending_{0}; // places looked at, on their way to the message thread
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
} // namespace fd::engine
