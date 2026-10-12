#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
/// Measurements of rendered audio (the MCP render tool, docs/design/mcp.md §5; later bounce and
/// export). Levels in dBFS (-inf for silence); loudness and true peak by the dynamics core's
/// meters (ITU-R BS.1770, EBU R 128).
struct AudioMeasurements
{
    struct Band
    {
        std::string name;
        double low = 0.0, high = 0.0; // Hz
        double levelDb = 0.0;         // average level relative to a full-scale sine in the band
    };

    double seconds = 0.0;
    double sampleRate = 0.0;
    int channels = 0;
    double peakDb = 0.0;          // sample peak over all channels
    double truePeakDb = 0.0;      // dBTP: 4x oversampled
    double integratedLufs = 0.0;  // gated, over the whole range (-inf: silent or under 400 ms)
    double shortTermMaxLufs = 0.0; // the loudest 3 s (-inf: under 3 s)
    double momentaryMaxLufs = 0.0; // the loudest 400 ms
    std::optional<double> loudnessRange; // LU (EBU Tech 3342): none under 3 s or for silence
    double rmsDb = 0.0;           // over all channels
    std::vector<double> channelPeakDb, channelRmsDb;
    double crestDb = 0.0;         // peak over RMS (0 for silence)
    std::int64_t clippedSamples = 0; // samples at or beyond full scale
    std::optional<double> correlation; // stereo, -1..1: none for mono or silence
    std::vector<Band> bands;      // spectral balance, the channels' average
    bool silent = true;           // nothing above -120 dBFS
};

/// Measures `channels` (one vector per channel, equal lengths).
[[nodiscard]] AudioMeasurements measureAudio(const std::vector<std::vector<float>>& channels, double sampleRate);

/// A 16-bit PCM WAV file in memory, for render results an assistant can listen to.
[[nodiscard]] std::vector<std::uint8_t> wav16(const std::vector<std::vector<float>>& channels, double sampleRate);
} // namespace fd::model
