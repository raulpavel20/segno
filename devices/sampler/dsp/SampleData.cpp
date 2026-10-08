#include "SampleData.h"

#include <algorithm>

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
} // namespace

std::shared_ptr<const SampleData> SampleData::build(const audiofile::DecodedAudio& audio, std::string path)
{
    auto s = std::make_shared<SampleData>();
    s->path = std::move(path);
    s->sampleRate = audio.sampleRate;
    s->numChannels = std::min(2, audio.numChannels());
    s->frames = audio.numFrames();
    s->rootKey = audio.rootKey;

    std::array<std::vector<float>, 2> current;
    for (int c = 0; c < s->numChannels; ++c)
        current[static_cast<size_t>(c)] = audio.channels[static_cast<size_t>(c)];
    for (int level = 0; level < maxLevels; ++level)
    {
        Level l;
        l.frames = static_cast<int>(current[0].size());
        for (int c = 0; c < s->numChannels; ++c)
            l.channels[static_cast<size_t>(c)] = padded(current[static_cast<size_t>(c)]);
        s->levels.push_back(std::move(l));
        if (current[0].size() < 64)
            break;
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

std::shared_ptr<const SampleData> SampleData::load(const std::string& path, std::string* error)
{
    const auto decoded = audiofile::decode(path, error);
    if (!decoded)
        return nullptr;
    return build(*decoded, path);
}
} // namespace fd::devices
