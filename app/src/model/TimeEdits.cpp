#include "model/TimeEdits.h"

#include <algorithm>
#include <cmath>

namespace fd::model
{
namespace
{
constexpr double kEpsilon = 1e-6; // seconds: closer than this is the same time

bool near(double a, double b) noexcept
{
    return std::abs(a - b) <= kEpsilon;
}

void byTime(AutomationPoints& points)
{
    std::stable_sort(points.begin(), points.end(),
                     [](const auto& a, const auto& b) { return a.time < b.time; });
}
} // namespace

TimeSelection timeSelectionOf(const Selection& selection)
{
    if (!selection.hasTime())
        return {};
    return {*selection.time, selection.rowTracks, selection.rowLanes};
}

AutomationPoints clearPoints(const AutomationPoints& points, double start, double end)
{
    const bool anyInside = std::any_of(points.begin(), points.end(), [&](const auto& p)
                                       { return p.time > start + kEpsilon && p.time < end - kEpsilon; });
    if (!anyInside)
        return points;
    const float left = automationValueBefore(points, start), right = automationValueAt(points, end);
    const float after = automationCurveAt(points, end);
    AutomationPoints out;
    bool atStart = false, atEnd = false;
    for (const auto& p : points)
    {
        if (p.time < start - kEpsilon)
            out.push_back(p);
        else if (near(p.time, start))
        {
            out.push_back(p);
            atStart = true;
        }
        else if (near(p.time, end))
        {
            out.push_back(p);
            atEnd = true;
        }
        else if (p.time > end + kEpsilon)
            out.push_back(p);
    }
    if (!atStart)
        out.push_back({start, left, 0.0f});
    if (!atEnd)
        out.push_back({end, right, after});
    byTime(out);
    // Inside, a straight line between the edges.
    for (size_t i = out.size(); i-- > 0;)
        if (near(out[i].time, start))
        {
            out[i].curve = 0.0f;
            break;
        }
    return out;
}

AutomationPoints piecePoints(const AutomationPoints& points, double start, double end)
{
    AutomationPoints out;
    if (points.empty() || !(end > start))
        return out;
    if (std::none_of(points.begin(), points.end(), [&](const auto& p) { return near(p.time, start); }))
        out.push_back({0.0, automationValueAt(points, start), automationCurveAt(points, start)});
    for (const auto& p : points)
        if (p.time >= start - kEpsilon && p.time < end - kEpsilon)
            out.push_back({std::max(0.0, p.time - start), p.value, p.curve});
    out.push_back({end - start, automationValueBefore(points, end), 0.0f});
    return out;
}

AutomationPoints placePoints(const AutomationPoints& points, const AutomationPoints& piece, double at,
                             double length)
{
    if (piece.empty())
        return points;
    const double end = at + length;
    AutomationPoints out;
    for (const auto& p : points)
        if (p.time < at - kEpsilon)
            out.push_back(p);
    if (!points.empty())
    {
        // The curve before the stretch keeps its value up to it.
        const float left = automationValueBefore(points, at);
        if (!(near(piece.front().time, 0.0) && std::abs(piece.front().value - left) < 1e-6f))
            out.push_back({at, left, 0.0f});
    }
    for (const auto& q : piece)
        out.push_back({at + q.time, q.value, q.curve});
    if (!points.empty())
    {
        // ...and the curve after it keeps its value from it.
        const bool pointAtEnd =
            std::any_of(points.begin(), points.end(), [&](const auto& p) { return near(p.time, end); });
        const float right = automationValueAt(points, end);
        if (!pointAtEnd && !(near(piece.back().time, length) && std::abs(piece.back().value - right) < 1e-6f))
            out.push_back({end, right, automationCurveAt(points, end)});
        for (const auto& p : points)
            if (p.time >= end - kEpsilon && (p.time > end + kEpsilon || pointAtEnd))
                out.push_back(p);
    }
    byTime(out);
    return out;
}

std::set<size_t> pointsIn(const AutomationPoints& points, double start, double end)
{
    std::set<size_t> out;
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i].time >= start - kEpsilon && points[i].time < end - kEpsilon)
            out.insert(i);
    return out;
}

StretchFades stretchFades(const std::vector<ClipInfo>& asked, TimeSpan span)
{
    StretchFades out;
    auto clips = asked;
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    const double s = span.start, e = span.end;
    const auto joins = [](const ClipInfo& a, const ClipInfo& b)
    { return a.kind == ClipInfo::Kind::Audio && b.kind == ClipInfo::Kind::Audio && near(a.end(), b.start); };
    for (size_t i = 0; i < clips.size(); ++i)
    {
        const auto& c = clips[i];
        if (c.kind != ClipInfo::Kind::Audio)
            continue;
        const bool joinBefore = i > 0 && joins(clips[i - 1], c);
        const bool joinAfter = i + 1 < clips.size() && joins(c, clips[i + 1]);
        // Across the join with the next clip: a crossfade over the stretch.
        if (joinAfter && s < c.end() - kEpsilon && e > c.end() + kEpsilon)
            out.crossfades.push_back({c.id, e - s, s - c.end()});
        // From its start (or a gap before it) into it: a fade-in to the stretch's end.
        if (s <= c.start + kEpsilon && e > c.start + kEpsilon && e < c.end() - kEpsilon &&
            !(joinBefore && s < c.start - kEpsilon))
            out.fadeIns.push_back({c.id, e - c.start});
        // From inside it to its end (or past it, into a gap): a fade-out from the stretch's start.
        if (s > c.start + kEpsilon && s < c.end() - kEpsilon && e >= c.end() - kEpsilon &&
            !(joinAfter && e > c.end() + kEpsilon))
            out.fadeOuts.push_back({c.id, c.end() - s});
    }
    return out;
}
} // namespace fd::model
