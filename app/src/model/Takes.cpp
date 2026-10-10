#include "model/Takes.h"

#include "model/Editing.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>

namespace fd::model
{
std::vector<std::pair<double, double>> inUse(const ClipInfo& take, const std::vector<ClipInfo>& main)
{
    std::vector<std::pair<double, double>> out;
    for (const auto& c : main)
        if (c.fromTake == take.id)
        {
            const double from = std::max(c.start, take.start), to = std::min(c.end(), take.end());
            if (to > from)
                out.emplace_back(from, to);
        }
    std::sort(out.begin(), out.end());
    return out;
}

bool isUsed(const TakeInfo& take, const std::vector<ClipInfo>& main)
{
    return std::any_of(take.clips.begin(), take.clips.end(), [&](const auto& c) { return !inUse(c, main).empty(); });
}

std::optional<size_t> takeAt(const std::vector<TakeInfo>& takes, const std::vector<ClipInfo>& main, double seconds)
{
    for (size_t i = 0; i < takes.size(); ++i)
        for (const auto& c : takes[i].clips)
            for (const auto& [from, to] : inUse(c, main))
                if (seconds >= from && seconds < to)
                    return i;
    return std::nullopt;
}

std::optional<size_t> stepTake(const std::vector<TakeInfo>& takes, std::optional<size_t> current, double from, double to,
                               int direction)
{
    const auto n = takes.size();
    if (n == 0)
        return std::nullopt;
    auto covers = [&](const TakeInfo& t) {
        return std::any_of(t.clips.begin(), t.clips.end(), [&](const auto& c) { return c.start <= from + 1e-6 && c.end() >= to - 1e-6; });
    };
    const size_t start = current ? *current : (direction > 0 ? n - 1 : 0);
    for (size_t k = 1; k <= n; ++k)
    {
        const auto i = direction > 0 ? (start + k) % n : (start + n - k % n) % n;
        if (covers(takes[i]))
            return i;
    }
    return std::nullopt;
}

int nextTakeNumber(const std::vector<TakeInfo>& takes)
{
    int highest = 0;
    for (const auto& t : takes)
        if (t.name.rfind("Take ", 0) == 0)
            highest = std::max(highest, std::atoi(t.name.c_str() + 5));
    return highest + 1;
}

std::optional<size_t> playingTake(const TakeStackInfo& stack, const std::vector<ClipInfo>& trackClips)
{
    std::optional<size_t> playing;
    for (const auto& c : trackClips)
    {
        if (c.stack != stack.id)
            continue;
        std::optional<size_t> of;
        for (size_t i = 0; i < stack.takes.size() && !of; ++i)
            for (const auto& tc : stack.takes[i].clips)
                if (tc.id == c.fromTake)
                    of = i;
        if (!of || (playing && *playing != *of))
            return std::nullopt;
        playing = of;
    }
    return playing;
}

std::optional<Badge> badge(const TakeStackInfo& stack, const std::vector<ClipInfo>& trackClips, bool narrow)
{
    if (!hasTakesToShow(stack))
        return std::nullopt;
    Badge b;
    const auto playing = playingTake(stack, trackClips);
    b.comp = !playing.has_value();
    if (narrow)
        return b;
    if (playing)
    {
        b.text = std::to_string(*playing + 1) + "/" + std::to_string(stack.takes.size());
    }
    else
        b.text = "Comp";
    return b;
}

double takeStripsHeight(const TrackInfo& track, double strip)
{
    double h = 0.0;
    for (const auto& s : track.stacks)
        if (s.open && hasTakesToShow(s))
            h = std::max(h, static_cast<double>(s.takes.size()) * strip);
    return h;
}

std::optional<StripHit> hitStrip(const TrackInfo& track, double seconds, double below, double strip)
{
    if (below < 0.0 || strip <= 0.0)
        return std::nullopt;
    for (const auto& s : track.stacks)
    {
        if (!s.open || !hasTakesToShow(s) || seconds < s.start || seconds >= s.end)
            continue;
        const auto row = static_cast<size_t>(std::floor(below / strip));
        if (row >= s.takes.size())
            return std::nullopt;
        const auto& take = s.takes[s.takes.size() - 1 - row]; // newest on top
        StripHit hit{.stack = s.id, .take = take.id};
        for (const auto& c : take.clips)
            if (seconds >= c.start && seconds < c.end())
                hit.onClip = true;
        return hit;
    }
    return std::nullopt;
}

std::vector<ClipGroup> clipGroups(const TrackInfo& track)
{
    std::vector<ClipGroup> out;
    std::map<std::uint64_t, size_t> byStack;
    for (const auto& c : track.clips) // sorted by start
    {
        const auto* stack = c.stack != 0 ? track.stack(c.stack) : nullptr;
        if (stack != nullptr)
            if (const auto it = byStack.find(stack->id); it != byStack.end())
            {
                out[it->second].parts.push_back(&c);
                continue;
            }
        out.push_back({.parts = {&c}, .stack = stack});
        if (stack != nullptr)
            byStack[stack->id] = out.size() - 1;
    }
    return out;
}

std::vector<std::uint64_t> clipGroup(const Snapshot& snapshot, std::uint64_t clip)
{
    const auto found = findClip(snapshot, clip);
    if (!found)
        return {};
    if (found->first->stack == 0)
        return {clip};
    std::vector<std::uint64_t> out;
    for (const auto& c : snapshot.tracks[static_cast<size_t>(found->second)].clips)
        if (c.stack == found->first->stack)
            out.push_back(c.id);
    return out;
}

std::set<std::uint64_t> withGroups(const Snapshot& snapshot, std::set<std::uint64_t> clips)
{
    auto out = clips;
    for (const auto id : clips)
        for (const auto part : clipGroup(snapshot, id))
            out.insert(part);
    return out;
}

bool isInnerEdge(const TrackInfo& track, const ClipInfo& clip, bool startEdge)
{
    if (clip.stack == 0)
        return false;
    for (const auto& c : track.clips)
        if (c.id != clip.id && c.stack == clip.stack && (startEdge ? c.start < clip.start : c.start > clip.start))
            return true;
    return false;
}

std::optional<size_t> replacementTake(const TakeStackInfo& stack, const std::set<std::uint64_t>& deleted, double from, double to)
{
    std::optional<size_t> overlapping;
    for (size_t k = stack.takes.size(); k-- > 0;)
    {
        const auto& t = stack.takes[k];
        if (deleted.contains(t.id))
            continue;
        for (const auto& c : t.clips)
        {
            if (c.start <= from + 1e-6 && c.end() >= to - 1e-6)
                return k;
            if (!overlapping && c.start < to && c.end() > from)
                overlapping = k;
        }
    }
    return overlapping;
}

std::vector<SwipePiece> swipePieces(const TakeStackInfo& stack, std::uint64_t take, double from, double to)
{
    constexpr double kShortest = 1e-3; // a sliver of a take is no stretch of it
    std::vector<SwipePiece> out;
    const auto* t = stack.take(take);
    if (t == nullptr)
        return out;
    const double a = std::max(std::min(from, to), stack.start), b = std::min(std::max(from, to), stack.end);
    for (const auto& c : t->clips)
    {
        const double x = std::max(a, c.start), y = std::min(b, c.end());
        if (y - x >= kShortest)
            out.push_back({c.id, x, y});
    }
    return out;
}

std::optional<CompJoin> compJoinAt(const TrackInfo& track, double seconds, double tolerance)
{
    std::optional<CompJoin> best;
    for (const auto& stack : track.stacks)
    {
        std::vector<const ClipInfo*> parts;
        for (const auto& c : track.clips)
            if (c.stack == stack.id)
                parts.push_back(&c);
        std::sort(parts.begin(), parts.end(), [](auto* a, auto* b) { return a->start < b->start; });
        auto takeOf = [&](const ClipInfo& part) -> std::uint64_t {
            for (const auto& k : stack.takes)
                for (const auto& c : k.clips)
                    if (c.id == part.fromTake)
                        return k.id;
            return 0;
        };
        for (size_t i = 0; i + 1 < parts.size(); ++i)
        {
            const auto& a = *parts[i];
            const auto& b = *parts[i + 1];
            const double d = std::abs(a.end() - seconds);
            if (std::abs(a.end() - b.start) > 1e-6 || d > tolerance || (best && d >= std::abs(best->at - seconds)))
                continue;
            best = CompJoin{.stack = stack.id, .left = a.id, .right = b.id, .leftTake = takeOf(a), .rightTake = takeOf(b),
                            .at = a.end(), .from = a.start, .to = b.end()};
        }
    }
    return best;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> seamlessParts(std::vector<ClipInfo> parts)
{
    constexpr double kNear = 1e-6;
    std::sort(parts.begin(), parts.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    std::vector<std::pair<std::uint64_t, std::uint64_t>> out;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
    {
        const auto& a = parts[i];
        const auto& b = parts[i + 1];
        if (a.fromTake != 0 && a.fromTake == b.fromTake && std::abs(a.end() - b.start) < kNear &&
            std::abs(a.offset + a.length - b.offset) < kNear)
            out.emplace_back(a.id, b.id);
    }
    return out;
}

std::vector<std::vector<size_t>> overlapGroups(const std::vector<std::pair<double, double>>& spans)
{
    // Union-find over overlapping pairs.
    std::vector<size_t> parent(spans.size());
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = i;
    std::function<size_t(size_t)> root = [&](size_t i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
    for (size_t a = 0; a < spans.size(); ++a)
        for (size_t b = a + 1; b < spans.size(); ++b)
            if (spans[a].first < spans[b].second && spans[b].first < spans[a].second)
                parent[root(b)] = root(a);
    std::map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < spans.size(); ++i)
        groups[root(i)].push_back(i);
    std::vector<std::vector<size_t>> out;
    for (auto& [r, g] : groups)
        out.push_back(std::move(g));
    std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.front() < y.front(); });
    return out;
}
} // namespace fd::model
