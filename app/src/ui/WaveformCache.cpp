#include "ui/WaveformCache.h"

#include <juce_events/juce_events.h>

namespace fd::app
{
WaveformCache::WaveformCache() = default;

WaveformCache::~WaveformCache()
{
    *self_ = nullptr; // results arriving later are dropped
    pool_.removeAllJobs(true, 10000);
}

void WaveformCache::request(const std::string& path)
{
    if (path.empty())
        return;
    auto& e = entries_[path];
    if (e.peaks != nullptr || e.building || e.failed)
        return;
    e.building = true;
    std::weak_ptr<WaveformCache*> weak = self_;
    pool_.addJob([path, weak] {
        auto peaks = model::buildPeaks(path);
        juce::MessageManager::callAsync([path, weak, result = std::move(peaks)]() mutable {
            if (auto self = weak.lock(); self && *self != nullptr)
                (*self)->finished(path, std::move(result));
        });
    });
}

void WaveformCache::finished(const std::string& path, std::shared_ptr<const model::Peaks> peaks)
{
    auto& e = entries_[path];
    e.building = false;
    e.failed = peaks == nullptr;
    e.peaks = std::move(peaks);
    if (onReady)
        onReady();
}

const model::Peaks* WaveformCache::find(const std::string& path) const
{
    const auto it = entries_.find(path);
    return it != entries_.end() ? it->second.peaks.get() : nullptr;
}

void WaveformCache::waitUntilIdle()
{
    while (pool_.getNumJobs() > 0)
        juce::Thread::sleep(2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // deliver the results
}
} // namespace fd::app
