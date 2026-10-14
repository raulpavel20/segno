#include "SampleData.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <tuple>

namespace fd::devices
{
namespace
{
std::vector<float> padded(const std::vector<float>& v)
{
    std::vector<float> out(v.size() + 2 * SampleData::padding, 0.0f);
    std::copy(v.begin(), v.end(), out.begin() + SampleData::padding);
    return out;
}

/// The same as 16-bit: exact for a 16-bit file's own samples, rounded for its decimated copies.
std::vector<std::int16_t> padded16(const std::vector<float>& v)
{
    std::vector<std::int16_t> out(v.size() + 2 * SampleData::padding, 0);
    std::transform(
        v.begin(), v.end(), out.begin() + SampleData::padding, [](float x)
        { return static_cast<std::int16_t>(std::clamp(std::lround(x * 32768.0f), -32768L, 32767L)); });
    return out;
}

/// What a decoded file is known by: where it is, when it last changed, how it's held.
struct CacheKey
{
    std::string path;
    std::filesystem::file_time_type modified;
    std::uintmax_t size = 0;
    int levels = 0;
    bool compact = false;
    bool operator<(const CacheKey& o) const
    {
        return std::tie(path, modified, size, levels, compact) <
               std::tie(o.path, o.modified, o.size, o.levels, o.compact);
    }
};

struct Cache
{
    std::mutex mutex;
    std::map<CacheKey, std::weak_ptr<const SampleData>> entries;
};

Cache& cache()
{
    static Cache c;
    return c;
}
} // namespace

size_t SampleData::bytes() const noexcept
{
    size_t total = 0;
    for (const auto& l : levels)
        for (size_t c = 0; c < 2; ++c)
            total += l.channels[c].size() * sizeof(float) + l.compactData[c].size() * sizeof(std::int16_t);
    return total;
}

std::shared_ptr<const SampleData> SampleData::build(const audiofile::DecodedAudio& audio, std::string path,
                                                    Options options)
{
    auto s = std::make_shared<SampleData>();
    s->path = std::move(path);
    s->sampleRate = audio.sampleRate;
    s->numChannels = std::min(2, audio.numChannels());
    s->frames = audio.numFrames();
    s->rootKey = audio.rootKey;
    s->compact = options.compact && audio.bitsPerSample > 0 && audio.bitsPerSample <= 16;

    std::array<std::vector<float>, 2> current;
    for (int c = 0; c < s->numChannels; ++c)
        current[static_cast<size_t>(c)] = audio.channels[static_cast<size_t>(c)];
    for (int level = 0; level < std::clamp(options.levels, 1, maxLevels); ++level)
    {
        Level l;
        l.frames = static_cast<int>(current[0].size());
        for (int c = 0; c < s->numChannels; ++c)
        {
            const auto& from = current[static_cast<size_t>(c)];
            if (s->compact)
                l.compactData[static_cast<size_t>(c)] = padded16(from);
            else
                l.channels[static_cast<size_t>(c)] = padded(from);
        }
        s->levels.push_back(std::move(l));
        if (current[0].size() < 64)
            break;
        if (level + 1 < options.levels)
            for (int c = 0; c < s->numChannels; ++c)
                current[static_cast<size_t>(c)] = dsp::decimateByTwo(current[static_cast<size_t>(c)]);
    }

    const int points = (s->frames + overviewBlock - 1) / overviewBlock;
    s->overviewMin.assign(static_cast<size_t>(points), 0.0f);
    s->overviewMax.assign(static_cast<size_t>(points), 0.0f);
    for (int p = 0; p < points; ++p)
    {
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < s->numChannels; ++c)
            for (int i = p * overviewBlock; i < std::min(s->frames, (p + 1) * overviewBlock); ++i)
            {
                const float v = audio.channels[static_cast<size_t>(c)][static_cast<size_t>(i)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        s->overviewMin[static_cast<size_t>(p)] = lo;
        s->overviewMax[static_cast<size_t>(p)] = hi;
    }
    return s;
}

std::shared_ptr<const SampleData> SampleData::load(const std::string& path, std::string* error,
                                                   Options options)
{
    // Known by where it is, when it changed and its size; a file those can't be read of isn't kept.
    std::error_code timeError, sizeError;
    CacheKey key{path, std::filesystem::last_write_time(path, timeError), std::filesystem::file_size(path, sizeError),
                 options.levels, options.compact};
    if (timeError || sizeError)
        key = {};
    auto& c = cache();
    if (!key.path.empty())
    {
        const std::scoped_lock lock(c.mutex);
        if (const auto it = c.entries.find(key); it != c.entries.end())
            if (auto held = it->second.lock())
                return held;
    }
    // Decoded outside the lock, so other files decode alongside; two loads of one file at once
    // both decode, and the first kept is shared from then on.
    const auto decoded = audiofile::decode(path, error);
    if (!decoded)
        return nullptr;
    auto built = build(*decoded, path, options);
    if (key.path.empty())
        return built;
    const std::scoped_lock lock(c.mutex);
    auto& slot = c.entries[key];
    if (auto held = slot.lock())
        return held;
    slot = built;
    // Forget the files no Sampler holds any more.
    std::erase_if(c.entries, [](const auto& e) { return e.second.expired(); });
    return built;
}
} // namespace fd::devices
