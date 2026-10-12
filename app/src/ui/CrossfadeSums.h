#pragma once

#include "model/Fades.h"
#include "model/Peaks.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fd::app
{
/// The waveforms crossfades play (fades.md §3): the two clips' audio through the crossfade's gains,
/// summed, as peaks. Each join's audio around it is read once on a background thread and kept, so
/// a crossfade being dragged gets its sum at once on every step, never blinking back to the clips'
/// own waveforms.
class CrossfadeSums
{
public:
    /// Where two clips meet: their files, each file's position at the join (seconds), and their
    /// loops (ADR 0048): around the join, a looped clip's audio wraps into its loop.
    struct Join
    {
        std::string out, in;
        double outAt = 0.0, inAt = 0.0;
        model::ClipLoop outLoop, inLoop;

        auto operator<=>(const Join&) const = default;
    };
    /// A sum and where it lies: from `from` seconds after the join (negative: before) for `length`.
    struct Sum
    {
        const model::Peaks* peaks = nullptr;
        double from = 0.0, length = 0.0;
    };

    CrossfadeSums();
    ~CrossfadeSums();

    /// The sum of a crossfade at `join` over [from, from + length] (seconds from the join). Until
    /// the join's audio there has been read: the last sum made at this join, with its own span (none
    /// before the first); reading starts, and onReady tells when it's done. For paint.
    Sum get(const Join& join, double from, double length, model::CrossfadeShape shape);
    /// Called on the message thread when a join's audio has been read.
    std::function<void()> onReady;
    /// Blocks until every requested read is done (tests).
    void waitUntilIdle();

    /// The audio kept around a join: both files from `before` seconds before it to `after` after,
    /// stereo, interleaved, at `rate`.
    struct Window
    {
        double rate = 0.0, before = 0.0, after = 0.0;
        std::vector<float> out, in;
    };

private:
    struct Made
    {
        double from = 0.0, length = 0.0;
        model::CrossfadeShape shape = model::CrossfadeShape::EqualPower;
        std::shared_ptr<const model::Peaks> peaks;
    };
    struct Entry
    {
        std::shared_ptr<const Window> window;
        double reading = 0.0;          // the reach (seconds each side) being read; 0: none
        bool failed = false;
        std::vector<Made> made;        // the latest sums, newest last
        std::uint64_t used = 0;
    };
    void finished(const Join& join, double reach, std::shared_ptr<const Window> window);
    void read(const Join& join, Entry& entry, double reach);

    std::map<Join, Entry> entries_;
    std::uint64_t clock_ = 0;
    juce::ThreadPool pool_{juce::ThreadPoolOptions{}.withThreadName("Crossfades").withNumberOfThreads(1)};
    std::shared_ptr<CrossfadeSums*> self_ = std::make_shared<CrossfadeSums*>(this);
};
} // namespace fd::app
