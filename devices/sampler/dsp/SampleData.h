#pragma once

#include "fd/audiofile/AudioFile.h"
#include "fd/dsp/Resampling.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fd::devices
{
/// How a sample is held. One sample keeps every level as floats; a multisample's zones keep one
/// decimated copy, and a 16-bit file as 16-bit (SPEC, "State, loading and memory").
struct SampleOptions
{
    int levels = 7;       // SampleData::maxLevels
    bool compact = false; // a 16-bit file held as 16-bit
    bool operator==(const SampleOptions&) const = default;
};

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

    using Options = SampleOptions;

    struct Level
    {
        std::array<std::vector<float>, 2> channels;           // padded; empty second channel for mono
        std::array<std::vector<std::int16_t>, 2> compactData; // the same, held as 16-bit
        int frames = 0;
    };

    std::string path;
    double sampleRate = 48000.0;
    int numChannels = 0; // 1 or 2 (further channels are ignored)
    int frames = 0;
    std::optional<int> rootKey;
    bool compact = false; // levels held as 16-bit
    std::vector<Level> levels;
    /// Waveform overview for displays: min and max of both channels per overviewBlock frames.
    std::vector<float> overviewMin, overviewMax;

    [[nodiscard]] double seconds() const noexcept { return frames / sampleRate; }

    /// Start of channel `c` (0 or 1; mono reads channel 0) at `level`, past the padding. Float
    /// levels only.
    [[nodiscard]] const float* data(int level, int c) const noexcept
    {
        const auto& l = levels[static_cast<size_t>(level)];
        return l.channels[static_cast<size_t>(numChannels > 1 ? c : 0)].data() + padding;
    }

    /// The interpolated value of channel `c` at `level`, at fractional frame `pos` of that level.
    [[nodiscard]] float read(const dsp::SincKernels::Table& table, int level, int c,
                             double pos) const noexcept
    {
        const auto& l = levels[static_cast<size_t>(level)];
        const auto ch = static_cast<size_t>(numChannels > 1 ? c : 0);
        return compact ? dsp::SincKernels::read(table, l.compactData[ch].data() + padding, pos)
                       : dsp::SincKernels::read(table, l.channels[ch].data() + padding, pos);
    }

    /// The memory the levels take, in bytes.
    [[nodiscard]] size_t bytes() const noexcept;

    /// Builds playback data from decoded audio (not real-time).
    [[nodiscard]] static std::shared_ptr<const SampleData> build(const audiofile::DecodedAudio& audio,
                                                                 std::string path, Options options = {});
    /// Decodes and builds (not real-time), or gives the data already built for the same file
    /// with the same options, whichever Sampler holds it: one decode for every user. Null on
    /// failure, with `error` set. Any thread.
    [[nodiscard]] static std::shared_ptr<const SampleData> load(const std::string& path, std::string* error,
                                                                Options options = {});
};
} // namespace fd::devices
