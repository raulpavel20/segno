#include "model/AudioAnalysis.h"

#include "model/Snapshot.h"

#include <algorithm>
#include <cmath>
#include <fd/audiofile/AudioFile.h>
#include <fd/music/Detect.h>

namespace fd::model
{
namespace
{
/// Transients are found 30 s at a time, each stretch read with half a second either side so
/// nothing at its edges is missed or found twice.
constexpr double kSegmentSeconds = 30.0, kPadSeconds = 0.5;
/// What the tempo and the mode are found from.
constexpr double kTempoSeconds = 60.0, kPercussiveSeconds = 20.0, kKeySeconds = 120.0;
} // namespace

std::optional<FileAnalysis> analyseFile(const std::string& path, std::string* error,
                                        const std::atomic<bool>* stop)
{
    FileAnalysis out;
    std::vector<float> buffer, head;
    std::int64_t bufferStart = 0, frames = 0;
    double rate = 0.0;
    bool first = true;
    dsp::PitchClassCounter pitches;
    // Finds the transients in the buffer, keeping those in [keepFrom, keepTo) of it; and counts
    // the pitches in that part, while it's within the first two minutes.
    const auto find = [&](size_t length, double keepFrom, double keepTo)
    {
        const auto found = dsp::onsetCandidates(std::span<const float>(buffer.data(), length), rate);
        for (const auto& c : found)
            if (c.time >= keepFrom && c.time < keepTo)
                out.onsets.push_back({c.time + static_cast<double>(bufferStart) / rate, c.strength});
        const double start = static_cast<double>(bufferStart) / rate;
        if (start + keepFrom < kKeySeconds)
        {
            const auto from = std::min(length, static_cast<size_t>(keepFrom * rate));
            const auto to = std::min({length, static_cast<size_t>(keepTo * rate),
                                      static_cast<size_t>((kKeySeconds - start) * rate)});
            if (to > from)
                pitches.add(std::span<const float>(buffer.data() + from, to - from), rate);
        }
    };
    const auto info = audiofile::stream(
        path, 65536,
        [&](const audiofile::StreamInfo& format, const float* interleaved, int count)
        {
            rate = format.sampleRate;
            const int channels = std::max(1, format.numChannels);
            for (int i = 0; i < count; ++i)
            {
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c)
                    sum += interleaved[static_cast<size_t>(i * channels + c)];
                const float mono = sum / static_cast<float>(channels);
                buffer.push_back(mono);
                if (static_cast<double>(frames) < kTempoSeconds * rate)
                    head.push_back(mono);
                ++frames;
            }
            const auto segment = static_cast<size_t>(kSegmentSeconds * rate),
                       pad = static_cast<size_t>(kPadSeconds * rate);
            while (buffer.size() >= segment + 2 * pad)
            {
                find(segment + 2 * pad, first ? 0.0 : kPadSeconds,
                     (first ? 0.0 : kPadSeconds) + kSegmentSeconds);
                // The next stretch starts where this one's kept transients end, less its pad.
                const auto dropped = segment - (first ? pad : 0);
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(dropped));
                bufferStart += static_cast<std::int64_t>(dropped);
                first = false;
            }
            return stop == nullptr || !stop->load();
        },
        error);
    if (!info || !(rate > 0.0) || (stop != nullptr && stop->load()))
        return std::nullopt;
    if (!buffer.empty())
        find(buffer.size(), first ? 0.0 : kPadSeconds, static_cast<double>(buffer.size()) / rate + 1.0);
    std::sort(out.onsets.begin(), out.onsets.end(),
              [](const dsp::OnsetCandidate& a, const dsp::OnsetCandidate& b) { return a.time < b.time; });
    out.length = static_cast<double>(frames) / rate;
    const auto percussive = std::min(head.size(), static_cast<size_t>(kPercussiveSeconds * rate));
    out.percussiveness = dsp::percussiveness(std::span<const float>(head.data(), percussive), rate);
    out.tempo = dsp::estimateTempo(head, rate);
    out.pitchClasses = pitches.result();
    out.key = keyOfSound(out.pitchClasses);
    return out;
}

std::optional<music::Key> keyOfSound(const dsp::PitchClasses& sound)
{
    if (sound.tonal < kKeyTonal)
        return std::nullopt;
    const auto clear = music::clearKey(sound.weights);
    return clear ? std::optional(clear->key) : std::nullopt;
}

std::optional<music::Key> fileKey(std::optional<int> metadataRoot, const std::optional<music::Key>& named,
                                  const dsp::PitchClasses* sound)
{
    if (metadataRoot)
    {
        const int root = ((*metadataRoot % 12) + 12) % 12;
        if (named && named->root == root)
            return named;
        // Major or minor at that root: whichever the sound fits better.
        if (sound != nullptr && sound->tonal >= kKeyTonal)
            for (const auto& c : music::keysFromProfile(sound->weights, 24))
                if (c.key.root == root)
                    return c.key;
        return music::Key{root, "major"};
    }
    if (named)
        return named;
    return sound != nullptr ? keyOfSound(*sound) : std::nullopt;
}

std::vector<double> clipTransients(const FileAnalysis& file, double sensitivity,
                                   const std::vector<double>& added, const std::vector<double>& removed)
{
    auto out = dsp::onsetsAt(file.onsets, sensitivity);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](double t)
                             {
                                 return std::any_of(removed.begin(), removed.end(),
                                                    [t](double r) { return std::abs(r - t) < 0.001; });
                             }),
              out.end());
    for (double t : added)
        if (std::none_of(out.begin(), out.end(), [t](double o) { return std::abs(o - t) < 0.001; }))
            out.push_back(t);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<double> transientsOf(const ClipInfo& clip)
{
    if (clip.analysis == nullptr)
        return {};
    return clipTransients(*clip.analysis, clip.warp.sensitivity, clip.transientsAdded,
                          clip.transientsRemoved);
}

bool startsInBeats(const FileAnalysis& file) noexcept
{
    return file.percussiveness > 0.5;
}

bool isLoopAt(double fileSeconds, double tempo) noexcept
{
    if (!(tempo > 0.0) || !(fileSeconds > 0.0))
        return false;
    const double bars = fileSeconds * tempo / 240.0;
    for (const double n : {1.0, 2.0, 4.0, 8.0, 16.0})
        if (std::abs(bars - n) / n < 0.01)
            return true;
    return false;
}
} // namespace fd::model
