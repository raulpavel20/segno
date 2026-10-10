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

bool cutStart(const FadeClip& c) noexcept
{
    return c.offset > kSame;
}

bool cutEnd(const FadeClip& c) noexcept
{
    return c.sourceLength > 0.0 && c.offset + c.length < c.sourceLength - kSame;
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
    return clip.canExtend ? std::max(0.0, clip.offset) : 0.0;
}

double spareAfter(const FadeClip& clip) noexcept
{
    return clip.canExtend && clip.sourceLength > 0.0 ? std::max(0.0, clip.sourceLength - (clip.offset + clip.length)) : 0.0;
}

bool meet(const FadeClip& first, const FadeClip& second) noexcept
{
    return std::abs(first.end() - second.start) < kSame;
}

bool seamless(const FadeClip& prev, const FadeClip& next) noexcept
{
    return meet(prev, next) && !prev.source.empty() && prev.source == next.source &&
           std::abs(prev.offset + prev.length - next.offset) < kSame;
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

std::optional<Crossfade> crossfadeOut(const std::vector<FadeClip>& clips, size_t index)
{
    if (index + 1 >= clips.size())
        return std::nullopt;
    const auto& c = clips[index];
    const auto& next = clips[index + 1];
    if (!c.crossOut || c.muted || next.muted)
        return std::nullopt;
    return placeCrossfade(c, next, c.crossOut->length, c.crossOut->from, c.crossOut->shape);
}

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
    return gain;
}
} // namespace fd::model
