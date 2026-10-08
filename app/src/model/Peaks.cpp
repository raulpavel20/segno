#include "model/Peaks.h"

#include "fd/audiofile/AudioFile.h"

#include <algorithm>

namespace fd::model
{
std::pair<float, float> Peaks::range(std::int64_t from, std::int64_t to) const noexcept
{
    if (levels.empty() || to <= 0 || from >= frames)
        return {0.0f, 0.0f};
    from = std::max<std::int64_t>(0, from);
    to = std::max(from + 1, std::min(to, frames));
    size_t level = 0;
    while (level + 1 < levels.size() && levels[level + 1].binFrames <= to - from)
        ++level;
    const auto& l = levels[level];
    const auto first = static_cast<size_t>(from / l.binFrames);
    const auto last = std::min(l.min.size(), static_cast<size_t>((to - 1) / l.binFrames) + 1);
    float lo = 0.0f, hi = 0.0f;
    for (size_t b = first; b < last; ++b)
    {
        lo = std::min(lo, l.min[b]);
        hi = std::max(hi, l.max[b]);
    }
    return {lo, hi};
}

std::size_t Peaks::bytes() const noexcept
{
    std::size_t n = 0;
    for (const auto& l : levels)
        n += (l.min.size() + l.max.size()) * sizeof(float);
    return n;
}

PeakBuilder::PeakBuilder(double sampleRate, int numChannels)
    : peaks_(std::make_shared<Peaks>()), channels_(std::max(1, numChannels))
{
    peaks_->sampleRate = sampleRate;
    peaks_->levels.push_back({.binFrames = Peaks::baseBin});
}

void PeakBuilder::add(const float* interleaved, int frames)
{
    auto& l = peaks_->levels[0];
    for (int i = 0; i < frames; ++i)
    {
        for (int c = 0; c < channels_; ++c)
        {
            const float v = interleaved[i * channels_ + c];
            lo_ = std::min(lo_, v);
            hi_ = std::max(hi_, v);
        }
        if (++inBin_ == Peaks::baseBin)
        {
            l.min.push_back(lo_);
            l.max.push_back(hi_);
            inBin_ = 0;
            lo_ = hi_ = 0.0f;
        }
    }
    peaks_->frames += frames;
}

std::shared_ptr<const Peaks> PeakBuilder::finish()
{
    if (inBin_ > 0)
    {
        peaks_->levels[0].min.push_back(lo_);
        peaks_->levels[0].max.push_back(hi_);
        inBin_ = 0;
    }
    while (peaks_->levels.back().min.size() > 1)
    {
        const auto& fine = peaks_->levels.back();
        Peaks::Level coarse{.binFrames = fine.binFrames * Peaks::levelFactor};
        for (size_t b = 0; b < fine.min.size(); b += Peaks::levelFactor)
        {
            const auto end = std::min(fine.min.size(), b + Peaks::levelFactor);
            coarse.min.push_back(*std::min_element(fine.min.begin() + static_cast<long>(b), fine.min.begin() + static_cast<long>(end)));
            coarse.max.push_back(*std::max_element(fine.max.begin() + static_cast<long>(b), fine.max.begin() + static_cast<long>(end)));
        }
        peaks_->levels.push_back(std::move(coarse));
    }
    return std::move(peaks_);
}

std::shared_ptr<const Peaks> buildPeaks(const std::string& path, std::string* error)
{
    std::optional<PeakBuilder> builder;
    const auto info = audiofile::stream(
        path, 65536,
        [&](const audiofile::StreamInfo& format, const float* interleaved, int frames) {
            if (!builder)
                builder.emplace(format.sampleRate, format.numChannels);
            builder->add(interleaved, frames);
            return true;
        },
        error);
    if (!info || !builder)
        return nullptr;
    return builder->finish();
}
} // namespace fd::model
