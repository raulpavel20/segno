#include "ui/CrossfadeSums.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cmath>

namespace fd::app
{
namespace
{
constexpr size_t kKept = 64; // sums kept at most (the least recently drawn go first)
constexpr int kChannels = 2;

/// `frames` stereo frames of `file` from `fromSeconds`, read at `rate` (nearest frame), interleaved;
/// empty if it can't be read.
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

std::shared_ptr<const model::Peaks> build(const CrossfadeSums::Key& key)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> probe(formats.createReaderFor(juce::File(key.out)));
    if (probe == nullptr)
        return nullptr;
    const double rate = probe->sampleRate;
    probe.reset();
    const int frames = static_cast<int>(std::ceil(key.length * rate)) + 1;
    const auto outgoing = readStereo(formats, key.out, key.outFrom, frames, rate);
    const auto incoming = readStereo(formats, key.in, key.inFrom, frames, rate);
    if (outgoing.empty() || incoming.empty())
        return nullptr;
    const auto sum = model::crossfadeSum(outgoing, incoming, kChannels, key.shape);
    model::PeakBuilder peaks(rate, kChannels);
    peaks.add(sum.data(), frames);
    return peaks.finish();
}
} // namespace

CrossfadeSums::CrossfadeSums() = default;

CrossfadeSums::~CrossfadeSums()
{
    *self_ = nullptr; // results arriving later are dropped
    pool_.removeAllJobs(true, 10000);
}

const model::Peaks* CrossfadeSums::get(const Key& key)
{
    auto& e = entries_[key];
    e.used = ++clock_;
    if (e.peaks != nullptr || e.building || e.failed)
        return e.peaks.get();
    e.building = true;
    // Keep the cache small: the least recently drawn finished sums go.
    while (entries_.size() > kKept)
    {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (!it->second.building && (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            break;
        entries_.erase(oldest);
    }
    std::weak_ptr<CrossfadeSums*> weak = self_;
    pool_.addJob([key, weak] {
        auto peaks = build(key);
        juce::MessageManager::callAsync([key, weak, result = std::move(peaks)]() mutable {
            if (auto self = weak.lock(); self && *self != nullptr)
                (*self)->finished(key, std::move(result));
        });
    });
    return nullptr;
}

void CrossfadeSums::finished(const Key& key, std::shared_ptr<const model::Peaks> peaks)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return;
    it->second.building = false;
    it->second.failed = peaks == nullptr;
    it->second.peaks = std::move(peaks);
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
