#include "fd/audiofile/AudioFile.h"

// The one place the dr_libs implementations are compiled (testkit uses them through here).
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fd::audiofile
{
namespace
{
void setError(std::string* error, std::string message)
{
    if (error != nullptr)
        *error = std::move(message);
}

/// Splits interleaved frames into channels and frees the dr_libs buffer with `freeFn`.
template <typename FreeFn>
DecodedAudio deinterleave(float* interleaved, unsigned channels, std::uint64_t frames, double rate, FreeFn freeFn)
{
    DecodedAudio out;
    out.sampleRate = rate;
    out.channels.assign(channels, std::vector<float>(static_cast<size_t>(frames)));
    for (std::uint64_t i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c)
            out.channels[c][static_cast<size_t>(i)] = interleaved[i * channels + c];
    freeFn(interleaved);
    return out;
}

std::optional<DecodedAudio> decodeWav(const std::string& path)
{
    drwav wav;
    if (!drwav_init_file_with_metadata(&wav, path.c_str(), 0, nullptr))
        return std::nullopt;
    std::optional<int> root;
    for (drwav_uint32 i = 0; i < wav.metadataCount; ++i)
    {
        const auto& m = wav.pMetadata[i];
        if (m.type == drwav_metadata_type_smpl && m.data.smpl.midiUnityNote <= 127)
            root = static_cast<int>(m.data.smpl.midiUnityNote);
        else if (m.type == drwav_metadata_type_inst && !root && m.data.inst.midiUnityNote >= 0)
            root = static_cast<int>(m.data.inst.midiUnityNote);
    }
    const unsigned channels = wav.channels;
    const auto frames = wav.totalPCMFrameCount;
    std::vector<float> interleaved(static_cast<size_t>(frames * channels));
    const auto read = drwav_read_pcm_frames_f32(&wav, frames, interleaved.data());
    const double rate = wav.sampleRate;
    drwav_uninit(&wav);
    if (channels == 0 || rate <= 0.0)
        return std::nullopt;
    auto out = deinterleave(interleaved.data(), channels, read, rate, [](float*) {});
    out.rootKey = root;
    return out;
}

std::optional<DecodedAudio> decodeFlac(const std::string& path)
{
    unsigned channels = 0, rate = 0;
    drflac_uint64 frames = 0;
    float* data = drflac_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &rate, &frames, nullptr);
    if (data == nullptr)
        return std::nullopt;
    return deinterleave(data, channels, frames, rate, [](float* p) { drflac_free(p, nullptr); });
}

std::optional<DecodedAudio> decodeMp3(const std::string& path)
{
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    float* data = drmp3_open_file_and_read_pcm_frames_f32(path.c_str(), &config, &frames, nullptr);
    if (data == nullptr)
        return std::nullopt;
    return deinterleave(data, config.channels, frames, config.sampleRate, [](float* p) { drmp3_free(p, nullptr); });
}

size_t writeProc(void* user, const void* data, size_t bytes)
{
    return std::fwrite(data, 1, bytes, static_cast<std::FILE*>(user));
}

drwav_bool32 seekProc(void* user, int offset, drwav_seek_origin origin)
{
    const int whence = origin == DRWAV_SEEK_SET ? SEEK_SET : origin == DRWAV_SEEK_END ? SEEK_END : SEEK_CUR;
    return std::fseek(static_cast<std::FILE*>(user), offset, whence) == 0;
}
} // namespace

std::optional<DecodedAudio> decode(const std::string& path, std::string* error)
{
    std::array<char, 12> head{};
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            setError(error, "cannot open " + path);
            return std::nullopt;
        }
        in.read(head.data(), head.size());
    }
    auto starts = [&](const char* tag) { return std::memcmp(head.data(), tag, std::strlen(tag)) == 0; };

    std::optional<DecodedAudio> out;
    if (starts("RIFF") || starts("RIFX") || starts("RF64") || starts("riff") || starts("FORM"))
        out = decodeWav(path); // dr_wav also reads W64 ("riff") and AIFF/AIFC ("FORM")
    else if (starts("fLaC"))
        out = decodeFlac(path);
    else
        out = decodeMp3(path); // MP3 has no reliable magic (ID3 tag or a frame sync)

    if (!out || out->numFrames() == 0)
    {
        setError(error, "unsupported or empty audio file: " + path);
        return std::nullopt;
    }
    return out;
}

std::optional<StreamInfo> stream(const std::string& path, int blockFrames, const BlockCallback& onBlock,
                                 std::string* error)
{
    std::array<char, 12> head{};
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            setError(error, "cannot open " + path);
            return std::nullopt;
        }
        in.read(head.data(), head.size());
    }
    auto starts = [&](const char* tag) { return std::memcmp(head.data(), tag, std::strlen(tag)) == 0; };
    blockFrames = std::max(1, blockFrames);

    // One loop for the three decoders: read(frames, buffer) -> frames read.
    auto run = [&](StreamInfo info, auto&& read) -> std::optional<StreamInfo> {
        if (info.numChannels <= 0 || info.sampleRate <= 0.0)
        {
            setError(error, "unsupported audio format: " + path);
            return std::nullopt;
        }
        std::vector<float> buffer(static_cast<size_t>(blockFrames) * static_cast<size_t>(info.numChannels));
        std::int64_t total = 0;
        for (;;)
        {
            const auto n = static_cast<int>(read(static_cast<std::uint64_t>(blockFrames), buffer.data()));
            if (n <= 0)
                break;
            total += n;
            if (!onBlock(info, buffer.data(), n))
                break;
        }
        info.numFrames = std::max(info.numFrames, total);
        return info;
    };

    if (starts("RIFF") || starts("RIFX") || starts("RF64") || starts("riff") || starts("FORM"))
    {
        drwav wav;
        if (!drwav_init_file(&wav, path.c_str(), nullptr))
        {
            setError(error, "unsupported or damaged audio file: " + path);
            return std::nullopt;
        }
        auto info = run({static_cast<double>(wav.sampleRate), static_cast<int>(wav.channels),
                         static_cast<std::int64_t>(wav.totalPCMFrameCount)},
                        [&](std::uint64_t n, float* out) { return drwav_read_pcm_frames_f32(&wav, n, out); });
        drwav_uninit(&wav);
        return info;
    }
    if (starts("fLaC"))
    {
        drflac* flac = drflac_open_file(path.c_str(), nullptr);
        if (flac == nullptr)
        {
            setError(error, "unsupported or damaged audio file: " + path);
            return std::nullopt;
        }
        auto info = run({static_cast<double>(flac->sampleRate), static_cast<int>(flac->channels),
                         static_cast<std::int64_t>(flac->totalPCMFrameCount)},
                        [&](std::uint64_t n, float* out) { return drflac_read_pcm_frames_f32(flac, n, out); });
        drflac_close(flac);
        return info;
    }
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, path.c_str(), nullptr))
    {
        setError(error, "unsupported or empty audio file: " + path);
        return std::nullopt;
    }
    auto info = run({static_cast<double>(mp3.sampleRate), static_cast<int>(mp3.channels), 0},
                    [&](std::uint64_t n, float* out) { return drmp3_read_pcm_frames_f32(&mp3, n, out); });
    drmp3_uninit(&mp3);
    if (info && info->numFrames == 0)
    {
        setError(error, "unsupported or empty audio file: " + path);
        return std::nullopt;
    }
    return info;
}

bool writeWav(const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate,
              std::optional<int> rootKey)
{
    if (channels.empty())
        return false;
    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = static_cast<drwav_uint32>(channels.size());
    format.sampleRate = static_cast<drwav_uint32>(sampleRate);
    format.bitsPerSample = 32;

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;

    drwav_metadata smpl{};
    smpl.type = drwav_metadata_type_smpl;
    if (rootKey)
        smpl.data.smpl.midiUnityNote = static_cast<drwav_uint32>(*rootKey);

    drwav wav;
    if (!drwav_init_write_with_metadata(&wav, &format, writeProc, seekProc, file, nullptr, rootKey ? &smpl : nullptr,
                                        rootKey ? 1u : 0u))
    {
        std::fclose(file);
        return false;
    }
    const size_t frames = channels[0].size();
    std::vector<float> interleaved(frames * channels.size());
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < channels.size(); ++c)
            interleaved[i * channels.size() + c] = channels[c][i];
    const auto written = drwav_write_pcm_frames(&wav, frames, interleaved.data());
    drwav_uninit(&wav);
    std::fclose(file);
    return written == frames;
}
} // namespace fd::audiofile
