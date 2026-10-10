#pragma once

#include "model/Fades.h"
#include "model/Peaks.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace fd::app
{
/// The waveforms crossfades play (fades.md §3): the two clips' audio through the crossfade's gains,
/// summed, as peaks whose frame 0 is the crossfade's start. Built from the files' samples on a
/// background thread (a crossfade is short), and kept while drawn.
class CrossfadeSums
{
public:
    struct Key
    {
        std::string out, in;          // the outgoing and incoming clips' files
        double outFrom = 0.0, inFrom = 0.0; // where in each file the crossfade starts (seconds)
        double length = 0.0;          // seconds
        model::CrossfadeShape shape = model::CrossfadeShape::EqualPower;

        auto operator<=>(const Key&) const = default;
    };

    CrossfadeSums();
    ~CrossfadeSums();

    /// The sum's peaks if built, else null (and its building starts). For paint.
    const model::Peaks* get(const Key& key);
    /// Called on the message thread when a sum is ready.
    std::function<void()> onReady;
    /// Blocks until every requested sum is built (tests).
    void waitUntilIdle();

private:
    struct Entry
    {
        std::shared_ptr<const model::Peaks> peaks;
        bool building = false, failed = false;
        std::uint64_t used = 0;
    };
    void finished(const Key& key, std::shared_ptr<const model::Peaks> peaks);

    std::map<Key, Entry> entries_;
    std::uint64_t clock_ = 0;
    juce::ThreadPool pool_{juce::ThreadPoolOptions{}.withThreadName("Crossfades").withNumberOfThreads(1)};
    std::shared_ptr<CrossfadeSums*> self_ = std::make_shared<CrossfadeSums*>(this);
};
} // namespace fd::app
