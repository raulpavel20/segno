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

// ---- time inside a MIDI clip -------------------------------------------------------------------

Plan clipTimePlan(ClipTimeCommand command, double a, double b)
{
    if (!(b > a))
        return {};
    switch (command)
    {
    case ClipTimeCommand::Insert:
        return planInsert(a, b - a);
    case ClipTimeCommand::Delete:
        return planDelete(a, b);
    case ClipTimeCommand::Duplicate:
        return planCopy(a, b, b);
    }
    return {};
}

namespace
{
/// The notes in noteOrder, those marked selected by index.
NoteEdit sortedNotes(std::vector<std::pair<Note, bool>> items)
{
    std::stable_sort(items.begin(), items.end(),
                     [](const auto& x, const auto& y) { return noteOrder(x.first, y.first); });
    NoteEdit out;
    for (auto& [note, selected] : items)
    {
        if (selected)
            out.selection.insert(out.notes.size());
        out.notes.push_back(note);
    }
    return out;
}
} // namespace

NoteEdit planNotes(const std::vector<Note>& notes, const Plan& plan)
{
    if (plan.empty())
        return {notes, {}};
    const auto cuts = cutPoints(plan);
    struct Piece
    {
        Note note;
        std::size_t origin = 0;
        bool copy = false;
    };
    std::vector<Piece> pieces;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        // Cut at every cut point inside it, each piece going with its own stretch.
        const auto& n = notes[i];
        double from = n.start;
        const double end = n.start + n.length;
        const auto place = [&](double pieceEnd)
        {
            const auto to = landingsOf(plan, from);
            const auto land = [&](double start, bool copy)
            {
                auto piece = n;
                piece.start = start;
                piece.length = pieceEnd - from;
                pieces.push_back({piece, i, copy});
            };
            if (to.itself)
                land(*to.itself, false);
            for (const double copy : to.copies)
                land(copy, true);
            from = pieceEnd;
        };
        for (const double c : cuts)
            if (c > from + kEpsilon && c < end - kEpsilon)
                place(c);
        place(end);
    }
    // The pieces of a note that meet again join, so a note held across deleted time is one note.
    std::vector<std::pair<Note, bool>> out;
    std::vector<std::size_t> originOf;
    std::vector<bool> copyOf;
    for (const auto& p : pieces)
    {
        bool joined = false;
        for (std::size_t k = 0; k < out.size() && !joined; ++k)
            if (!p.copy && !copyOf[k] && originOf[k] == p.origin &&
                near(out[k].first.start + out[k].first.length, p.note.start))
            {
                out[k].first.length += p.note.length;
                joined = true;
            }
        if (!joined)
        {
            out.emplace_back(p.note, p.copy);
            originOf.push_back(p.origin);
            copyOf.push_back(p.copy);
        }
    }
    return sortedNotes(std::move(out));
}

std::vector<ControllerLane> planControllers(const std::vector<ControllerLane>& lanes, const Plan& plan)
{
    SongContent content;
    for (std::size_t i = 0; i < lanes.size(); ++i)
        content.lanes.push_back({i, lanes[i].points});
    auto out = lanes;
    for (const auto& changed : applyPlan(content, plan).lanes)
        out[changed.id].points = changed.points;
    return out;
}

double clipTimeEdge(ClipTimeCommand command, double a, double b, double t, bool isEnd)
{
    // An end edge just at or inside a duplicated stretch takes the copy in with it.
    if (command == ClipTimeCommand::Duplicate && isEnd && t > a + kEpsilon)
        return std::max(t, b) + (b - a);
    return planEdge(clipTimePlan(command, a, b), t, isEnd);
}

NoteEdit splitNotesAt(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double a,
                      double b)
{
    std::vector<std::pair<Note, bool>> out;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const bool target = selection.empty() || selection.contains(i);
        if (!target)
        {
            out.emplace_back(n, false);
            continue;
        }
        double from = n.start;
        const double end = n.start + n.length;
        const auto piece = [&](double to)
        {
            auto p = n;
            p.start = from;
            p.length = to - from;
            // A selected note's piece inside the stretch stays selected.
            out.emplace_back(p, !selection.empty() && from >= a - kEpsilon && to <= b + kEpsilon);
            from = to;
        };
        for (const double edge : {a, b})
            if (edge - from >= minNoteLength - kEpsilon && end - edge >= minNoteLength - kEpsilon)
                piece(edge);
        piece(end);
    }
    return sortedNotes(std::move(out));
}
} // namespace fd::model
