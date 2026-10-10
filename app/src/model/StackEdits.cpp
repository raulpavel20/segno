#include "model/StackEdits.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-9;

const ClipPlace* place(const std::vector<ClipPlace>& clips, std::uint64_t id)
{
    for (const auto& c : clips)
        if (c.clip == id)
            return &c;
    return nullptr;
}

/// The span of a stack's parts.
std::pair<double, double> partsSpan(const std::vector<ClipPlace>& clips, std::uint64_t stack)
{
    double from = 1e300, to = -1e300;
    for (const auto& c : clips)
        if (c.stack == stack)
        {
            from = std::min(from, c.start);
            to = std::max(to, c.end());
        }
    return {from, to};
}
} // namespace

GroupedEdits groupEdits(const std::vector<ClipPlace>& clips, const std::vector<ClipEdit>& edits)
{
    GroupedEdits out;
    std::map<std::uint64_t, size_t> moved; // stack -> index in out.moves
    std::set<std::uint64_t> edited;
    for (const auto& e : edits)
        edited.insert(e.clip);
    for (const auto& e : edits)
    {
        const auto* c = place(clips, e.clip);
        if (c == nullptr || c->stack == 0)
        {
            out.edits.push_back(e);
            continue;
        }
        const bool move = std::abs(e.length - c->length) < kEps && std::abs(e.offset - c->offset) < kEps;
        if (move)
        {
            auto it = moved.find(c->stack);
            if (it == moved.end())
            {
                it = moved.emplace(c->stack, out.moves.size()).first;
                out.moves.push_back({.stack = c->stack, .toTrack = e.track, .delta = e.start - c->start});
                // The stack's other parts follow, whether or not they were edited.
                for (const auto& other : clips)
                    if (other.stack == c->stack && other.clip != c->clip && !edited.contains(other.clip))
                        out.edits.push_back({other.clip, e.track, other.start + (e.start - c->start), other.length, other.offset});
            }
            // The first move of a stack wins for all of its parts.
            const auto& m = out.moves[it->second];
            out.edits.push_back({e.clip, m.toTrack, c->start + m.delta, c->length, c->offset});
            continue;
        }
        // A trim: the stack's range follows its outer edges.
        out.edits.push_back(e);
        const auto [from, to] = partsSpan(clips, c->stack);
        StackTrim trim{.stack = c->stack};
        if (std::abs(c->start - from) < kEps && std::abs(e.start - c->start) > kEps)
            trim.start = e.start;
        if (std::abs(c->end() - to) < kEps && std::abs((e.start + e.length) - c->end()) > kEps)
            trim.end = e.start + e.length;
        if (trim.start || trim.end)
            out.trims.push_back(trim);
    }
    // A move that goes nowhere moves nothing.
    std::erase_if(out.moves, [&](const StackMove& m) {
        if (std::abs(m.delta) > kEps)
            return false;
        for (const auto& c : clips)
            if (c.stack == m.stack)
                return c.track == m.toTrack;
        return true;
    });
    return out;
}

std::pair<std::vector<ClipEdit>, std::vector<StackCopy>> groupPlacements(const std::vector<ClipPlace>& clips,
                                                                         const std::vector<ClipEdit>& placements)
{
    std::vector<ClipEdit> edits;
    std::vector<StackCopy> copies;
    auto copied = [&](std::uint64_t stack, std::uint64_t track, double delta) {
        return std::any_of(copies.begin(), copies.end(), [&](const StackCopy& s) {
            return s.stack == stack && s.toTrack == track && std::abs(s.delta - delta) < kEps;
        });
    };
    std::set<std::pair<std::uint64_t, std::uint64_t>> placed; // (source, track)
    for (const auto& p : placements)
    {
        const auto* c = place(clips, p.clip);
        if (c == nullptr || c->stack == 0)
        {
            edits.push_back(p);
            continue;
        }
        const double delta = p.start - c->start;
        if (!placed.insert({p.clip, p.track}).second)
            continue;
        edits.push_back(p);
        if (copied(c->stack, p.track, delta))
            continue;
        copies.push_back({.stack = c->stack, .toTrack = p.track, .delta = delta});
        for (const auto& other : clips)
            if (other.stack == c->stack && other.clip != c->clip)
            {
                const bool asked = std::any_of(placements.begin(), placements.end(), [&](const ClipEdit& q) {
                    return q.clip == other.clip && q.track == p.track;
                });
                if (!asked && placed.insert({other.clip, p.track}).second)
                    edits.push_back({other.clip, p.track, other.start + delta, other.length, other.offset});
            }
    }
    return {edits, copies};
}

std::vector<std::uint64_t> withParts(const std::vector<ClipPlace>& clips, const std::vector<std::uint64_t>& ids)
{
    std::vector<std::uint64_t> out;
    std::set<std::uint64_t> seen;
    auto add = [&](std::uint64_t id) {
        if (seen.insert(id).second)
            out.push_back(id);
    };
    for (const auto id : ids)
    {
        add(id);
        if (const auto* c = place(clips, id); c != nullptr && c->stack != 0)
            for (const auto& other : clips)
                if (other.stack == c->stack)
                    add(other.clip);
    }
    return out;
}

SplitPlan splitPlan(const std::vector<ClipPlace>& clips, const std::vector<StackRange>& stacks,
                    const std::vector<std::uint64_t>& ids, double at)
{
    SplitPlan plan;
    std::set<std::uint64_t> stackSet;
    for (const auto id : withParts(clips, ids))
        if (const auto* c = place(clips, id))
        {
            if (c->start < at - kEps && c->end() > at + kEps)
                plan.clips.push_back(id);
            if (c->stack != 0)
                stackSet.insert(c->stack);
        }
    for (const auto& s : stacks)
        if (stackSet.contains(s.id) && s.start < at - kEps && s.end > at + kEps)
            plan.stacks.push_back(s.id);
    return plan;
}

StackSettle settleStacks(const std::vector<ClipPlace>& trackClips, const std::vector<StackRange>& stacks)
{
    StackSettle out;
    auto clips = trackClips;
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    std::set<std::uint64_t> known;
    for (const auto& s : stacks)
        known.insert(s.id);
    for (const auto& c : clips)
        if (c.stack != 0 && !known.contains(c.stack))
            out.plain.push_back(c.clip);
    for (const auto& s : stacks)
    {
        bool started = false, broken = false;
        double from = 1e300, to = -1e300;
        std::set<std::uint64_t> parts;
        for (const auto& c : clips)
        {
            if (c.stack != s.id)
            {
                broken = broken || started; // any other clip after its parts began
                continue;
            }
            if (broken)
            {
                out.plain.push_back(c.clip);
                continue;
            }
            started = true;
            parts.insert(c.clip);
            from = std::min(from, c.start);
            to = std::max(to, c.end());
        }
        if (!started)
        {
            out.remove.push_back(s.id);
            continue;
        }
        // Over its parts, and between the other clips around them.
        StackRange settled{s.id, std::min(s.start, from), std::max(s.end, to)};
        for (const auto& c : clips)
            if (!parts.contains(c.clip))
            {
                if (c.end() <= from + kEps)
                    settled.start = std::max(settled.start, c.end());
                else if (c.start >= to - kEps)
                    settled.end = std::min(settled.end, c.start);
            }
        if (!(settled == s))
            out.ranges.push_back(settled);
    }
    return out;
}

PassCover passCover(const std::vector<ClipPlace>& trackClips, double from, double to)
{
    constexpr double kSlack = 1e-3;
    auto clips = trackClips;
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    std::map<std::uint64_t, std::pair<double, double>> extent; // a stack's parts, from first to last
    for (const auto& c : clips)
        if (c.stack != 0)
        {
            auto [it, fresh] = extent.try_emplace(c.stack, c.start, c.end());
            it->second = {std::min(it->second.first, c.start), std::max(it->second.second, c.end())};
        }
    PassCover out;
    const ClipPlace* first = nullptr;
    bool oneTake = true;
    for (const auto& c : clips)
    {
        if (!(c.start < to - kSlack && c.end() > from + kSlack))
            continue;
        out.before.push_back(c.clip);
        first = first != nullptr ? first : &c;
        oneTake = oneTake && c.take != 0 && c.take == first->take;
        if (c.stack == 0 || std::find(out.join.begin(), out.join.end(), c.stack) != out.join.end())
            continue;
        if (const auto [a, b] = extent.at(c.stack); a >= from - kSlack && b <= to + kSlack)
            out.join.push_back(c.stack);
    }
    if (first != nullptr && oneTake && std::find(out.join.begin(), out.join.end(), first->stack) != out.join.end())
        out.before.clear(); // what played is a take the clip gets anyway
    return out;
}

ClipPlace trimPlace(const ClipPlace& clip, double from, double to)
{
    auto out = clip;
    const double start = std::max(clip.start, from), end = std::min(clip.end(), to);
    out.offset = clip.offset + (start - clip.start);
    out.start = start;
    out.length = std::max(0.0, end - start);
    return out;
}
} // namespace fd::model
