#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fd::model
{
/// Waveform overview of one audio file (C3): min/max of all channels per bin, at resolutions
/// of 16, 64, 256 ... frames per bin. Immutable once built; shared between clips of the file.
struct Peaks
{
    static constexpr int baseBin = 16;
    static constexpr int levelFactor = 4;

    struct Level
    {
        int binFrames = baseBin;
        std::vector<float> min, max;
    };

    double sampleRate = 0.0;
    std::int64_t frames = 0;
    std::vector<Level> levels; // [0] is the finest

    /// Min and max over source frames [from, to), from the coarsest level whose bins are no
    /// wider than the range (at most levelFactor bins are read per call in steady use).
    [[nodiscard]] std::pair<float, float> range(std::int64_t from, std::int64_t to) const noexcept;

    /// Approximate memory use in bytes.
    [[nodiscard]] std::size_t bytes() const noexcept;
};

/// Accumulates interleaved audio into Peaks block by block (used while streaming a file): every
/// channel's min and max together, or one channel's (`channel` ≥ 0).
class PeakBuilder
{
public:
    /// `channel`'s peaks; all channels' together (-1); or the first two's mean (meanOfTwo).
    PeakBuilder(double sampleRate, int numChannels, int channel = -1);
    static constexpr int meanOfTwo = -2;
    void add(const float* interleaved, int frames);
    /// Finishes the last bins and builds the coarser levels.
    [[nodiscard]] std::shared_ptr<const Peaks> finish();

private:
    std::shared_ptr<Peaks> peaks_;
    int channels_;
    int first_ = 0, last_ = 0; // the channels it reads
    bool mean_ = false;        // their mean (two of them), not each
    int inBin_ = 0;
    float lo_ = 0.0f, hi_ = 0.0f;
};

/// Streams `path` and builds its peaks (not real-time; call off the message thread for big
/// files). Null with `error` set if the file can't be read.
[[nodiscard]] std::shared_ptr<const Peaks> buildPeaks(const std::string& path, std::string* error = nullptr);
/// The same, a Peaks for each channel (the audio editor's rows, audio-editor.md §4), from one read
/// of the file; with two channels or more, one more after them: the first two's mean as Mono plays
/// it ((l + r) / 2, §6.3: out of phase, it cancels). Empty, with `error` set, if it can't be read.
[[nodiscard]] std::vector<std::shared_ptr<const Peaks>> buildChannelPeaks(const std::string& path,
                                                                          std::string* error = nullptr);
} // namespace fd::model
