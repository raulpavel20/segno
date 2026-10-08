#include "fd/testkit/WavIO.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <vector>

namespace fd::testkit
{
std::optional<WavData> readWav(const std::string& path)
{
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 frames = 0;
    float* interleaved = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &sampleRate, &frames, nullptr);
    if (interleaved == nullptr)
        return std::nullopt;

    WavData out;
    out.sampleRate = sampleRate;
    out.buffer.resize(static_cast<int>(channels), static_cast<int>(frames));
    for (drwav_uint64 i = 0; i < frames; ++i)
        for (unsigned int c = 0; c < channels; ++c)
            out.buffer.at(static_cast<int>(c), static_cast<int>(i)) = interleaved[i * channels + c];
    drwav_free(interleaved, nullptr);
    return out;
}

bool writeWav(const std::string& path, const AudioBuffer& buffer, double sampleRate)
{
    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = static_cast<drwav_uint32>(buffer.numChannels());
    format.sampleRate = static_cast<drwav_uint32>(sampleRate);
    format.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &format, nullptr))
        return false;

    const int channels = buffer.numChannels();
    std::vector<float> interleaved(static_cast<size_t>(buffer.numSamples() * channels));
    for (int i = 0; i < buffer.numSamples(); ++i)
        for (int c = 0; c < channels; ++c)
            interleaved[static_cast<size_t>(i * channels + c)] = buffer.at(c, i);

    const auto written =
        drwav_write_pcm_frames(&wav, static_cast<drwav_uint64>(buffer.numSamples()), interleaved.data());
    drwav_uninit(&wav);
    return written == static_cast<drwav_uint64>(buffer.numSamples());
}
} // namespace fd::testkit
