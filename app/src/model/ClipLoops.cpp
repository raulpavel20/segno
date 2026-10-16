#include "model/ClipLoops.h"

#include "model/Editing.h"
#include "model/Snapshot.h"
#include "model/Takes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-9;

/// `x` wrapped into [0, length).
double wrapped(double x, double length) noexcept
{
    double r = std::fmod(x, length);
    if (r < 0.0)
        r += length;
    return r >= length - kEps ? 0.0 : r;
}

/// The longest a loop from `start` can be in `clip`'s content: to an audio file's end; MIDI has no end.
double longestFrom(const ClipInfo& clip, double start) noexcept
{
    return clip.kind == ClipInfo::Kind::Audio && clip.sourceLength > 0.0 ? clip.sourceLength - start
                                                                           : std::numeric_limits<double>::max();
}

/// A loop kept inside the content: from zero, inside an audio file, at least the minimum long.
ClipLoop fitted(const ClipInfo& clip, ClipLoop loop) noexcept
{
    const bool audio = clip.kind == ClipInfo::Kind::Audio && clip.sourceLength > 0.0;
    const double latestStart = audio ? std::max(0.0, clip.sourceLength - minClipLength) : loop.start;
    loop.start = std::clamp(loop.start, 0.0, std::max(0.0, latestStart));
    loop.length = std::clamp(loop.length, minClipLength, std::max(minClipLength, longestFrom(clip, loop.start)));
    return loop;
}
} // namespace

double phaseOf(double offset, const ClipLoop& loop) noexcept
{
    return loop.on() ? wrapped(offset, loop.length) : offset;
}

double contentAt(double clipStart, double offset, const ClipLoop& loop, double seconds) noexcept
{
    const double run = offset + (seconds - clipStart);
    return loop.on() ? loop.start + wrapped(run, loop.length) : run;
}

double contentAt(const ClipInfo& clip, double seconds) noexcept
{
    return contentAt(clip.start, clip.offset, clip.loop, seconds);
}

double contentUpTo(double clipStart, double offset, const ClipLoop& loop, double seconds) noexcept
{
    const double at = contentAt(clipStart, offset, loop, seconds);
    return loop.on() && at - loop.start < 1e-6 ? loop.end() : at;
}

double lapAnchor(const ClipInfo& clip) noexcept
{
    return clip.start - (clip.loop.on() ? phaseOf(clip.offset, clip.loop) : 0.0);
}

std::vector<Lap> laps(const ClipInfo& clip, double from, double to)
{
    const double a = std::max(from, clip.start), b = std::min(to, clip.end());
    if (b <= a)
        return {};
    if (!clip.loop.on())
        return {{a, b, contentAt(clip, a)}};
    const double length = clip.loop.length, anchor = lapAnchor(clip);
    std::vector<Lap> out;
    for (auto k = static_cast<long long>(std::floor((a - anchor) / length + kEps)); anchor + k * length < b - kEps; ++k)
    {
        const double lapStart = anchor + static_cast<double>(k) * length;
        const double lapFrom = std::max(a, lapStart), lapTo = std::min(b, lapStart + length);
        if (lapTo > lapFrom + kEps)
            out.push_back({lapFrom, lapTo, clip.loop.start + (lapFrom - lapStart)});
    }
    return out;
}

int lapCount(const ClipInfo& clip) noexcept
{
    if (!clip.loop.on())
        return 1;
    return std::max(1, static_cast<int>(std::ceil((clip.end() - lapAnchor(clip)) / clip.loop.length - 1e-6)));
}

std::vector<Seam> seams(const ClipInfo& clip)
{
    std::vector<Seam> out;
    if (!clip.loop.on())
        return out;
    const double anchor = lapAnchor(clip);
    for (int k = 1;; ++k)
    {
        const double at = anchor + k * clip.loop.length;
        if (at >= clip.end() - 1e-6)
            break;
        if (at > clip.start + 1e-6)
            out.push_back({at, k});
    }
    return out;
}

std::optional<Seam> seamAt(const ClipInfo& clip, double seconds, double tolerance)
{
    if (!clip.loop.on())
        return std::nullopt;
    const double anchor = lapAnchor(clip);
    const auto k = static_cast<int>(std::lround((seconds - anchor) / clip.loop.length));
    const double at = anchor + k * clip.loop.length;
    if (k < 1 || at <= clip.start + 1e-6 || at >= clip.end() - 1e-6 || std::abs(at - seconds) > tolerance)
        return std::nullopt;
    return Seam{at, k};
}

LoopPlacement placementOf(const ClipInfo& clip) noexcept
{
    return {clip.start, clip.length, clip.offset, clip.loop};
}

LoopPlacement loopOn(const ClipInfo& clip)
{
    if (clip.loop.on())
        return placementOf(clip);
    const double from = clip.offset;
    const double length = std::max(minClipLength, std::min(clip.length, longestFrom(clip, from)));
    return {clip.start, clip.length, 0.0, {from, length}};
}

LoopPlacement loopOff(const ClipInfo& clip)
{
    if (!clip.loop.on())
        return placementOf(clip);
    const double from = contentAt(clip, clip.start);
    const double length = std::clamp(clip.loop.end() - from, minClipLength, clip.length);
    return {clip.start, length, from, {}};
}

LoopPlacement withLoop(const ClipInfo& clip, ClipLoop loop)
{
    loop = fitted(clip, loop);
    const double atStart = contentAt(clip, clip.start);
    const bool inside = atStart >= loop.start - kEps && atStart < loop.end() - kEps;
    return {clip.start, clip.length, inside ? std::max(0.0, atStart - loop.start) : 0.0, loop};
}

LoopPlacement withLoopLength(const ClipInfo& clip, double length)
{
    if (!clip.loop.on())
        return placementOf(clip);
    const auto loop = fitted(clip, {clip.loop.start, length});
    return {clip.start, clip.length, wrapped(phaseOf(clip.offset, clip.loop), loop.length), loop};
}

LoopPlacement trimmedStart(const ClipInfo& clip, double start)
{
    // Not before time zero (nor, unless it loops, before its source's start), and not past its end
    // less the minimum.
    const double earliest = clip.loop.on() ? 0.0 : std::max(0.0, clip.start - clip.offset);
    start = std::clamp(start, earliest, std::max(earliest, clip.end() - minClipLength));
    const double offset = clip.offset + (start - clip.start);
    return {start, clip.end() - start, clip.loop.on() ? wrapped(offset, clip.loop.length) : offset, clip.loop};
}

LoopPlacement trimmedEnd(const ClipInfo& clip, double end)
{
    end = std::max(clip.start + minClipLength, end);
    if (!clip.loop.on() && clip.sourceLength > 0.0)
        end = std::min(end, clip.start - clip.offset + clip.sourceLength); // not past the source
    return {clip.start, end - clip.start, clip.offset, clip.loop};
}

std::string whyCantLoop(const ClipInfo& clip, const TrackInfo& track)
{
    if (clip.stack != 0)
        if (const auto* stack = track.stack(clip.stack); stack != nullptr && hasTakesToShow(*stack))
            return "A clip with takes can't loop: keep only the take you want first (Clip > Takes > Keep "
                   "only this take).";
    if (clip.kind == ClipInfo::Kind::Audio && clip.sourceLength <= 0.0)
        return "This clip's audio file is missing.";
    return {};
}

double clipViewOrigin(const ClipInfo& clip) noexcept
{
    if (!clip.loop.on())
        return clip.start - clip.offset;
    double origin = lapAnchor(clip) - clip.loop.start;
    if (origin < 0.0)
        origin += std::ceil(-origin / clip.loop.length - kEps) * clip.loop.length;
    return origin;
}

double clipViewTime(const ClipInfo& clip, double seconds) noexcept
{
    return clip.loop.on() ? clipViewOrigin(clip) + contentAt(clip, seconds) : seconds;
}

std::optional<double> lengthenedLoop(const ClipInfo& clip, const std::vector<Note>& written, const TempoMap& tempo)
{
    if (!clip.loop.on() || clip.kind != ClipInfo::Kind::Midi || written.empty())
        return std::nullopt;
    double lastBeat = 0.0;
    for (const auto& n : written)
        lastBeat = std::max(lastBeat, n.start + n.length);
    const double lastEnd = tempo.secondsForBeats(lastBeat); // content seconds
    if (lastEnd <= clip.loop.end() + 1e-6)
        return std::nullopt;
    // The end of the song bar it ends in, as the clip view shows the content.
    const double origin = clipViewOrigin(clip), bar = tempo.secondsForBeats(tempo.beatsPerBar());
    const double end = std::ceil((origin + lastEnd) / bar - 1e-9) * bar - origin;
    return end - clip.loop.start;
}
} // namespace fd::model
