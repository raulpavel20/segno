#include "fd/testkit/WavIO.h"

#include "fd/audiofile/AudioFile.h"

namespace fd::testkit
{
std::optional<WavData> readWav(const std::string& path)
{
    const auto decoded = audiofile::decode(path);
    if (!decoded)
        return std::nullopt;
    WavData out;
    out.sampleRate = decoded->sampleRate;
    out.buffer.resize(decoded->numChannels(), decoded->numFrames());
    for (int c = 0; c < decoded->numChannels(); ++c)
        for (int i = 0; i < decoded->numFrames(); ++i)
            out.buffer.at(c, i) = decoded->channels[static_cast<size_t>(c)][static_cast<size_t>(i)];
    return out;
}

bool writeWav(const std::string& path, const AudioBuffer& buffer, double sampleRate)
{
    std::vector<std::vector<float>> channels(static_cast<size_t>(buffer.numChannels()));
    for (int c = 0; c < buffer.numChannels(); ++c)
        channels[static_cast<size_t>(c)].assign(buffer.channel(c), buffer.channel(c) + buffer.numSamples());
    return audiofile::writeWav(path, channels, sampleRate);
}
} // namespace fd::testkit
