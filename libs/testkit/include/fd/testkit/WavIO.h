#pragma once

#include "fd/testkit/AudioBuffer.h"

#include <optional>
#include <string>

namespace fd::testkit
{
struct WavData
{
    AudioBuffer buffer;
    double sampleRate = 0.0;
};

/// Reads any PCM/float WAV as 32-bit float. Returns nullopt on failure.
[[nodiscard]] std::optional<WavData> readWav(const std::string& path);

/// Writes a 32-bit float WAV. Returns false on failure.
bool writeWav(const std::string& path, const AudioBuffer& buffer, double sampleRate);
} // namespace fd::testkit
