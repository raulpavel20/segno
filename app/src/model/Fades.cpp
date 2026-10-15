#include "model/Fades.h"

#include "model/Automation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>

namespace fd::model
{
namespace
{
constexpr double kSame = 1e-6; // seconds: two edges this close are one place

double clamp01(double v) noexcept
{
    return std::clamp(v, 0.0, 1.0);
}

/// What a clip plays at its start, and up to at its end (a looped clip's end on a seam: the loop's
/// end): seconds into its file (ADR 0048).
double contentAtStart(const FadeClip& c) noexcept
{
    return contentAt(c.start, c.offset, c.loop, c.start);
}

double contentAtEnd(const FadeClip& c) noexcept
{
    return contentUpTo(c.start, c.offset, c.loop, c.end());
}

bool cutStart(const FadeClip& c) noexcept
{
    return contentAtStart(c) > kSame;
}

bool cutEnd(const FadeClip& c) noexcept
{
    return c.sourceLength > 0.0 && contentAtEnd(c) < c.sourceLength - kSame;
}

/// A fade's ramp, with the edge fade's floor on a cut edge (§2).
Ramp fadeRamp(const Fade& fade, bool edge) noexcept
{
    if (edge && fade.length < kEdgeFadeSeconds)
        return {.kind = Ramp::Kind::Fade, .length = kEdgeFadeSeconds, .edge = true};
    if (fade.length < kShortestFade)
        return {};
    return {.kind = Ramp::Kind::Fade, .length = fade.length, .bend = fade.bend};
}
} // namespace

std::string shapeName(CrossfadeShape shape)
{
    switch (shape)
    {
    case CrossfadeShape::EqualPower: return "Equal power";
    case CrossfadeShape::EqualGain: return "Equal gain";
    case CrossfadeShape::SCurve: return "S-curve";
    }
    return {};
}

std::optional<CrossfadeShape> shapeFromName(const std::string& name)
{
    std::string key;
    for (const char c : name)
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (key == "equalpower")
        return CrossfadeShape::EqualPower;
    if (key == "equalgain")
        return CrossfadeShape::EqualGain;
    if (key == "scurve")
        return CrossfadeShape::SCurve;
    return std::nullopt;
}

float fadeInGain(double alpha, float bend) noexcept
{
    return std::clamp(segmentValueAt(0.0, 0.0f, 1.0, 1.0f, curveForBend(bend), clamp01(alpha)), 0.0f, 1.0f);
}

float fadeOutGain(double alpha, float bend) noexcept
{
    return std::clamp(segmentValueAt(0.0, 1.0f, 1.0, 0.0f, curveForBend(bend), clamp01(alpha)), 0.0f, 1.0f);
}

CrossGains crossfadeGains(double alpha, CrossfadeShape shape) noexcept
{
    const double a = clamp01(alpha);
    switch (shape)
    {
    case CrossfadeShape::EqualPower:
        return {static_cast<float>(std::cos(a * std::numbers::pi * 0.5)), static_cast<float>(std::sin(a * std::numbers::pi * 0.5))};
    case CrossfadeShape::EqualGain: return {static_cast<float>(1.0 - a), static_cast<float>(a)};
    case CrossfadeShape::SCurve:
    {
        const double in = 0.5 - 0.5 * std::cos(a * std::numbers::pi);
        return {static_cast<float>(1.0 - in), static_cast<float>(in)};
    }
    }
    return {};
}

double spareBefore(const FadeClip& clip) noexcept
{
    if (!clip.canExtend)
        return 0.0;
    return clip.loop.on() ? clip.loop.length : std::max(0.0, clip.offset);
}

double spareAfter(const FadeClip& clip) noexcept
{
    if (!clip.canExtend)
        return 0.0;
    if (clip.loop.on())
        return clip.loop.length;
    return clip.sourceLength > 0.0 ? std::max(0.0, clip.sourceLength - (clip.offset + clip.length)) : 0.0;
}

bool meet(const FadeClip& first, const FadeClip& second) noexcept
{
    return std::abs(first.end() - second.start) < kSame;
}

bool seamless(const FadeClip& prev, const FadeClip& next) noexcept
{
    return meet(prev, next) && !prev.source.empty() && prev.source == next.source &&
           prev.reversed == next.reversed && prev.loop == next.loop && prev.map == next.map &&
           std::abs(contentAtEnd(prev) - contentAtStart(next)) < kSame;
}

std::optional<Crossfade> placeCrossfade(const FadeClip& out, const FadeClip& in, double length, std::optional<double> from,
                                        CrossfadeShape shape)
{
    if (!meet(out, in))
        return std::nullopt;
    const double before = spareBefore(in), after = spareAfter(out);
    // As long as the spare audio and the clips allow: the outgoing clip plays `from + L` past the
    // join, the incoming one `-from` before it, and neither ramp runs past its clip's far end.
    const double l = std::min({length, before + after, out.length + after, in.length + before, out.length + in.length});
    if (l < kShortestFade)
        return std::nullopt;
    const double lo = std::max({-l, -before, -out.length});
    const double hi = std::min({0.0, after - l, in.length - l});
    // Keep its centre where it was asked for (centred on the join when not given).
    const double centre = from ? *from + length * 0.5 : 0.0;
    return Crossfade{.length = l, .from = std::clamp(centre - l * 0.5, lo, hi), .shape = shape};
}

std::optional<CrossfadePlacement> placementOf(const Crossfade& crossfade) noexcept
{
    for (const auto p : {CrossfadePlacement::Before, CrossfadePlacement::Centred, CrossfadePlacement::After})
        if (std::abs(crossfade.from - placementFrom(p, crossfade.length)) < kSame)
            return p;
    return std::nullopt;
}

double placementFrom(CrossfadePlacement placement, double length) noexcept
{
    switch (placement)
    {
    case CrossfadePlacement::Before: return -length;
    case CrossfadePlacement::Centred: return -length * 0.5;
    case CrossfadePlacement::After: return 0.0;
    }
    return -length * 0.5;
}

std::optional<Crossfade> placeCrossfadeAt(const FadeClip& out, const FadeClip& in, double length, CrossfadePlacement placement,
                                          CrossfadeShape shape)
{
    const double from = placementFrom(placement, length);
    const auto x = placeCrossfade(out, in, length, from, shape);
    if (!x || std::abs(x->length - length) > kSame || std::abs(x->from - from) > kSame)
        return std::nullopt;
    return x;
}

std::vector<float> crossfadeSum(const std::vector<float>& outgoing, const std::vector<float>& incoming, int channels,
                                CrossfadeShape shape)
{
    const auto frames = channels > 0 ? std::min(outgoing.size(), incoming.size()) / static_cast<size_t>(channels) : 0;
    std::vector<float> out(frames * static_cast<size_t>(std::max(0, channels)));
    for (size_t f = 0; f < frames; ++f)
    {
        const double alpha = frames > 1 ? static_cast<double>(f) / static_cast<double>(frames - 1) : 0.0;
        const auto g = crossfadeGains(alpha, shape);
        for (int c = 0; c < channels; ++c)
        {
            const auto i = f * static_cast<size_t>(channels) + static_cast<size_t>(c);
            out[i] = outgoing[i] * g.out + incoming[i] * g.in;
        }
    }
    return out;
}

namespace
{
/// The crossfade stored at the end of `clips[index]`, fitted to the spare audio (before sharing).
std::optional<Crossfade> fitted(const std::vector<FadeClip>& clips, size_t index)
{
    if (index + 1 >= clips.size())
        return std::nullopt;
    const auto& c = clips[index];
    const auto& next = clips[index + 1];
    if (!c.crossOut || c.muted || next.muted)
        return std::nullopt;
    return placeCrossfade(c, next, c.crossOut->length, c.crossOut->from, c.crossOut->shape);
}

/// How far a crossfade reaches into the clip before its join, and into the clip after it.
double reachBefore(const Crossfade& x) noexcept
{
    return std::max(0.0, -x.from);
}
double reachAfter(const Crossfade& x) noexcept
{
    return std::max(0.0, x.from + x.length);
}

/// The share of their reaches two crossfades keep in a clip `length` long: all of them, or as
/// much as fits, in proportion.
double share(double length, double in, double out) noexcept
{
    return in + out > length && in + out > 0.0 ? std::max(0.0, length) / (in + out) : 1.0;
}
} // namespace

std::optional<Crossfade> crossfadeOut(const std::vector<FadeClip>& clips, size_t index)
{
    const auto x = fitted(clips, index);
    if (!x)
        return std::nullopt;
    // A clip's two crossfades share it: where their reaches into it overlap, each keeps its share.
    const auto prev = index > 0 ? fitted(clips, index - 1) : std::nullopt;
    const auto next = fitted(clips, index + 1);
    const double before = reachBefore(*x), after = reachAfter(*x);
    const double b = before * share(clips[index].length, prev ? reachAfter(*prev) : 0.0, before);
    const double a = after * share(clips[index + 1].length, after, next ? reachBefore(*next) : 0.0);
    if (a + b < kShortestFade)
        return std::nullopt;
    return Crossfade{.length = a + b, .from = -b, .shape = x->shape};
}

std::optional<Crossfade> placeCrossfadeOn(const std::vector<FadeClip>& clips, size_t index, double length,
                                          std::optional<double> from, CrossfadeShape shape)
{
    if (index + 1 >= clips.size())
        return std::nullopt;
    const auto x = placeCrossfade(clips[index], clips[index + 1], length, from, shape);
    if (!x)
        return std::nullopt;
    // Up to the crossfades at the two clips' other ends, as they play without this one.
    auto without = clips;
    without[index].crossOut.reset();
    const auto prev = index > 0 ? crossfadeOut(without, index - 1) : std::nullopt;
    const auto next = crossfadeOut(without, index + 1);
    const double b = std::min(reachBefore(*x), std::max(0.0, clips[index].length - (prev ? reachAfter(*prev) : 0.0)));
    const double a = std::min(reachAfter(*x), std::max(0.0, clips[index + 1].length - (next ? reachBefore(*next) : 0.0)));
    if (a + b < kShortestFade)
        return std::nullopt;
    return Crossfade{.length = a + b, .from = -b, .shape = shape};
}

double crossfadeReachBefore(const Crossfade& crossfade) noexcept
{
    return reachBefore(crossfade);
}

double crossfadeReachAfter(const Crossfade& crossfade) noexcept
{
    return reachAfter(crossfade);
}

namespace
{
/// A looped clip's seam fades over what it plays, [from, to): every seam whose fades reach into it,
/// so a clip split beside a seam still has the seam's whole fade, except a seam right on `from` or
/// `to`, where the clip's own edge fades it (a split exactly on a seam) or nothing may.
SeamRamps seamRamps(const FadeClip& c, double from, double to) noexcept
{
    SeamRamps s{.period = c.loop.length};
    if (c.sourceLength > 0.0 && c.loop.end() < c.sourceLength - kSame)
        s.out = kEdgeFadeSeconds;
    if (c.loop.start > kSame)
        s.in = kEdgeFadeSeconds;
    if (!s.any())
        return {};
    const double anchor = c.start - phaseOf(c.offset, c.loop);
    const auto onEdge = [&](double seam) { return std::abs(seam - from) <= kSame || std::abs(seam - to) <= kSame; };
    // The first seam past `from` less its fade-in, and the last before `to` plus its fade-out.
    double first = anchor + std::floor((from - s.in - anchor) / s.period) * s.period;
    while (first <= from - s.in || onEdge(first))
        first += s.period;
    double last = anchor + std::ceil((to + s.out - anchor) / s.period) * s.period;
    while (last >= to + s.out || onEdge(last))
        last -= s.period;
    if (last < first - kSame)
        return {};
    s.first = first;
    s.until = last + kSame;
    return s;
}
} // namespace

ClipPlayback clipPlayback(const std::vector<FadeClip>& clips, size_t index, bool edgeFades)
{
    ClipPlayback p;
    if (index >= clips.size() || clips[index].muted)
        return p;
    const auto& c = clips[index];
    const auto* prev = index > 0 && !clips[index - 1].muted && meet(clips[index - 1], c) ? &clips[index - 1] : nullptr;
    const auto* next = index + 1 < clips.size() && !clips[index + 1].muted && meet(c, clips[index + 1]) ? &clips[index + 1] : nullptr;

    if (const auto x = index > 0 ? crossfadeOut(clips, index - 1) : std::nullopt; x && prev != nullptr)
    {
        p.extendStart = -x->from;
        p.in = {.kind = Ramp::Kind::Cross, .length = x->length, .shape = x->shape};
    }
    else
        p.in = fadeRamp(c.in, edgeFades && cutStart(c) && !(prev != nullptr && seamless(*prev, c)));

    if (const auto x = crossfadeOut(clips, index); x && next != nullptr)
    {
        p.extendEnd = x->from + x->length;
        p.out = {.kind = Ramp::Kind::Cross, .length = x->length, .shape = x->shape};
    }
    else
        p.out = fadeRamp(c.out, edgeFades && cutEnd(c) && !(next != nullptr && seamless(c, *next)));

    // A looped clip's seams fade where they cut into the file (ADR 0048), as cut edges do.
    if (edgeFades && c.loop.on())
        p.seams = seamRamps(c, c.start - p.extendStart, c.end() + p.extendEnd);

    // Fades never overlap: on a clip shorter than both, they share it in proportion.
    const double played = c.length + p.extendStart + p.extendEnd;
    double fades = 0.0;
    for (const auto* r : {&p.in, &p.out})
        if (r->kind == Ramp::Kind::Fade)
            fades += r->length;
    double crosses = 0.0;
    for (const auto* r : {&p.in, &p.out})
        if (r->kind == Ramp::Kind::Cross)
            crosses += r->length;
    if (fades > 0.0 && fades + crosses > played)
    {
        const double k = std::max(0.0, played - crosses) / fades;
        for (auto* r : {&p.in, &p.out})
            if (r->kind == Ramp::Kind::Fade)
                r->length *= k;
    }
    return p;
}

float rampGain(const Ramp& ramp, double alpha, bool fadingIn) noexcept
{
    switch (ramp.kind)
    {
    case Ramp::Kind::None: return 1.0f;
    case Ramp::Kind::Fade: return fadingIn ? fadeInGain(alpha, ramp.bend) : fadeOutGain(alpha, ramp.bend);
    case Ramp::Kind::Cross:
    {
        const auto g = crossfadeGains(alpha, ramp.shape);
        return fadingIn ? g.in : g.out;
    }
    }
    return 1.0f;
}

float seamGain(const SeamRamps& seams, double seconds) noexcept
{
    if (!seams.any() || seconds < seams.first - seams.out)
        return 1.0f;
    // The seam at or before `seconds`, and the next one.
    const double k = std::floor((seconds - seams.first) / seams.period);
    const double before = seams.first + k * seams.period, after = before + seams.period;
    float gain = 1.0f;
    if (k >= 0.0 && before < seams.until && seams.in > 0.0 && seconds < before + seams.in)
        gain *= fadeInGain((seconds - before) / seams.in, 0.0f);
    if (k + 1.0 >= 0.0 && after < seams.until && seams.out > 0.0 && seconds >= after - seams.out)
        gain *= fadeOutGain((seconds - (after - seams.out)) / seams.out, 0.0f);
    return gain;
}

bool seamRampBetween(const SeamRamps& seams, double from, double to) noexcept
{
    if (!seams.any())
        return false;
    // The first seam whose fades reach past `from`: it must start its fade-out before `to`.
    const double k = std::max(0.0, std::ceil((from - seams.in - seams.first) / seams.period));
    const double at = seams.first + k * seams.period;
    return at < seams.until && at - seams.out < to;
}

float playbackGain(const FadeClip& clip, const ClipPlayback& playback, double seconds) noexcept
{
    const double start = clip.start - playback.extendStart, end = clip.end() + playback.extendEnd;
    if (seconds < start || seconds >= end)
        return 0.0f;
    float gain = 1.0f;
    if (playback.in.kind != Ramp::Kind::None && seconds < start + playback.in.length)
        gain *= rampGain(playback.in, (seconds - start) / playback.in.length, true);
    if (playback.out.kind != Ramp::Kind::None && seconds > end - playback.out.length)
        gain *= rampGain(playback.out, (seconds - (end - playback.out.length)) / playback.out.length, false);
    return gain * seamGain(playback.seams, seconds);
}
} // namespace fd::model
