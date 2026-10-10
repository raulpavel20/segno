#include "ui/CrossfadeSums.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>

namespace fd::app
{
namespace
{
constexpr size_t kJoins = 16;     // joins kept at most (the least recently drawn go first)
constexpr size_t kMade = 4;       // sums kept per join
constexpr double kLeast = 0.5;    // seconds read each side of a join, at least
constexpr double kRoom = 10.0;    // room to grow read past a crossfade, at most
constexpr int kChannels = 2;

/// `frames` stereo frames of `file` from `fromSeconds` (silence before its start), read at `rate`
/// (nearest frame), interleaved; empty if it can't be read.
std::vector<float> readStereo(juce::AudioFormatManager& formats, const std::string& file, double fromSeconds, int frames, double rate)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(file)));
    if (reader == nullptr || frames <= 0)
        return {};
    const double ratio = reader->sampleRate / rate;
    const auto first = static_cast<juce::int64>(std::floor(fromSeconds * reader->sampleRate));
    const int span = static_cast<int>(std::ceil(frames * ratio)) + 2;
    juce::AudioBuffer<float> buffer(kChannels, span);
    reader->read(&buffer, 0, span, first, true, true); // a mono file fills both channels
    std::vector<float> out(static_cast<size_t>(frames) * kChannels);
    for (int f = 0; f < frames; ++f)
    {
        const int i = std::clamp(static_cast<int>(std::lround(f * ratio)), 0, span - 1);
        for (int c = 0; c < kChannels; ++c)
            out[static_cast<size_t>(f * kChannels + c)] = buffer.getSample(c, i);
    }
    return out;
}

std::shared_ptr<const CrossfadeSums::Window> readWindow(const CrossfadeSums::Join& join, double reach)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> probe(formats.createReaderFor(juce::File(join.out)));
    if (probe == nullptr)
        return nullptr;
    auto w = std::make_shared<CrossfadeSums::Window>();
    w->rate = probe->sampleRate;
    w->before = w->after = reach;
    probe.reset();
    const int frames = static_cast<int>(std::ceil(2.0 * reach * w->rate)) + 1;
    w->out = readStereo(formats, join.out, join.outAt - reach, frames, w->rate);
    w->in = readStereo(formats, join.in, join.inAt - reach, frames, w->rate);
    if (w->out.empty() || w->in.empty())
        return nullptr;
    return w;
}

/// How far each side of the join a crossfade over [from, from + length] reaches.
double reachOf(double from, double length)
{
    return std::max({0.0, -from, from + length});
}

/// The sum from the window, or null if the window doesn't hold the crossfade.
std::shared_ptr<const model::Peaks> sumFrom(const CrossfadeSums::Window& w, double from, double length, model::CrossfadeShape shape)
{
    const auto first = static_cast<long>(std::lround((w.before + from) * w.rate));
    const auto frames = static_cast<long>(std::ceil(length * w.rate)) + 1;
    const auto held = static_cast<long>(w.out.size() / kChannels);
    if (first < 0 || first + frames > held)
        return nullptr;
    const auto a = w.out.begin() + first * kChannels, b = w.in.begin() + first * kChannels;
    const std::vector<float> outgoing(a, a + frames * kChannels), incoming(b, b + frames * kChannels);
    const auto sum = model::crossfadeSum(outgoing, incoming, kChannels, shape);
    model::PeakBuilder peaks(w.rate, kChannels);
    peaks.add(sum.data(), static_cast<int>(frames));
    return peaks.finish();
}
} // namespace

CrossfadeSums::CrossfadeSums() = default;

CrossfadeSums::~CrossfadeSums()
{
    *self_ = nullptr; // results arriving later are dropped
    pool_.removeAllJobs(true, 10000);
}

CrossfadeSums::Sum CrossfadeSums::get(const Join& join, double from, double length, model::CrossfadeShape shape)
{
    auto& e = entries_[join];
    e.used = ++clock_;
    auto latest = [&]() -> Sum {
        if (e.made.empty())
            return {};
        const auto& m = e.made.back();
        return {m.peaks.get(), m.from, m.length};
    };
    for (auto it = e.made.begin(); it != e.made.end(); ++it)
        if (it->from == from && it->length == length && it->shape == shape)
        {
            std::rotate(it, it + 1, e.made.end()); // the newest now
            return latest();
        }
    if (e.window != nullptr)
        if (auto peaks = sumFrom(*e.window, from, length, shape))
        {
            e.made.push_back({from, length, shape, std::move(peaks)});
            if (e.made.size() > kMade)
                e.made.erase(e.made.begin());
            return latest();
        }
    // Not held yet: read around the join, with room to grow, and draw the last sum meanwhile.
    if (!e.failed)
    {
        const double reach = reachOf(from, length);
        if (reach > e.reading)
            read(join, e, std::max(kLeast, reach + std::min(reach, kRoom)));
    }
    return latest();
}

void CrossfadeSums::read(const Join& join, Entry& entry, double reach)
{
    entry.reading = reach;
    // Keep the cache small: the least recently drawn joins go.
    while (entries_.size() > kJoins)
    {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.reading == 0.0 && (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            break;
        entries_.erase(oldest);
    }
    std::weak_ptr<CrossfadeSums*> weak = self_;
    pool_.addJob([join, reach, weak] {
        auto window = readWindow(join, reach);
        juce::MessageManager::callAsync([join, reach, weak, result = std::move(window)]() mutable {
            if (auto self = weak.lock(); self && *self != nullptr)
                (*self)->finished(join, reach, std::move(result));
        });
    });
}

void CrossfadeSums::finished(const Join& join, double reach, std::shared_ptr<const Window> window)
{
    const auto it = entries_.find(join);
    if (it == entries_.end() || it->second.reading != reach) // a wider read is on its way
        return;
    it->second.reading = 0.0;
    it->second.failed = window == nullptr;
    if (window != nullptr)
        it->second.window = std::move(window);
    if (onReady)
        onReady();
}

void CrossfadeSums::waitUntilIdle()
{
    while (pool_.getNumJobs() > 0)
        juce::Thread::sleep(2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // deliver the results
}
} // namespace fd::app
