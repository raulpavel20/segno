#include "engine/FileAnalyser.h"

#include <juce_events/juce_events.h>

namespace fd::engine
{
FileAnalyser::FileAnalyser() : juce::Thread("Segno file analysis")
{
    startThread(juce::Thread::Priority::low);
}

FileAnalyser::~FileAnalyser()
{
    alive_->store(false);
    signalThreadShouldExit();
    notify();
    stopThread(10000);
}

std::shared_ptr<const model::FileAnalysis> FileAnalyser::get(const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    const auto modified = file.getLastModificationTime();
    {
        std::lock_guard lock(mutex_);
        auto& e = entries_[path];
        // Changed on disk since: found again.
        if (e.modified != modified && !e.queued)
        {
            e = Entry{modified, nullptr, false, false};
        }
        if (e.found != nullptr || e.failed)
            return e.found;
        if (e.queued)
            return nullptr;
        e.queued = true;
        queue_.push_back(path);
        ++pending_;
    }
    notify();
    return nullptr;
}

void FileAnalyser::waitUntilIdle()
{
    while (pending_.load() > 0)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        else
            juce::Thread::sleep(5);
    }
}

void FileAnalyser::run()
{
    while (!threadShouldExit())
    {
        std::string path;
        {
            std::lock_guard lock(mutex_);
            if (!queue_.empty())
            {
                path = queue_.front();
                queue_.pop_front();
            }
        }
        if (path.empty())
        {
            wait(-1);
            continue;
        }
        auto found = model::analyseFile(path);
        std::shared_ptr<const model::FileAnalysis> shared =
            found ? std::make_shared<const model::FileAnalysis>(std::move(*found)) : nullptr;
        {
            std::lock_guard lock(mutex_);
            auto& e = entries_[path];
            e.queued = false;
            e.found = shared;
            e.failed = shared == nullptr;
        }
        juce::MessageManager::callAsync(
            [this, alive = alive_, path]
            {
                if (!alive->load())
                    return;
                --pending_;
                if (onFound)
                    onFound(path);
            });
    }
}
} // namespace fd::engine
