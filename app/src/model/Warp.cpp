#include "model/Warp.h"

#include "model/Snapshot.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <tuple>

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

/// Moments this close in the file are the same moment (a transient on a marker).
constexpr double kSameMoment = 0.001;
/// Slack on the stretch limits, so a piece exactly at one counts as within it.
constexpr double kLimitSlack = 1e-9;

/// Beats a second of the file at the clip's tempo.
double beatsPerFileSecond(const AudioWarp& w) noexcept
{
    return std::clamp(w.tempo, kMinClipTempo, kMaxClipTempo) / 60.0;
}

/// The pinned moments: the file's start, then the markers.
std::vector<WarpMarker> pins(const AudioWarp& w)
{
    std::vector<WarpMarker> out;
    out.reserve(w.markers.size() + 1);
    out.push_back({0.0, 0.0});
    out.insert(out.end(), w.markers.begin(), w.markers.end());
    return out;
}

/// Whether the piece between two pins is in order and within the stretch limits.
bool pieceFits(const AudioWarp& w, const WarpMarker& a, const WarpMarker& b) noexcept
{
    const double files = b.file - a.file, beats = b.beat - a.beat;
    if (!(files > 0.0) || !(beats > 0.0))
        return false;
    const double stretch = beats / files / beatsPerFileSecond(w);
    return stretch >= kMinPieceStretch - kLimitSlack && stretch <= kMaxPieceStretch + kLimitSlack;
}

/// Whether every piece is in order and within the limits.
bool fits(const AudioWarp& w)
{
    const auto p = pins(w);
    for (size_t i = 1; i < p.size(); ++i)
        if (!pieceFits(w, p[i - 1], p[i]))
            return false;
    return true;
}

/// `edit(t)` for the largest t in [0, 1] that fits (t = 1 when it all fits): every edit here moves
/// beats linearly with t, so what fits is an interval from 0.
template <typename Edit> AudioWarp largestFitting(const AudioWarp& from, Edit edit)
{
    if (auto all = edit(1.0); fits(all))
        return all;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 48; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        (fits(edit(mid)) ? lo : hi) = mid;
    }
    auto out = edit(lo);
    return fits(out) ? out : from;
}

std::vector<size_t> sortedUnique(std::vector<size_t> v, size_t below)
{
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    v.erase(std::remove_if(v.begin(), v.end(), [below](size_t i) { return i >= below; }), v.end());
    return v;
}

/// `beats` and `file` scaled into [lo, hi] of the stretch: the range of the far end of a piece
/// from `a` when its other dimension is fixed.
struct Span
{
    double lo, hi;
};
Span clampSpan(Span a, Span b)
{
    return {std::max(a.lo, b.lo), std::min(a.hi, b.hi)};
}
} // namespace

std::string warpModeName(WarpMode mode)
{
    switch (mode)
    {
    case WarpMode::Beats:
        return "Beats";
    case WarpMode::Tones:
        return "Tones";
    case WarpMode::Repitch:
        return "Repitch";
    }
    return "Tones";
}

std::optional<WarpMode> warpModeFromName(const std::string& name)
{
    for (auto m : {WarpMode::Beats, WarpMode::Tones, WarpMode::Repitch})
        if (lower(warpModeName(m)) == lower(name))
            return m;
    return std::nullopt;
}

std::string tailsName(WarpTails tails)
{
    return tails == WarpTails::Loop ? "Loop" : "Fade";
}

std::optional<WarpTails> tailsFromName(const std::string& name)
{
    for (auto t : {WarpTails::Fade, WarpTails::Loop})
        if (lower(tailsName(t)) == lower(name))
            return t;
    return std::nullopt;
}

std::optional<double> tempoFromName(const std::string& name)
{
    const auto text = lower(name);
    for (size_t at = text.find("bpm"); at != std::string::npos; at = text.find("bpm", at + 3))
    {
        // The number just before it, past a space, an underscore or a dash.
        size_t end = at;
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '_' || text[end - 1] == '-'))
            --end;
        size_t begin = end;
        while (begin > 0 &&
               (std::isdigit(static_cast<unsigned char>(text[begin - 1])) != 0 || text[begin - 1] == '.'))
            --begin;
        if (begin == end || text[begin] == '.')
            continue;
        // Not the end of a longer word or number ("v2bpm" is no tempo).
        if (begin > 0 && std::isalpha(static_cast<unsigned char>(text[begin - 1])) != 0)
            continue;
        const double bpm = std::strtod(text.substr(begin, end - begin).c_str(), nullptr);
        if (bpm >= 40.0 && bpm <= 300.0)
            return bpm;
    }
    return std::nullopt;
}

double fileAtBeat(const AudioWarp& warp, double beat) noexcept
{
    const double perBeat = 1.0 / beatsPerFileSecond(warp);
    if (warp.markers.empty() || beat <= 0.0)
        return beat * perBeat;
    WarpMarker prev{0.0, 0.0};
    for (const auto& m : warp.markers)
    {
        if (beat < m.beat)
            return prev.file + (beat - prev.beat) * (m.file - prev.file) / (m.beat - prev.beat);
        prev = m;
    }
    return prev.file + (beat - prev.beat) * perBeat;
}

double beatAtFile(const AudioWarp& warp, double file) noexcept
{
    const double perSecond = beatsPerFileSecond(warp);
    if (warp.markers.empty() || file <= 0.0)
        return file * perSecond;
    WarpMarker prev{0.0, 0.0};
    for (const auto& m : warp.markers)
    {
        if (file < m.file)
            return prev.beat + (file - prev.file) * (m.beat - prev.beat) / (m.file - prev.file);
        prev = m;
    }
    return prev.beat + (file - prev.file) * perSecond;
}

double WarpPiece::tempo() const noexcept
{
    return file1 > file0 ? (beat1 - beat0) / (file1 - file0) * 60.0 : 0.0;
}

std::vector<WarpPiece> warpPieces(const AudioWarp& warp, double fileLength)
{
    std::vector<WarpPiece> out;
    WarpMarker prev{0.0, 0.0};
    for (const auto& m : warp.markers)
    {
        if (m.file >= fileLength)
            break;
        out.push_back({prev.beat, m.beat, prev.file, m.file});
        prev = m;
    }
    if (fileLength > prev.file)
        out.push_back({prev.beat, beatAtFile(warp, fileLength), prev.file, fileLength});
    return out;
}

double pieceStretch(const AudioWarp& warp, const WarpPiece& piece) noexcept
{
    return piece.tempo() / 60.0 / beatsPerFileSecond(warp);
}

AudioWarp reversedWarp(const AudioWarp& warp, double fileLength)
{
    AudioWarp out = warp;
    out.markers.clear();
    if (warp.markers.empty() || !(fileLength > 0.0))
        return out;
    const double end = beatAtFile(warp, fileLength);
    // Each pin seen from the file's end; the start's pin becomes the end's, unless the piece
    // before it plays at the clip's tempo (then it pins nothing the tempo doesn't).
    auto p = pins(warp);
    for (auto it = p.rbegin(); it != p.rend(); ++it)
    {
        const WarpMarker m{fileLength - it->file, end - it->beat};
        if (m.file <= 1e-9 || m.file > fileLength)
            continue;
        out.markers.push_back(m);
    }
    if (!out.markers.empty())
    {
        const auto& last = out.markers.back();
        const auto before = out.markers.size() > 1 ? out.markers[out.markers.size() - 2] : WarpMarker{};
        const double tempo = (last.beat - before.beat) / (last.file - before.file);
        if (std::abs(tempo - beatsPerFileSecond(warp)) < 1e-9 * std::max(1.0, tempo))
            out.markers.pop_back();
    }
    return out;
}

double ContentMap::fileAt(double content) const noexcept
{
    const double at = warped ? fileAtBeat(played, content * beatsPerSecond) : content;
    return reversed ? fileLength - at : at;
}

double ContentMap::playedAt(double content) const noexcept
{
    return warped ? fileAtBeat(played, content * beatsPerSecond) : content;
}

double ContentMap::contentAt(double file) const noexcept
{
    const double at = reversed ? fileLength - file : file;
    return warped ? beatAtFile(played, at) / beatsPerSecond : at;
}

double ContentMap::contentLength() const noexcept
{
    return warped ? beatAtFile(played, fileLength) / beatsPerSecond : fileLength;
}

ContentMap contentMap(const ClipInfo& clip)
{
    ContentMap m;
    m.warped = clip.warp.on;
    m.reversed = clip.sound.reversed;
    m.fileLength = clip.fileLength;
    m.beatsPerSecond = clip.beatsPerSecond > 0.0 ? clip.beatsPerSecond : 2.0;
    if (m.warped)
        m.played = m.reversed ? reversedWarp(clip.warp, clip.fileLength) : clip.warp;
    return m;
}

double fileTimeAt(const ClipInfo& clip, double content)
{
    return contentMap(clip).fileAt(content);
}

std::vector<WarpMarker> pieceStarts(const AudioWarp& warp, const std::vector<double>& transients,
                                    double fileLength)
{
    auto out = pins(warp);
    out.erase(std::remove_if(out.begin() + 1, out.end(),
                             [fileLength](const WarpMarker& m) { return m.file >= fileLength; }),
              out.end());
    if (warp.mode != WarpMode::Beats)
        return out;
    for (double t : transients)
    {
        if (!(t > 0.0) || t >= fileLength)
            continue;
        const bool near = std::any_of(out.begin(), out.end(), [t](const WarpMarker& m)
                                      { return std::abs(m.file - t) < kSameMoment; });
        if (!near)
            out.push_back({t, beatAtFile(warp, t)});
    }
    std::sort(out.begin(), out.end(),
              [](const WarpMarker& a, const WarpMarker& b) { return a.file < b.file; });
    return out;
}

std::optional<size_t> markerNear(const AudioWarp& warp, double file, double tolerance)
{
    std::optional<size_t> best;
    double bestDistance = tolerance;
    for (size_t i = 0; i < warp.markers.size(); ++i)
        if (const double d = std::abs(warp.markers[i].file - file); d <= bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    return best;
}

std::pair<AudioWarp, std::optional<size_t>> withMarkerAt(const AudioWarp& warp, double file)
{
    if (file <= 1e-9)
        return {warp, std::nullopt};
    if (auto near = markerNear(warp, file, 1e-6))
        return {warp, near};
    AudioWarp out = warp;
    const WarpMarker m{file, beatAtFile(warp, file)};
    auto at = std::upper_bound(out.markers.begin(), out.markers.end(), m,
                               [](const WarpMarker& a, const WarpMarker& b) { return a.file < b.file; });
    const auto index = static_cast<size_t>(at - out.markers.begin());
    out.markers.insert(at, m);
    return {out, index};
}

AudioWarp withMarkerMoved(const AudioWarp& warp, size_t index, double beat)
{
    if (index >= warp.markers.size())
        return warp;
    const auto p = pins(warp);
    const size_t i = index + 1; // in pins
    const double k = beatsPerFileSecond(warp);
    const auto& prev = p[i - 1];
    const auto& m = p[i];
    // The piece before: its beats over its file within the limits.
    Span allowed{prev.beat + kMinPieceStretch * k * (m.file - prev.file),
                 prev.beat + kMaxPieceStretch * k * (m.file - prev.file)};
    if (i + 1 < p.size())
    {
        const auto& next = p[i + 1];
        allowed = clampSpan(allowed, {next.beat - kMaxPieceStretch * k * (next.file - m.file),
                                      next.beat - kMinPieceStretch * k * (next.file - m.file)});
    }
    if (allowed.lo > allowed.hi)
        return warp;
    AudioWarp out = warp;
    out.markers[index].beat = std::clamp(beat, allowed.lo, allowed.hi);
    return out;
}

AudioWarp withMarkerSlid(const AudioWarp& warp, size_t index, double file)
{
    if (index >= warp.markers.size())
        return warp;
    const auto p = pins(warp);
    const size_t i = index + 1;
    const double k = beatsPerFileSecond(warp);
    const auto& prev = p[i - 1];
    const auto& m = p[i];
    // The piece before: its beats are fixed, so its file length is within the limits.
    const double before = m.beat - prev.beat;
    Span allowed{prev.file + before / (kMaxPieceStretch * k), prev.file + before / (kMinPieceStretch * k)};
    if (i + 1 < p.size())
    {
        const auto& next = p[i + 1];
        const double after = next.beat - m.beat;
        allowed = clampSpan(allowed, {next.file - after / (kMinPieceStretch * k),
                                      next.file - after / (kMaxPieceStretch * k)});
    }
    if (allowed.lo > allowed.hi)
        return warp;
    AudioWarp out = warp;
    out.markers[index].file = std::clamp(file, allowed.lo, allowed.hi);
    return out;
}

AudioWarp withMarkersMoved(const AudioWarp& warp, const std::vector<size_t>& indices, double beats)
{
    const auto moved = sortedUnique(indices, warp.markers.size());
    if (moved.empty() || beats == 0.0)
        return warp;
    return largestFitting(warp,
                          [&](double t)
                          {
                              AudioWarp out = warp;
                              for (auto i : moved)
                                  out.markers[i].beat += beats * t;
                              return out;
                          });
}

AudioWarp withoutMarkers(const AudioWarp& warp, const std::vector<size_t>& indices)
{
    const auto gone = sortedUnique(indices, warp.markers.size());
    AudioWarp out = warp;
    for (auto it = gone.rbegin(); it != gone.rend(); ++it)
        out.markers.erase(out.markers.begin() + static_cast<std::ptrdiff_t>(*it));
    // Two neighbours left too far apart in tempo can't be joined by one piece: what doesn't fit
    // goes too, from the end of the gap.
    while (!fits(out))
    {
        const auto p = pins(out);
        for (size_t i = 1; i < p.size(); ++i)
            if (!pieceFits(out, p[i - 1], p[i]))
            {
                out.markers.erase(out.markers.begin() + static_cast<std::ptrdiff_t>(i - 1));
                break;
            }
    }
    return out;
}

AudioWarp warpedFrom(const AudioWarp& warp, size_t index)
{
    AudioWarp out = warp;
    if (index + 1 < out.markers.size())
        out.markers.resize(index + 1);
    return out;
}

AudioWarp withMarkersAtTransients(const AudioWarp& warp, const std::vector<double>& transients,
                                  double fromFile, double toFile)
{
    AudioWarp out = warp;
    for (double t : transients)
        if (t >= fromFile && t < toFile && t > kSameMoment && !markerNear(out, t, kSameMoment))
            out = withMarkerAt(out, t).first;
    return out;
}

AudioWarp withTempo(const AudioWarp& warp, double tempo)
{
    AudioWarp out = warp;
    out.tempo = std::clamp(tempo, kMinClipTempo, kMaxClipTempo);
    const double scale = out.tempo / std::clamp(warp.tempo, kMinClipTempo, kMaxClipTempo);
    for (auto& m : out.markers)
        m.beat *= scale;
    return out;
}

AudioWarp withRangeStretched(const AudioWarp& warp, double from, double to, double newFrom, double newTo)
{
    if (!(to > from) || !(newTo > newFrom))
        return warp;
    AudioWarp pinned = withMarkerAt(warp, fileAtBeat(warp, from)).first;
    pinned = withMarkerAt(pinned, fileAtBeat(warp, to)).first;
    const double fileFrom = fileAtBeat(warp, from), fileTo = fileAtBeat(warp, to);
    return largestFitting(warp,
                          [&](double t)
                          {
                              const double a = from + (newFrom - from) * t, b = to + (newTo - to) * t;
                              AudioWarp out = pinned;
                              for (auto& m : out.markers)
                                  if (m.file >= fileFrom - 1e-9 && m.file <= fileTo + 1e-9)
                                      m.beat = a + (m.beat - from) * (b - a) / (to - from);
                              return out;
                          });
}

AudioWarp quantised(const AudioWarp& warp, const std::vector<double>& transients, double fromBeat,
                    double toBeat, double originBeat, const WarpQuantise& settings)
{
    if (!(settings.gridBeats > 0.0))
        return warp;
    AudioWarp out = warp;
    for (double t : transients)
    {
        const double beat = beatAtFile(warp, t);
        if (beat < fromBeat || beat >= toBeat || t <= kSameMoment)
            continue;
        const double song = originBeat + beat;
        const double step = std::round(song / settings.gridBeats);
        double target = step * settings.gridBeats;
        if (static_cast<long long>(step) % 2 != 0)
            target += std::clamp(settings.swing, 0.0, 1.0) * settings.gridBeats * 0.5;
        const double wanted = beat + std::clamp(settings.strength, 0.0, 1.0) * (target - song);
        auto index = markerNear(out, t, kSameMoment);
        if (!index)
            std::tie(out, index) = withMarkerAt(out, t);
        if (index)
            out = withMarkerMoved(out, *index, wanted);
    }
    return out;
}
} // namespace fd::model
