#include "model/Sections.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fd::model
{
namespace
{
constexpr double kEpsilon = 1e-6; // seconds: closer than this is the same time

bool named(const std::vector<SectionInfo>& sections, const std::string& name)
{
    return std::any_of(sections.begin(), sections.end(), [&](const auto& s) { return s.name == name; });
}

float valueBefore(const AutomationPoints& points, double t)
{
    return automationValueBefore(points, t);
}

float curveAt(const AutomationPoints& points, double t)
{
    return automationCurveAt(points, t);
}

/// Stretches that follow each other in the old song too become one; zero-length ones go.
Plan merged(const Plan& plan)
{
    Plan out;
    for (const auto& s : plan)
    {
        if (!s.empty() && s.to - s.from <= kEpsilon)
            continue;
        if (!out.empty() && !out.back().empty() && !s.empty() && !out.back().copy && !s.copy &&
            std::abs(out.back().to - s.from) <= kEpsilon)
            out.back().to = s.to;
        else
            out.push_back(s);
    }
    return out;
}

bool inside(double t, const Stretch& s)
{
    return t >= s.from - kEpsilon && t < s.to - kEpsilon;
}
} // namespace

bool sectionFits(const std::vector<SectionInfo>& sections, double start, double end, std::uint64_t ignore,
                 double shortest)
{
    if (end - start < shortest - kEpsilon || start < -kEpsilon)
        return false;
    return std::none_of(sections.begin(), sections.end(), [&](const auto& s)
                        { return s.id != ignore && start < s.end - kEpsilon && end > s.start + kEpsilon; });
}

const SectionInfo* sectionAt(const std::vector<SectionInfo>& sections, double seconds)
{
    for (const auto& s : sections)
        if (seconds >= s.start - kEpsilon && seconds < s.end - kEpsilon)
            return &s;
    return nullptr;
}

std::string newSectionName(const std::vector<SectionInfo>& sections)
{
    for (char c = 'A'; c <= 'Z'; ++c)
        if (const std::string name(1, c); !named(sections, name))
            return name;
    for (int n = 27;; ++n)
        if (auto name = "Section " + std::to_string(n); !named(sections, name))
            return name;
}

std::string sectionBaseName(const std::string& name)
{
    if (const auto space = name.find_last_of(' ');
        space != std::string::npos && space > 0 && space + 1 < name.size() &&
        std::all_of(name.begin() + static_cast<long>(space) + 1, name.end(),
                    [](char c) { return c >= '0' && c <= '9'; }))
        return name.substr(0, space);
    return name;
}

std::string copyName(const std::vector<SectionInfo>& sections, const std::string& name)
{
    // "Chorus 2": the base is "Chorus" and the copies count on from 2.
    std::string base = name;
    int from = 2;
    if (const auto space = name.find_last_of(' ');
        space != std::string::npos && space + 1 < name.size() &&
        std::all_of(name.begin() + static_cast<long>(space) + 1, name.end(),
                    [](char c) { return c >= '0' && c <= '9'; }))
    {
        base = name.substr(0, space);
        from = std::stoi(name.substr(space + 1)) + 1;
    }
    for (int n = std::max(2, from);; ++n)
        if (auto candidate = base + " " + std::to_string(n); !named(sections, candidate))
            return candidate;
}

int newSectionColour(const std::vector<SectionInfo>& sections, int paletteSize)
{
    if (sections.empty() || paletteSize <= 0)
        return 0;
    const auto last = std::max_element(sections.begin(), sections.end(),
                                       [](const auto& a, const auto& b) { return a.start < b.start; });
    return (last->colour + 1) % paletteSize;
}

std::vector<double> sectionBoundaries(const std::vector<SectionInfo>& sections)
{
    std::vector<double> times;
    for (const auto& s : sections)
    {
        times.push_back(s.start);
        times.push_back(s.end);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
                            [](double a, double b) { return std::abs(a - b) <= kEpsilon; }),
                times.end());
    return times;
}

std::optional<double> nearestBoundary(const std::vector<SectionInfo>& sections, double seconds)
{
    std::optional<double> best;
    for (const double b : sectionBoundaries(sections))
        if (!best || std::abs(b - seconds) < std::abs(*best - seconds))
            best = b;
    return best;
}

std::optional<double> previousSectionStart(const std::vector<SectionInfo>& sections, double seconds)
{
    std::optional<double> best;
    for (const auto& s : sections)
        if (s.start < seconds - kEpsilon && (!best || s.start > *best))
            best = s.start;
    return best;
}

std::optional<double> nextSectionStart(const std::vector<SectionInfo>& sections, double seconds)
{
    std::optional<double> best;
    for (const auto& s : sections)
        if (s.start > seconds + kEpsilon && (!best || s.start < *best))
            best = s.start;
    return best;
}

std::optional<Landing> landRun(const std::vector<SectionInfo>& sections, std::pair<double, double> span,
                               double start, bool copy)
{
    const auto [s, e] = span;
    const double length = e - s;
    // Out of any section that isn't in the run: to its nearer edge.
    const auto between = [&](double t)
    {
        for (const auto& x : sections)
        {
            const bool inRun = x.start >= s - kEpsilon && x.end <= e + kEpsilon;
            if (!inRun && t > x.start + kEpsilon && t < x.end - kEpsilon)
                return t - x.start < x.end - t ? x.start : x.end;
        }
        return t;
    };
    if (copy)
    {
        const double to = between(std::max(0.0, start));
        if (to > s + kEpsilon && to < e - kEpsilon)
            return std::nullopt; // inside itself
        return Landing{to, to};
    }
    if (start < s - kEpsilon)
    {
        const double to = std::min(between(std::max(0.0, start)), s);
        if (to >= s - kEpsilon)
            return std::nullopt;
        return Landing{to, to};
    }
    if (start > s + kEpsilon)
    {
        const double to = std::max(between(start + length), e);
        if (to <= e + kEpsilon)
            return std::nullopt;
        return Landing{to, to - length};
    }
    return std::nullopt;
}

std::optional<std::pair<double, double>> spanOf(const std::vector<SectionInfo>& sections,
                                                const std::vector<std::uint64_t>& ids)
{
    std::optional<std::pair<double, double>> span;
    for (const auto& s : sections)
        if (std::find(ids.begin(), ids.end(), s.id) != ids.end())
            span = span ? std::pair{std::min(span->first, s.start), std::max(span->second, s.end)}
                        : std::pair{s.start, s.end};
    return span;
}

// ---- plans --------------------------------------------------------------------------------

Plan planMove(double start, double end, double to)
{
    if (to > start - kEpsilon && to < end + kEpsilon)
        return {}; // onto itself: it doesn't move
    if (to <= start)
        return merged({{0.0, to}, {start, end}, {to, start}, {end, kSongEnd}});
    return merged({{0.0, start}, {end, to}, {start, end}, {to, kSongEnd}});
}

Plan planCopy(double start, double end, double to)
{
    if (to > start + kEpsilon && to < end - kEpsilon)
        return {}; // into itself
    return merged({{0.0, to}, {start, end, 0.0, true}, {to, kSongEnd}});
}

Plan planDelete(double start, double end)
{
    return merged({{0.0, start}, {end, kSongEnd}});
}

Plan planInsert(double at, double length)
{
    if (length <= kEpsilon)
        return {};
    return merged({{0.0, at}, {at, at, length}, {at, kSongEnd}});
}

Plan planPaste(double at, double length)
{
    if (length <= kEpsilon)
        return {};
    return merged({{0.0, at}, {at, at, length, false, true}, {at, kSongEnd}});
}

double pasteLanding(const std::vector<SectionInfo>& sections, double at)
{
    at = std::max(0.0, at);
    for (const auto& s : sections)
        if (at > s.start + kEpsilon && at < s.end - kEpsilon)
            return at - s.start <= s.end - at ? s.start : s.end;
    return at;
}

SongPiece pieceOf(const SongContent& content, double start, double end)
{
    SongPiece piece{.length = std::max(0.0, end - start)};
    for (const auto& s : content.sections)
        if (s.start >= start - kEpsilon && s.end <= end + kEpsilon)
        {
            auto shifted = s;
            shifted.start -= start;
            shifted.end -= start;
            piece.sections.push_back(shifted);
        }
    for (const auto& lane : content.lanes)
    {
        if (lane.points.empty())
            continue;
        // Its values at both edges pinned, so it pastes the curve it had here.
        AutomationPoints points;
        if (std::none_of(lane.points.begin(), lane.points.end(),
                         [&](const auto& p) { return std::abs(p.time - start) <= kEpsilon; }))
            points.push_back({0.0, automationValueAt(lane.points, start), curveAt(lane.points, start)});
        for (const auto& p : lane.points)
            if (p.time >= start - kEpsilon && p.time < end - kEpsilon)
                points.push_back({std::max(0.0, p.time - start), p.value, p.curve});
        points.push_back({piece.length, valueBefore(lane.points, end), 0.0f});
        piece.lanes.push_back({lane.id, std::move(points)});
    }
    return piece;
}

std::vector<double> cutPoints(const Plan& plan)
{
    std::vector<double> times;
    for (const auto& s : plan)
        for (const double t : {s.from, s.to})
            if (t > kEpsilon && std::isfinite(t))
                times.push_back(t);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
                            [](double a, double b) { return std::abs(a - b) <= kEpsilon; }),
                times.end());
    return times;
}

namespace
{
/// Where each of a (merged) plan's stretches lands.
std::vector<double> landsOf(const Plan& plan)
{
    std::vector<double> lands;
    double at = 0.0;
    for (const auto& s : plan)
    {
        lands.push_back(at);
        at += s.length();
    }
    return lands;
}
} // namespace

PlanLandings landingsOf(const Plan& asked, double t)
{
    const auto plan = merged(asked);
    const auto lands = landsOf(plan);
    PlanLandings out;
    for (size_t k = 0; k < plan.size(); ++k)
        if (!plan[k].empty() && inside(t, plan[k]))
        {
            const double to = lands[k] + (t - plan[k].from);
            if (plan[k].copy)
                out.copies.push_back(to);
            else
                out.itself = to;
        }
    return out;
}

double planEdge(const Plan& asked, double t, bool isEnd)
{
    const auto plan = merged(asked);
    const auto lands = landsOf(plan);
    for (size_t k = 0; k < plan.size(); ++k)
    {
        const auto& st = plan[k];
        if (st.empty() || st.copy)
            continue;
        const bool in = isEnd ? (t > st.from + kEpsilon && t <= st.to + kEpsilon) : inside(t, st);
        if (in)
            return lands[k] + (t - st.from);
    }
    double closed = 0.0;
    for (size_t k = 0; k < plan.size(); ++k)
        if (!plan[k].empty() && !plan[k].copy && plan[k].to <= t + kEpsilon)
            closed = std::max(closed, lands[k] + plan[k].length());
    return closed;
}

PlanResult applyPlan(const SongContent& content, const Plan& asked,
                     const std::optional<SectionInfo>& newSection, const SongPiece* piece)
{
    PlanResult result;
    const auto plan = merged(asked);
    if (plan.empty())
    {
        result.sections = content.sections;
        return result;
    }
    // Where each stretch lands.
    const auto lands = landsOf(plan);

    // Clips.
    for (const auto& c : content.clips)
    {
        const auto to = landingsOf(plan, c.start);
        if (!to.itself)
            result.deletes.push_back(c.id);
        else if (std::abs(*to.itself - c.start) > kEpsilon)
            result.moves.push_back({c.id, *to.itself});
        for (const double copy : to.copies)
            result.copies.push_back({c.id, copy});
    }

    // Automation: each stretch's points where it lands, with its edges' values pinned. A lane
    // whose points all come before the first cut doesn't change.
    const auto cuts = cutPoints(plan);
    const auto pieceLane = [piece](std::uint64_t id) -> const SongContent::Lane*
    {
        if (piece != nullptr)
            for (const auto& l : piece->lanes)
                if (l.id == id)
                    return &l;
        return nullptr;
    };
    const bool pastes = std::any_of(plan.begin(), plan.end(), [](const auto& s) { return s.pasted; });
    for (const auto& lane : content.lanes)
    {
        const auto* fromPiece = pastes ? pieceLane(lane.id) : nullptr;
        if (lane.points.empty() || cuts.empty() ||
            (fromPiece == nullptr && std::all_of(lane.points.begin(), lane.points.end(), [&](const auto& p)
                                                 { return p.time < cuts.front() - kEpsilon; })))
            continue;
        AutomationPoints points;
        for (size_t k = 0; k < plan.size(); ++k)
        {
            const auto& s = plan[k];
            const double land = lands[k];
            if (s.empty() && s.pasted && fromPiece != nullptr)
            {
                // The piece's own points, its edges pinned.
                for (const auto& p : fromPiece->points)
                    points.push_back({land + p.time, p.value, p.curve});
                continue;
            }
            if (s.empty())
            {
                // It holds the value where it was put in.
                const float v = automationValueAt(lane.points, s.from);
                points.push_back({land, v, 0.0f});
                points.push_back({land + s.emptyLength, v, 0.0f});
                continue;
            }
            const bool pointAtStart = std::any_of(lane.points.begin(), lane.points.end(), [&](const auto& p)
                                                  { return std::abs(p.time - s.from) <= kEpsilon; });
            if (!(k == 0 && s.from <= kEpsilon) && !pointAtStart)
                points.push_back(
                    {land, automationValueAt(lane.points, s.from), curveAt(lane.points, s.from)});
            for (const auto& p : lane.points)
                if (inside(p.time, s))
                    points.push_back({land + (p.time - s.from), p.value, p.curve});
            if (std::isfinite(s.to))
                points.push_back({land + (s.to - s.from), valueBefore(lane.points, s.to), 0.0f});
        }
        std::stable_sort(points.begin(), points.end(),
                         [](const auto& a, const auto& b) { return a.time < b.time; });
        if (points != lane.points)
            result.lanes.push_back({lane.id, std::move(points)});
    }

    // Sections, like clips; copies named on.
    std::vector<SectionInfo> copies, pasted;
    for (size_t k = 0; k < plan.size(); ++k)
    {
        if (plan[k].pasted && piece != nullptr)
        {
            for (auto s : piece->sections)
            {
                s.id = 0;
                s.start += lands[k];
                s.end += lands[k];
                pasted.push_back(s);
            }
        }
        else if (plan[k].empty() && newSection)
        {
            auto s = *newSection;
            s.id = 0;
            s.start = lands[k];
            s.end = lands[k] + plan[k].emptyLength;
            result.sections.push_back(s);
        }
    }
    // A section's edges land each on its own (time-selection.md §9): its start with the stretch it's
    // in, its end with the stretch just before it, so time taken out of it shortens it and time put
    // into it lengthens it; a time that's gone lands where its gap closed. For the sections' own
    // commands, whose cuts are at section edges, this moves each section whole.
    for (const auto& s : content.sections)
    {
        auto placed = s;
        placed.start = planEdge(plan, s.start, false);
        placed.end = planEdge(plan, s.end, true);
        if (placed.end - placed.start > kEpsilon)
            result.sections.push_back(placed);
        // Copies of the sections wholly inside a copied stretch.
        for (size_t k = 0; k < plan.size(); ++k)
            if (plan[k].copy && inside(s.start, plan[k]) && s.end <= plan[k].to + kEpsilon)
            {
                auto copy = s;
                copy.id = 0;
                copy.start = lands[k] + (s.start - plan[k].from);
                copy.end = copy.start + s.length();
                copies.push_back(copy);
            }
    }
    // Copies named after everything else that's there.
    for (auto& c : copies)
    {
        auto all = content.sections;
        all.insert(all.end(), result.sections.begin(), result.sections.end());
        c.name = copyName(all, c.name);
        result.sections.push_back(c);
    }
    // Pasted ones keep their names (after a cut, or from another song) unless they're taken.
    for (auto& p : pasted)
    {
        if (named(result.sections, p.name))
            p.name = copyName(result.sections, p.name);
        result.sections.push_back(p);
    }
    std::sort(result.sections.begin(), result.sections.end(),
              [](const auto& a, const auto& b) { return a.start < b.start; });
    return result;
}
} // namespace fd::model
