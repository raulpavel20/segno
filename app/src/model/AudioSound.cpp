#include "model/AudioSound.h"

#include "model/Snapshot.h"
#include "model/Taper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fd/audiofile/AudioFile.h>
#include <fd/dsp/Loudness.h>

namespace fd::model
{
namespace
{
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

float gainForDb(double db) noexcept
{
    return db <= minusInfinityDb ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
}

/// The line's value at `seconds`, 0 dB without points.
float valueAt(const AutomationPoints& line, double seconds)
{
    return line.empty() ? lineValueForDb(0.0) : automationValueAt(line, seconds);
}

/// The line over `span` rewritten by `inside` (a value inside the span from the value there), the
/// level at the span's edges kept and joined to the inside by a ramp at each edge.
template <typename F>
AutomationPoints rewrite(const AutomationPoints& line, LineSpan span, double ramp, F inside)
{
    if (!(span.to > span.from))
        return line;
    const double r = std::min(ramp, (span.to - span.from) * 0.5);
    const double a = span.from + r, b = span.to - r;
    AutomationPoints out;
    for (const auto& p : line)
        if (p.time < span.from)
            out.push_back(p);
    out.push_back({span.from, valueAt(line, span.from), 0.0f});
    out.push_back({a, inside(valueAt(line, a)), 0.0f});
    for (const auto& p : line)
        if (p.time > a && p.time < b)
            out.push_back({p.time, inside(p.value), p.curve});
    if (b > a)
        out.push_back({b, inside(valueAt(line, b)), 0.0f});
    out.push_back({span.to, valueAt(line, span.to), 0.0f});
    for (const auto& p : line)
        if (p.time > span.to)
            out.push_back(p);
    return out;
}
} // namespace

std::string channelModeName(ChannelMode mode)
{
    switch (mode)
    {
    case ChannelMode::Stereo:
        return "Stereo";
    case ChannelMode::Left:
        return "Left";
    case ChannelMode::Right:
        return "Right";
    case ChannelMode::Mono:
        return "Mono";
    case ChannelMode::Swapped:
        return "Swapped";
    }
    return "Stereo";
}

std::optional<ChannelMode> channelModeFromName(const std::string& name)
{
    const auto n = lower(name);
    for (auto mode : {ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Right, ChannelMode::Mono,
                      ChannelMode::Swapped})
        if (n == lower(channelModeName(mode)))
            return mode;
    return std::nullopt;
}

float lineValueForDb(double db) noexcept
{
    return static_cast<float>(volumePosition(db));
}

double lineDbForValue(float value) noexcept
{
    return volumeDbAt(value);
}

bool AudioSound::neutral() const noexcept
{
    const float zero = lineValueForDb(0.0);
    const bool flat = std::all_of(line.begin(), line.end(), [zero](const AutomationPoint& p)
                                  { return std::abs(p.value - zero) < 1e-6f; });
    return gainDb == 0.0f && flat && !inverted && channels == ChannelMode::Stereo;
}

float lineGainAt(const AutomationPoints& line, double fileSeconds) noexcept
{
    if (line.empty())
        return 1.0f;
    const float v = automationValueAt(line, fileSeconds);
    return v <= 0.0f ? 0.0f : gainForDb(lineDbForValue(v));
}

float soundGainAt(const AudioSound& sound, double fileSeconds) noexcept
{
    return gainForDb(sound.gainDb) * lineGainAt(sound.line, fileSeconds);
}

double fileTimeOf(double content, double sourceLength, bool reversed) noexcept
{
    return reversed ? sourceLength - content : content;
}

namespace
{
/// Points at `flip` − their time, in time order: a segment read the other way starts at its
/// other end and bends the other way.
AutomationPoints mirrored(const AutomationPoints& line, double flip)
{
    AutomationPoints out;
    const auto n = line.size();
    for (size_t k = 0; k < n; ++k)
    {
        const auto& p = line[n - 1 - k];
        out.push_back({flip - p.time, p.value, k + 1 < n ? -line[n - 2 - k].curve : 0.0f});
    }
    return out;
}
} // namespace

AutomationPoints lineToView(const AutomationPoints& line, double origin, double sourceLength, bool reversed)
{
    if (reversed)
        return mirrored(line, origin + sourceLength);
    auto out = line;
    for (auto& p : out)
        p.time += origin;
    return out;
}

AutomationPoints lineFromView(const AutomationPoints& view, double origin, double sourceLength, bool reversed)
{
    if (reversed)
        return mirrored(view, origin + sourceLength);
    auto out = view;
    for (auto& p : out)
        p.time -= origin;
    return out;
}

void applyChannels(ChannelMode mode, float& left, float& right) noexcept
{
    switch (mode)
    {
    case ChannelMode::Stereo:
        return;
    case ChannelMode::Left:
        right = left;
        return;
    case ChannelMode::Right:
        left = right;
        return;
    case ChannelMode::Mono:
        left = right = 0.5f * (left + right);
        return;
    case ChannelMode::Swapped:
        std::swap(left, right);
        return;
    }
}

std::vector<LineSpan> fileSpans(const ClipInfo& clip, bool reversed, double from, double to)
{
    std::vector<LineSpan> out;
    for (const auto& lap : laps(clip, from, to))
    {
        const double c0 = lap.content, c1 = lap.content + (lap.to - lap.from);
        LineSpan span =
            reversed ? LineSpan{clip.sourceLength - c1, clip.sourceLength - c0} : LineSpan{c0, c1};
        if (span.to > span.from &&
            std::none_of(out.begin(), out.end(), [&](const LineSpan& s)
                         { return std::abs(s.from - span.from) < 1e-9 && std::abs(s.to - span.to) < 1e-9; }))
            out.push_back(span);
    }
    return out;
}

AutomationPoints lineWithGain(const AutomationPoints& line, LineSpan span, double deltaDb, double ramp)
{
    return rewrite(
        line, span, ramp, [deltaDb](float v)
        { return v <= 0.0f ? 0.0f : std::min(1.0f, lineValueForDb(lineDbForValue(v) + deltaDb)); });
}

AutomationPoints lineSilenced(const AutomationPoints& line, LineSpan span, double ramp)
{
    return rewrite(line, span, ramp, [](float) { return 0.0f; });
}

std::optional<ClipLevels> measureClip(const std::string& path, double contentFrom, double contentTo,
                                      const AudioSound& sound, std::string* error)
{
    dsp::LoudnessMeter meter;
    std::vector<std::vector<float>> scratch;
    std::vector<const float*> pointers;
    double peak = 0.0;
    std::int64_t frame = 0;
    bool prepared = false;
    double from = 0.0, to = 0.0; // in the file as recorded
    const auto info = audiofile::stream(
        path, 4096,
        [&](const audiofile::StreamInfo& format, const float* interleaved, int frames)
        {
            const int channels = std::max(1, format.numChannels);
            if (!prepared)
            {
                const double length = static_cast<double>(format.numFrames) / format.sampleRate;
                from = fileTimeOf(sound.reversed ? contentTo : contentFrom, length, sound.reversed);
                to = fileTimeOf(sound.reversed ? contentFrom : contentTo, length, sound.reversed);
                meter.prepare(format.sampleRate, std::min(channels, dsp::LoudnessMeter::maxChannels));
                scratch.assign(static_cast<size_t>(std::min(channels, 2)), std::vector<float>(4096));
                prepared = true;
            }
            int used = 0;
            for (int i = 0; i < frames; ++i, ++frame)
            {
                const double t = static_cast<double>(frame) / format.sampleRate;
                if (t < from)
                    continue;
                if (t >= to)
                    break;
                const float g = lineGainAt(sound.line, t);
                float l = interleaved[i * channels] * g;
                float r = channels > 1 ? interleaved[i * channels + 1] * g : l;
                if (channels > 1)
                    applyChannels(sound.channels, l, r);
                peak = std::max({peak, static_cast<double>(std::abs(l)), static_cast<double>(std::abs(r))});
                scratch[0][static_cast<size_t>(used)] = l;
                if (scratch.size() > 1)
                    scratch[1][static_cast<size_t>(used)] = r;
                ++used;
            }
            if (used > 0)
            {
                pointers.clear();
                for (auto& c : scratch)
                    pointers.push_back(c.data());
                meter.process(pointers.data(), used);
            }
            return static_cast<double>(frame) / format.sampleRate < to;
        },
        error);
    if (!info)
        return std::nullopt;
    ClipLevels levels;
    if (peak > 0.0)
        levels.peakDb = std::max(ClipLevels::silent, 20.0 * std::log10(peak));
    const float lufs = meter.integrated();
    if (std::isfinite(lufs) && lufs > -70.0f)
        levels.lufs = lufs;
    return levels;
}

std::vector<std::optional<float>> normaliseGains(const std::vector<ClipLevels>& levels,
                                                 const std::vector<float>& currentGainDb, NormaliseTo to,
                                                 double targetDb, bool together)
{
    const auto level = [to](const ClipLevels& l) { return to == NormaliseTo::Peak ? l.peakDb : l.lufs; };
    const auto heard = [](double db) { return db > -70.0; };
    const auto clamp = [](double db)
    {
        return static_cast<float>(
            std::clamp(db, static_cast<double>(minusInfinityDb), static_cast<double>(kMaxClipGainDb)));
    };
    std::vector<std::optional<float>> out(levels.size());
    if (together)
    {
        std::optional<double> loudest; // as they play now, their gains included
        for (size_t i = 0; i < levels.size(); ++i)
            if (const double l = level(levels[i]); heard(l))
                loudest = std::max(loudest.value_or(l + currentGainDb[i]), l + currentGainDb[i]);
        if (!loudest)
            return out;
        const double delta = targetDb - *loudest;
        for (size_t i = 0; i < levels.size(); ++i)
            if (heard(level(levels[i])))
                out[i] = clamp(currentGainDb[i] + delta);
        return out;
    }
    for (size_t i = 0; i < levels.size(); ++i)
        if (const double l = level(levels[i]); heard(l))
            out[i] = clamp(targetDb - l);
    return out;
}
} // namespace fd::model
