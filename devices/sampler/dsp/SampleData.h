#pragma once

#include "fd/audiofile/AudioFile.h"
#include "fd/dsp/Resampling.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fd::devices
{
/// An immutable decoded sample, ready for playback (built off the audio thread, ADR 0017).
///
/// Level 0 is the file at its own rate; level k is decimated by 2^k, so a voice never reads
/// faster than twice a level's rate (devices/sampler/SPEC.md). Every level is padded with
/// silence on both sides for the interpolator.
struct SampleData
{
    static constexpr int maxLevels = 7;
    static constexpr int padding = dsp::SincKernels::padding;
    static constexpr int overviewBlock = 64; // frames per waveform overview point

    struct Level
    {
        std::array<std::vector<float>, 2> channels; // padded; empty second channel for mono
        int frames = 0;
    };

    std::string path;
    double sampleRate = 48000.0;
    int numChannels = 0; // 1 or 2 (further channels are ignored)
    int frames = 0;
    std::optional<int> rootKey;
    std::vector<Level> levels;
    /// Waveform overview for displays: min and max of both channels per overviewBlock frames.
    std::vector<float> overviewMin, overviewMax;

    [[nodiscard]] double seconds() const noexcept { return frames / sampleRate; }

    /// Start of channel `c` (0 or 1; mono reads channel 0) at `level`, past the padding.
    [[nodiscard]] const float* data(int level, int c) const noexcept
    {
        const auto& l = levels[static_cast<size_t>(level)];
        return l.channels[static_cast<size_t>(numChannels > 1 ? c : 0)].data() + padding;
    }

    /// Builds playback data from decoded audio (not real-time).
    [[nodiscard]] static std::shared_ptr<const SampleData> build(const audiofile::DecodedAudio& audio, std::string path);
    /// Decodes and builds (not real-time). Null on failure, with `error` set.
    [[nodiscard]] static std::shared_ptr<const SampleData> load(const std::string& path, std::string* error);
};
} // namespace fd::devices
