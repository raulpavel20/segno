#include "TableFiles.h"

#include "fd/audiofile/AudioFile.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fd::devices::wavetable
{
namespace
{
std::vector<unsigned char> readBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t u32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t u16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

bool validFrameSize(int n)
{
    return n >= 2 && n <= 4096 && (n & (n - 1)) == 0;
}

/// The frame size a WAV's metadata gives, or 0.
int wavFrameSize(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return 0;
    size_t at = 12;
    while (at + 8 <= bytes.size())
    {
        const auto* id = bytes.data() + at;
        const std::uint32_t size = u32(id + 4);
        const auto* data = id + 8;
        const size_t available = std::min<size_t>(size, bytes.size() - at - 8);
        if (std::memcmp(id, "clm ", 4) == 0 && available >= 4)
        {
            // "<!>2048 ...": the frame size after the marker.
            const std::string text(reinterpret_cast<const char*>(data), std::min<size_t>(available, 32));
            if (const auto marker = text.find("<!>"); marker != std::string::npos)
            {
                const int n = std::atoi(text.c_str() + marker + 3);
                if (validFrameSize(n))
                    return n;
            }
        }
        if ((std::memcmp(id, "srge", 4) == 0 || std::memcmp(id, "srgo", 4) == 0) && available >= 8)
            if (const auto n = static_cast<int>(u32(data + 4)); validFrameSize(n))
                return n;
        if (std::memcmp(id, "uhWT", 4) == 0)
            return 2048;
        at += 8 + size + (size & 1);
    }
    return 0;
}

std::string stem(const std::string& path)
{
    return std::filesystem::path(path).stem().string();
}
} // namespace

std::optional<ImportedTable> importTable(const std::string& path, int frameLength, std::string* error)
{
    const auto fail = [error](std::string why) -> std::optional<ImportedTable>
    {
        if (error != nullptr)
            *error = std::move(why);
        return std::nullopt;
    };
    const auto bytes = readBytes(path);
    if (bytes.empty())
        return fail("The file can't be read.");
    ImportedTable t;
    t.name = stem(path);
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "vawt", 4) == 0)
    {
        // Surge's .wt: frame size, frame count, flags (int16, full-range int16), then the data.
        const int size = static_cast<int>(u32(bytes.data() + 4));
        const int count = u16(bytes.data() + 8);
        const int flags = u16(bytes.data() + 10);
        if (!validFrameSize(size) || count < 1)
            return fail("This .wt file's frames aren't a size a table can have.");
        const bool int16 = (flags & 0x4) != 0;
        const float scale = int16 ? ((flags & 0x8) != 0 ? 1.0f / 32768.0f : 1.0f / 16384.0f) : 1.0f;
        const size_t needed = 12 + static_cast<size_t>(size) * count * (int16 ? 2 : 4);
        if (bytes.size() < needed)
            return fail("This .wt file is shorter than its header says.");
        t.frameLength = frameLength > 0 ? frameLength : size;
        t.samples.resize(static_cast<size_t>(size) * count);
        for (size_t i = 0; i < t.samples.size(); ++i)
        {
            const auto* p = bytes.data() + 12 + i * (int16 ? 2 : 4);
            if (int16)
                t.samples[i] = static_cast<float>(static_cast<std::int16_t>(u16(p))) * scale;
            else
            {
                const std::uint32_t raw = u32(p);
                float v = 0.0f;
                std::memcpy(&v, &raw, sizeof(v));
                t.samples[i] = v;
            }
        }
    }
    else
    {
        std::string why;
        const auto audio = audiofile::decode(path, &why);
        if (!audio || audio->numFrames() < 2)
            return fail(why.empty() ? "The file isn't audio a table can be made from." : why);
        const int n = audio->numFrames();
        t.samples.assign(static_cast<size_t>(n), 0.0f);
        for (const auto& ch : audio->channels)
            for (int i = 0; i < n; ++i)
                t.samples[static_cast<size_t>(i)] +=
                    ch[static_cast<size_t>(i)] / static_cast<float>(audio->numChannels());
        int frame = frameLength > 0 ? frameLength : wavFrameSize(bytes);
        if (frame <= 0)
            frame = n == 16384 ? 256 : n <= 4096 ? n : 2048;
        t.frameLength = frame;
    }
    t.frames = std::clamp(static_cast<int>(t.samples.size()) / std::max(2, t.frameLength), 0,
                          dsp::Wavetable::maxFrames);
    if (t.frames < 1)
        return fail("The file is shorter than one frame.");
    t.samples.resize(static_cast<size_t>(t.frames) * static_cast<size_t>(t.frameLength));
    return t;
}

bool isTableFile(const std::string& path)
{
    // The chunks that give the frame size come before the data: the head of the file is enough.
    std::vector<unsigned char> head(65536);
    {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<size_t>(std::max<std::streamsize>(0, in.gcount())));
    }
    if (head.size() >= 4 && std::memcmp(head.data(), "vawt", 4) == 0)
        return true;
    if (wavFrameSize(head) > 0)
        return true;
    std::int64_t frames = -1;
    audiofile::stream(path, 4096,
                      [&frames](const audiofile::StreamInfo& info, const float*, int)
                      {
                          frames = info.numFrames;
                          return false;
                      });
    return frames == 16384 || (frames > 1 && frames <= 4096);
}

std::optional<MonoAudio> readMono(const std::string& path, std::string* error)
{
    std::string why;
    const auto audio = audiofile::decode(path, &why);
    if (!audio || audio->numFrames() < 2)
    {
        if (error != nullptr)
            *error = why.empty() ? "The file isn't audio." : why;
        return std::nullopt;
    }
    MonoAudio out;
    out.sampleRate = audio->sampleRate;
    out.samples.assign(static_cast<size_t>(audio->numFrames()), 0.0f);
    for (const auto& ch : audio->channels)
        for (size_t i = 0; i < out.samples.size(); ++i)
            out.samples[i] += ch[i] / static_cast<float>(audio->numChannels());
    return out;
}

std::optional<dsp::Wavetable> importCycles(const std::vector<std::string>& paths, std::string* error)
{
    std::vector<std::complex<float>> bins;
    int frames = 0;
    for (const auto& path : paths)
    {
        if (frames == dsp::Wavetable::maxFrames)
            break;
        auto one = importTable(path, 0, error);
        if (!one)
            continue;
        // The whole file is the cycle.
        const auto table = dsp::Wavetable::fromCycles(
            one->samples.data(), static_cast<int>(one->samples.size()) / one->frames, 1, false);
        bins.insert(bins.end(), table.spectrum(0), table.spectrum(0) + dsp::Wavetable::maxHarmonic + 1);
        ++frames;
    }
    if (frames == 0)
    {
        if (error != nullptr && error->empty())
            *error = "None of the files is a single cycle.";
        return std::nullopt;
    }
    return dsp::Wavetable::fromSpectra(std::move(bins), frames);
}

std::string toBase64(const std::vector<unsigned char>& bytes)
{
    static constexpr char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const std::uint32_t v = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                                (i + 2 < bytes.size() ? bytes[i + 2] : 0u);
        out.push_back(k[(v >> 18) & 63]);
        out.push_back(k[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? k[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? k[v & 63] : '=');
    }
    return out;
}

std::vector<unsigned char> fromBase64(const std::string& text)
{
    const auto value = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    std::vector<unsigned char> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text)
    {
        const int v = value(c);
        if (v < 0)
            continue;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}
namespace
{
void putU32(std::vector<unsigned char>& out, std::uint32_t v)
{
    for (int k = 0; k < 4; ++k)
        out.push_back(static_cast<unsigned char>((v >> (8 * k)) & 0xFF));
}

void putU16(std::vector<unsigned char>& out, std::uint16_t v)
{
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

void putFloat(std::vector<unsigned char>& out, float v)
{
    std::uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(v));
    putU32(out, raw);
}

bool writeBytes(const std::vector<unsigned char>& bytes, const std::string& path, std::string* error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (out.good())
        return true;
    if (error != nullptr)
        *error = "The file can't be written.";
    return false;
}
} // namespace

bool exportWav(const dsp::Wavetable& table, const std::string& path, std::string* error)
{
    // RIFF/WAVE: fmt (32-bit float, mono, 48 kHz), clm ("<!>2048"), data.
    const int n = dsp::Wavetable::frameSize;
    const auto samples = static_cast<std::uint32_t>(table.frames()) * static_cast<std::uint32_t>(n);
    const std::string clm = "<!>" + std::to_string(n);
    std::vector<unsigned char> b;
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    putU32(b, 0); // filled below
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    putU32(b, 16);
    putU16(b, 3); // IEEE float
    putU16(b, 1);
    putU32(b, 48000);
    putU32(b, 48000 * 4);
    putU16(b, 4);
    putU16(b, 32);
    b.insert(b.end(), {'c', 'l', 'm', ' '});
    putU32(b, static_cast<std::uint32_t>(clm.size()));
    b.insert(b.end(), clm.begin(), clm.end());
    if (clm.size() % 2 != 0)
        b.push_back(0); // chunks are padded to even sizes
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    putU32(b, samples * 4);
    for (int f = 0; f < table.frames(); ++f)
        for (int i = 0; i < n; ++i)
            putFloat(b, table.samples(f)[i]);
    const auto riff = static_cast<std::uint32_t>(b.size() - 8);
    std::memcpy(b.data() + 4, &riff, 4); // little-endian hosts only, as the rest of the format code
    return writeBytes(b, path, error);
}

bool exportWt(const dsp::Wavetable& table, const std::string& path, std::string* error)
{
    // "vawt", the frame size, the frame count, flags 0 (float data).
    const int n = dsp::Wavetable::frameSize;
    std::vector<unsigned char> b{'v', 'a', 'w', 't'};
    putU32(b, static_cast<std::uint32_t>(n));
    putU16(b, static_cast<std::uint16_t>(table.frames()));
    putU16(b, 0);
    for (int f = 0; f < table.frames(); ++f)
        for (int i = 0; i < n; ++i)
            putFloat(b, table.samples(f)[i]);
    return writeBytes(b, path, error);
}

} // namespace fd::devices::wavetable
