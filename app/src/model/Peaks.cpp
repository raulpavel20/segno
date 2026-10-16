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

PeakBuilder::PeakBuilder(double sampleRate, int numChannels, int channel)
    : peaks_(std::make_shared<Peaks>()), channels_(std::max(1, numChannels)),
      first_(channel >= 0 ? std::min(channel, channels_ - 1) : 0),
      last_(channel >= 0 ? first_ + 1 : channels_)
{
    peaks_->sampleRate = sampleRate;
    peaks_->levels.push_back({.binFrames = Peaks::baseBin});
}

void PeakBuilder::add(const float* interleaved, int frames)
{
    auto& l = peaks_->levels[0];
    for (int i = 0; i < frames; ++i)
    {
        for (int c = first_; c < last_; ++c)
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

std::shared_ptr<const Peaks> meanPeaks(const Peaks& a, const Peaks& b)
{
    auto out = std::make_shared<Peaks>();
    out->sampleRate = a.sampleRate;
    out->frames = std::min(a.frames, b.frames);
    for (size_t l = 0; l < std::min(a.levels.size(), b.levels.size()); ++l)
    {
        const auto& la = a.levels[l];
        const auto& lb = b.levels[l];
        Peaks::Level level{.binFrames = la.binFrames};
        const auto n = std::min(la.min.size(), lb.min.size());
        for (size_t i = 0; i < n; ++i)
        {
            level.min.push_back(0.5f * (la.min[i] + lb.min[i]));
            level.max.push_back(0.5f * (la.max[i] + lb.max[i]));
        }
        out->levels.push_back(std::move(level));
    }
    return out;
}

std::vector<std::shared_ptr<const Peaks>> buildChannelPeaks(const std::string& path, std::string* error)
{
    std::vector<PeakBuilder> builders;
    const auto info = audiofile::stream(
        path, 65536,
        [&](const audiofile::StreamInfo& format, const float* interleaved, int frames)
        {
            if (builders.empty())
                for (int c = 0; c < std::max(1, format.numChannels); ++c)
                    builders.emplace_back(format.sampleRate, format.numChannels, c);
            for (auto& b : builders)
                b.add(interleaved, frames);
            return true;
        },
        error);
    std::vector<std::shared_ptr<const Peaks>> out;
    if (!info)
        return out;
    for (auto& b : builders)
        out.push_back(b.finish());
    return out;
}
} // namespace fd::model
