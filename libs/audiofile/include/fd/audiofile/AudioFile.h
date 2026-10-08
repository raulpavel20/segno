#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fd::audiofile
{
/// Decoded audio, one vector per channel, 32-bit float.
struct DecodedAudio
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;
    /// Root note from the file's metadata (WAV `smpl`/`inst`, AIFF `INST`), if present.
    std::optional<int> rootKey;

    [[nodiscard]] int numChannels() const noexcept { return static_cast<int>(channels.size()); }
    [[nodiscard]] int numFrames() const noexcept { return channels.empty() ? 0 : static_cast<int>(channels[0].size()); }
};

/// Decodes WAV (incl. W64/RF64), AIFF, FLAC or MP3 from `path` (JUCE-free; not real-time).
/// The format is detected from the content. On failure returns nullopt and sets `error`.
[[nodiscard]] std::optional<DecodedAudio> decode(const std::string& path, std::string* error = nullptr);

/// Format of a file read by stream().
struct StreamInfo
{
    double sampleRate = 0.0;
    int numChannels = 0;
    std::int64_t numFrames = 0; // as reported by the header (MP3: estimated before reading)
};

/// Reads `path` block by block without loading it whole (large files, peak building). The
/// callback gets the format and interleaved float frames; returning false stops early. Same
/// formats as decode(). Returns the format, or nullopt (with `error`) if the file can't be read.
using BlockCallback = std::function<bool(const StreamInfo& info, const float* interleaved, int frames)>;
std::optional<StreamInfo> stream(const std::string& path, int blockFrames, const BlockCallback& onBlock,
                                 std::string* error = nullptr);

/// Writes a 32-bit float WAV, with a `smpl` root note when given. Returns false on failure.
bool writeWav(const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate,
              std::optional<int> rootKey = std::nullopt);
} // namespace fd::audiofile
