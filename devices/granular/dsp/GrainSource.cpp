#include "GrainSource.h"

#include <algorithm>
#include <cmath>

namespace fd::devices
{
audiofile::DecodedAudio GrainSource::chord(double seconds, double sampleRate)
{
    audiofile::DecodedAudio audio;
    audio.sampleRate = sampleRate;
    audio.rootKey = 45; // A2
    const auto frames = static_cast<size_t>(seconds * sampleRate);
    audio.channels.assign(2, std::vector<float>(frames, 0.0f));
    // A2, E3, A3, C#4, E4: each a few harmonics, the two sides a little apart in tuning, so the
    // chord beats slowly and moves across the field.
    constexpr double notes[] = {110.0, 164.81, 220.0, 277.18, 329.63};
    constexpr double twoPi = 6.283185307179586;
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double swell = 0.5 - 0.5 * std::cos(twoPi * t / seconds); // up and down over its length
        for (int c = 0; c < 2; ++c)
        {
            double x = 0.0;
            for (const double hz : notes)
                for (int h = 1; h <= 4; ++h)
                    x += std::sin(twoPi * hz * h * (1.0 + (c == 0 ? -0.0007 : 0.0007)) * t) / (h * h);
            audio.channels[static_cast<size_t>(c)][i] = static_cast<float>(
                0.08 * x * (0.25 + 0.75 * swell) * (1.0 + 0.3 * std::sin(twoPi * 0.4 * t + c)));
        }
    }
    return audio;
}

std::shared_ptr<const GrainSource> GrainSource::build(const audiofile::DecodedAudio& audio, std::string name,
                                                      std::string path)
{
    auto s = std::make_shared<GrainSource>();
    s->name = std::move(name);
    s->path = std::move(path);
    s->sampleRate = audio.sampleRate;
    s->numChannels = std::min(2, audio.numChannels());
    s->frames = audio.numFrames();
    if (audio.rootKey)
    {
        s->root = static_cast<double>(*audio.rootKey);
        s->rootFound = true;
    }
    // Brought to full scale: the overview drawn as the grains will play it.
    float peak = 0.0f;
    for (int c = 0; c < s->numChannels; ++c)
        for (const float x : audio.channels[static_cast<size_t>(c)])
            peak = std::max(peak, std::abs(x));
    const float gain = peak > 1e-6f ? 1.0f / peak : 1.0f;
    const int points = (s->frames + overviewBlock - 1) / overviewBlock;
    for (int c = 0; c < s->numChannels; ++c)
    {
        const auto& in = audio.channels[static_cast<size_t>(c)];
        auto& lo = s->overviewMin[static_cast<size_t>(c)];
        auto& hi = s->overviewMax[static_cast<size_t>(c)];
        lo.assign(static_cast<size_t>(points), 0.0f);
        hi.assign(static_cast<size_t>(points), 0.0f);
        for (int p = 0; p < points; ++p)
        {
            const auto from = static_cast<size_t>(p * overviewBlock);
            const auto to = std::min(in.size(), from + overviewBlock);
            const auto [mn, mx] =
                std::minmax_element(in.begin() + static_cast<long>(from), in.begin() + static_cast<long>(to));
            lo[static_cast<size_t>(p)] = *mn * gain;
            hi[static_cast<size_t>(p)] = *mx * gain;
        }
    }
    return s;
}
} // namespace fd::devices
