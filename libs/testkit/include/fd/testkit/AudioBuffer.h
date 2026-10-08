#pragma once

#include <cassert>
#include <vector>

namespace fd::testkit
{
/// Simple owning multichannel float buffer for tests and offline tools (not for real-time use).
class AudioBuffer
{
public:
    AudioBuffer() = default;
    AudioBuffer(int numChannels, int numSamples) { resize(numChannels, numSamples); }

    void resize(int numChannels, int numSamples)
    {
        data_.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(numSamples), 0.0f));
        numSamples_ = numSamples;
    }

    void clear()
    {
        for (auto& ch : data_)
            std::fill(ch.begin(), ch.end(), 0.0f);
    }

    [[nodiscard]] int numChannels() const noexcept { return static_cast<int>(data_.size()); }
    [[nodiscard]] int numSamples() const noexcept { return numSamples_; }

    float* channel(int c) noexcept { return data_[static_cast<size_t>(c)].data(); }
    [[nodiscard]] const float* channel(int c) const noexcept { return data_[static_cast<size_t>(c)].data(); }

    float& at(int c, int i) noexcept
    {
        assert(i >= 0 && i < numSamples_);
        return data_[static_cast<size_t>(c)][static_cast<size_t>(i)];
    }
    [[nodiscard]] float at(int c, int i) const noexcept { return data_[static_cast<size_t>(c)][static_cast<size_t>(i)]; }

    /// Raw channel pointers, e.g. for building a device ProcessContext.
    std::vector<float*> channelPointers()
    {
        std::vector<float*> p;
        for (auto& ch : data_)
            p.push_back(ch.data());
        return p;
    }

    /// Copy of samples [start, start + length) of every channel.
    [[nodiscard]] AudioBuffer slice(int start, int length) const
    {
        AudioBuffer out(numChannels(), length);
        for (int c = 0; c < numChannels(); ++c)
            for (int i = 0; i < length; ++i)
                out.at(c, i) = at(c, start + i);
        return out;
    }

private:
    std::vector<std::vector<float>> data_;
    int numSamples_ = 0;
};
} // namespace fd::testkit
