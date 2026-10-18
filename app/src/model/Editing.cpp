#include "model/Editing.h"

#include "model/ClipLoops.h"
#include "model/Groups.h"
#include "model/Takes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fd::model
{
std::optional<std::pair<const ClipInfo*, int>> findClip(const Snapshot& snapshot, std::uint64_t id)
{
    for (size_t t = 0; t < snapshot.tracks.size(); ++t)
        for (const auto& c : snapshot.tracks[t].clips)
            if (c.id == id)
                return std::pair{&c, static_cast<int>(t)};
    return std::nullopt;
}

std::optional<ClipHit> hitClip(const Snapshot& snapshot, const TrackLayout& layout, const Timeline& timeline,
                               double x, double y, double edgePixels)
{
    return hitClip(snapshot, layout, {}, timeline, x, y, edgePixels);
}

std::optional<ClipHit> hitClip(const Snapshot& snapshot, const TrackLayout& layout, const std::vector<int>& rowTracks,
                               const Timeline& timeline, double x, double y, double edgePixels)
{
    const auto row = layout.hit(y);
    if (!row || row->offset >= layout.baseHeight(row->track))
        return std::nullopt;
    const int track = rowTracks.empty() ? row->track
                      : row->track < static_cast<int>(rowTracks.size()) ? rowTracks[static_cast<size_t>(row->track)]
                                                                       : -1;
    if (track < 0 || track >= static_cast<int>(snapshot.tracks.size()))
        return std::nullopt;
    const double t = timeline.timeForX(x);
    const auto& clips = snapshot.tracks[static_cast<size_t>(track)].clips;
    // Later clips are drawn on top: search from the end.
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        if (t < it->start || t >= it->end())
            continue;
        const double x0 = timeline.xForTime(it->start), x1 = timeline.xForTime(it->end());
        const double edge = std::min(edgePixels, (x1 - x0) / 3.0);
        ClipHit hit{it->id, track, ClipHit::Zone::Body};
        const auto& owner = snapshot.tracks[static_cast<size_t>(track)];
        if (x - x0 < edge && !isInnerEdge(owner, *it, true))
            hit.zone = ClipHit::Zone::StartEdge;
        else if (x1 - x < edge && !isInnerEdge(owner, *it, false))
            hit.zone = ClipHit::Zone::EndEdge;
        return hit;
    }
    return std::nullopt;
}

std::vector<ClipEdit> dragClips(const Snapshot& snapshot, const std::set<std::uint64_t>& selection, const Drag& drag,
                                const std::function<double(double)>& snap)
{
    std::vector<std::pair<const ClipInfo*, int>> clips;
    for (auto id : selection)
        if (auto found = findClip(snapshot, id))
            clips.push_back(*found);
    const auto anchor = findClip(snapshot, drag.anchor);
    if (clips.empty() || !anchor)
        return {};
    const ClipInfo& a = *anchor->first;

    std::vector<ClipEdit> out;
    switch (drag.kind)
    {
    case DragKind::Move:
    {
        double delta = snap(a.start + drag.deltaSeconds) - a.start;
        // By tracks that hold clips: a group's row is skipped (groups.md §6.5).
        const auto holders = clipTracks(snapshot);
        int dTrack = drag.deltaTracks;
        double earliest = std::numeric_limits<double>::max();
        int lowest = std::numeric_limits<int>::max(), highest = std::numeric_limits<int>::min();
        for (const auto& [c, t] : clips)
        {
            earliest = std::min(earliest, c->start);
            lowest = std::min(lowest, clipTrackRank(snapshot, t));
            highest = std::max(highest, clipTrackRank(snapshot, t));
        }
        delta = std::max(delta, -earliest); // nothing before zero
        dTrack = std::clamp(dTrack, -lowest, static_cast<int>(holders.size()) - 1 - highest);
        for (const auto& [c, t] : clips)
            out.push_back({c->id,
                           snapshot
                               .tracks[static_cast<size_t>(
                                   holders[static_cast<size_t>(clipTrackRank(snapshot, t) + dTrack)])]
                               .id,
                           c->start + delta, c->length, c->offset});
        break;
    }
    case DragKind::TrimStart:
    {
        const double delta = snap(a.start + drag.deltaSeconds) - a.start;
        for (const auto& [c, t] : clips)
        {
            if (isInnerEdge(snapshot.tracks[static_cast<size_t>(t)], *c, true))
                continue; // between two of its parts: comping (C5c), not a trim
            const auto p = trimmedStart(*c, c->start + delta);
            out.push_back({c->id, snapshot.tracks[static_cast<size_t>(t)].id, p.start, p.length, p.offset});
        }
        break;
    }
    case DragKind::TrimEnd:
    {
        const double delta = snap(a.end() + drag.deltaSeconds) - a.end();
        for (const auto& [c, t] : clips)
        {
            if (isInnerEdge(snapshot.tracks[static_cast<size_t>(t)], *c, false))
                continue;
            const auto p = trimmedEnd(*c, c->end() + delta);
            out.push_back({c->id, snapshot.tracks[static_cast<size_t>(t)].id, p.start, p.length, p.offset});
        }
        break;
    }
    case DragKind::LoopExtend:
    {
        const double delta = snap(a.end() + drag.deltaSeconds) - a.end();
        if (delta <= 1e-9)
            return {}; // it never shortens: letting go where it started changes nothing
        for (const auto& [c, t] : clips)
        {
            const auto& track = snapshot.tracks[static_cast<size_t>(t)];
            if (isInnerEdge(track, *c, false))
                continue;
            if (!whyCantLoop(*c, track).empty()) // trimmed instead, as far as it goes
            {
                const auto p = trimmedEnd(*c, c->end() + delta);
                if (p.length > c->length + 1e-9)
                    out.push_back({c->id, track.id, p.start, p.length, p.offset});
                continue;
            }
            const auto looped = loopOn(*c);
            out.push_back({c->id, track.id, looped.start, looped.length + delta, looped.offset,
                           c->loop.on() ? std::nullopt : std::optional(looped.loop)});
        }
        break;
    }
    case DragKind::LoopLength:
    {
        // The grabbed seam where it's dragged (snapped); the loop's length follows from the lap the
        // clip starts in.
        if (!a.loop.on() || drag.seam < 1)
            return {};
        const double first = lapAnchor(a);
        const double seam = first + drag.seam * a.loop.length;
        const double at = snap(first + (seam + drag.deltaSeconds - first) / drag.seam);
        const auto p = withLoopLength(a, at - first);
        if (p == placementOf(a))
            return {};
        out.push_back({a.id, snapshot.tracks[static_cast<size_t>(anchor->second)].id, p.start, p.length, p.offset, p.loop});
        break;
    }
    }
    return out;
}

std::vector<std::uint64_t> clipsInBox(const Snapshot& snapshot, double t0, double t1, int track0, int track1)
{
    if (t1 < t0)
        std::swap(t0, t1);
    if (track1 < track0)
        std::swap(track0, track1);
    std::vector<std::uint64_t> out;
    for (int t = std::max(0, track0); t <= std::min(track1, static_cast<int>(snapshot.tracks.size()) - 1); ++t)
        for (const auto& c : snapshot.tracks[static_cast<size_t>(t)].clips)
            if (c.start <= t1 && c.end() >= t0)
                out.push_back(c.id);
    return out;
}

std::vector<ClipCut> clearRange(const std::vector<ClipInfo>& clips, double start, double end,
                                const std::set<std::uint64_t>& except)
{
    constexpr double tolerance = 1e-6;
    std::vector<ClipCut> cuts;
    for (const auto& c : clips)
    {
        if (except.contains(c.id) || c.start >= end - tolerance || c.end() <= start + tolerance)
            continue;
        const bool coversStart = start <= c.start + tolerance;
        const bool coversEnd = end >= c.end() - tolerance;
        ClipCut cut{.clip = c.id};
        if (coversStart && coversEnd)
            cut.kind = ClipCut::Kind::Remove;
        else if (!coversStart && !coversEnd)
        {
            cut.kind = ClipCut::Kind::SplitAround;
            cut.from = start;
            cut.to = end;
        }
        else if (!coversStart) // the placed range covers the clip's end: keep its head
        {
            cut.kind = ClipCut::Kind::Trim;
            cut.start = c.start;
            cut.length = start - c.start;
            cut.offset = c.offset;
        }
        else // covers the clip's start: keep its tail
        {
            cut.kind = ClipCut::Kind::Trim;
            cut.start = end;
            cut.length = c.end() - end;
            cut.offset = c.offset + (end - c.start);
        }
        cuts.push_back(cut);
    }
    return cuts;
}

std::vector<ClipEdit> duplicatePlacements(const Snapshot& snapshot, const std::set<std::uint64_t>& selection)
{
    double first = std::numeric_limits<double>::max(), last = 0.0;
    std::vector<std::pair<const ClipInfo*, int>> clips;
    for (auto id : selection)
        if (auto found = findClip(snapshot, id))
        {
            clips.push_back(*found);
            first = std::min(first, found->first->start);
            last = std::max(last, found->first->end());
        }
    std::vector<ClipEdit> out;
    const double span = last - first;
    for (const auto& [c, t] : clips)
        out.push_back({c->id, snapshot.tracks[static_cast<size_t>(t)].id, c->start + span, c->length, c->offset});
    return out;
}
} // namespace fd::model
