#include "model/RowTree.h"

#include <algorithm>
#include <map>
#include <set>

namespace fd::model
{
namespace
{
/// Whether `group` is `ancestor` or inside it, by the groups' parents.
bool within(const std::map<std::uint64_t, std::uint64_t>& parents, std::uint64_t group,
            std::uint64_t ancestor)
{
    for (std::size_t guard = 0; group != 0 && guard <= parents.size(); ++guard)
    {
        if (group == ancestor)
            return true;
        const auto it = parents.find(group);
        group = it != parents.end() ? it->second : 0;
    }
    return ancestor == 0;
}

/// The tree the rows make: each channel in its group, in order; each lane after the nearest track
/// or group above it in its own group (0: none there before it).
RowTree treeOf(const std::vector<TreeRow>& rows)
{
    RowTree tree;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        if (!r.lane)
        {
            tree.channels.push_back({.id = r.id, .group = r.group, .isGroup = r.isGroup});
            continue;
        }
        std::uint64_t after = 0;
        for (std::size_t k = i; k-- > 0;)
        {
            if (!rows[k].lane && rows[k].id == r.group)
                break; // its group's own row: it's first there
            if (!rows[k].lane && rows[k].group == r.group)
            {
                after = rows[k].id;
                break;
            }
        }
        tree.lanes.push_back({.id = r.id, .after = after, .group = r.group});
    }
    return tree;
}
} // namespace

std::vector<TreeRow> treeRows(const RowTree& tree)
{
    std::vector<TreeRow> out;
    std::set<std::uint64_t> groups;
    std::set<std::pair<std::uint64_t, std::uint64_t>> places; // (group, channel) as they are
    for (const auto& c : tree.channels)
    {
        if (c.isGroup)
            groups.insert(c.id);
        places.insert({c.group, c.id});
    }
    std::set<std::uint64_t> placed;
    const auto lanes = [&](std::uint64_t group, int depth, auto&& which)
    {
        for (const auto& l : tree.lanes)
            if (l.group == group && which(l) && placed.insert(l.id).second)
                out.push_back({.id = l.id, .group = group, .lane = true, .depth = depth});
    };
    const auto rowsIn = [&](std::uint64_t group, int depth, auto&& self) -> void
    {
        lanes(group, depth, [](const RowTree::Lane& l) { return l.after == 0; });
        for (const auto& c : tree.channels)
        {
            if (c.group != group)
                continue;
            out.push_back({.id = c.id, .group = group, .isGroup = c.isGroup, .depth = depth});
            if (c.isGroup && depth < static_cast<int>(tree.channels.size())) // bounded against loops
                self(c.id, depth + 1, self);
            lanes(group, depth, [&c](const RowTree::Lane& l) { return l.after == c.id; });
        }
        // After a row that isn't here: last in its group.
        lanes(group, depth, [&](const RowTree::Lane& l) { return !places.contains({group, l.after}); });
    };
    rowsIn(0, 0, rowsIn);
    // In a group that's gone: last of all.
    for (const auto& l : tree.lanes)
        if (placed.insert(l.id).second)
            out.push_back({.id = l.id, .group = 0, .lane = true, .depth = 0});
    return out;
}

RowTree withoutRow(const RowTree& tree, std::uint64_t row)
{
    auto rows = treeRows(tree);
    std::map<std::uint64_t, std::uint64_t> parents;
    for (const auto& c : tree.channels)
        if (c.isGroup)
            parents[c.id] = c.group;
    const auto gone =
        std::find_if(rows.begin(), rows.end(), [row](const TreeRow& r) { return !r.lane && r.id == row; });
    if (gone == rows.end())
        return tree;
    const auto in = gone->group;
    const bool group = gone->isGroup;
    std::vector<TreeRow> rest;
    for (auto it = rows.begin(); it != rows.end(); ++it)
    {
        const bool inside = it > gone && group && within(parents, it->group, row);
        if (it == gone || (inside && !it->lane))
            continue;
        auto kept = *it;
        if (inside) // a lane that was in it: in its group now
            kept.group = in;
        rest.push_back(kept);
    }
    return treeOf(rest);
}

RowTree groupInTree(const RowTree& tree, const std::vector<std::uint64_t>& chosen, std::uint64_t group)
{
    const auto rows = treeRows(tree);
    std::map<std::uint64_t, std::uint64_t> parents;
    for (const auto& c : tree.channels)
        if (c.isGroup)
            parents[c.id] = c.group;
    // What moves: each chosen row with the rows inside it, and the lanes between two of those.
    std::vector<char> taken(rows.size(), 0); // 1: moves in as it is; 2: moves in, into the group
    std::uint64_t block = 0;                 // the chosen group whose rows these are
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        if (block != 0 && within(parents, r.group, block))
            taken[i] = 1;
        else if (!r.lane && std::find(chosen.begin(), chosen.end(), r.id) != chosen.end())
        {
            taken[i] = 2;
            block = r.isGroup ? r.id : 0;
        }
        else
            block = 0;
    }
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (!taken[i])
            continue;
        bool lanesOnly = last.has_value();
        for (auto k = last.value_or(i) + 1; k < i && lanesOnly; ++k)
            lanesOnly = rows[k].lane;
        if (lanesOnly)
            for (auto k = *last + 1; k < i; ++k)
                taken[k] = 2;
        last = i;
    }
    const auto first = std::find_if(taken.begin(), taken.end(), [](char t) { return t != 0; });
    if (first == taken.end())
        return tree;
    const auto at = static_cast<std::size_t>(first - taken.begin());

    std::vector<TreeRow> out(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(at));
    out.push_back({.id = group, .group = rows[at].group, .isGroup = true});
    for (std::size_t i = at; i < rows.size(); ++i)
        if (taken[i])
        {
            auto r = rows[i];
            if (taken[i] == 2)
                r.group = group;
            out.push_back(r);
        }
    for (std::size_t i = at; i < rows.size(); ++i)
        if (!taken[i])
            out.push_back(rows[i]);
    return treeOf(out);
}

std::optional<RowMove> moveInTree(const RowTree& tree, std::uint64_t row, std::uint64_t before,
                                  std::optional<std::uint64_t> asked)
{
    const auto rows = treeRows(tree);
    std::map<std::uint64_t, std::uint64_t> parents; // each group's own group
    for (const auto& c : tree.channels)
        if (c.isGroup)
            parents[c.id] = c.group;
    const auto find = [&](std::uint64_t id)
    { return std::find_if(rows.begin(), rows.end(), [id](const TreeRow& r) { return r.id == id; }); };
    const auto moved = find(row);
    if (moved == rows.end() || before == row || (before != 0 && find(before) == rows.end()))
        return std::nullopt;
    // A group moves with its rows: those right after it, inside it.
    auto end = std::next(moved);
    if (moved->isGroup)
        while (end != rows.end() && within(parents, end->group, moved->id))
            ++end;
    std::vector<TreeRow> block(moved, end), rest(rows.begin(), moved);
    rest.insert(rest.end(), end, rows.end());

    const std::uint64_t into = asked ? *asked : before != 0 ? find(before)->group : 0;
    if (into != 0 && !parents.contains(into))
        return std::nullopt;
    if (moved->isGroup && within(parents, into, moved->id))
        return std::nullopt; // inside itself
    if (before != 0 &&
        std::none_of(rest.begin(), rest.end(), [before](const TreeRow& r) { return r.id == before; }))
        return std::nullopt; // onto its own rows

    // Right above `before` when it's in `into`; else at the end of `into`'s rows.
    auto at = rest.end();
    if (const auto b =
            std::find_if(rest.begin(), rest.end(), [before](const TreeRow& r) { return r.id == before; });
        before != 0 && b != rest.end() && b->group == into)
        at = b;
    else if (into != 0)
    {
        at = std::find_if(rest.begin(), rest.end(),
                          [into](const TreeRow& r) { return !r.lane && r.id == into; });
        if (at == rest.end())
            return std::nullopt;
        ++at;
        while (at != rest.end() && within(parents, at->group, into))
            ++at;
    }
    block.front().group = into;
    const auto offset = at - rest.begin();
    rest.insert(at, block.begin(), block.end());

    RowMove out;
    out.tree = treeOf(rest);
    if (out.tree == treeOf(rows))
        return std::nullopt; // where it was
    out.group = into;
    for (auto k = offset; k-- > 0;)
    {
        const auto& r = rest[static_cast<std::size_t>(k)];
        if (!r.lane && r.id == into)
            break;
        if (!r.lane && r.group == into)
        {
            out.after = r.id;
            break;
        }
    }
    return out;
}
} // namespace fd::model
