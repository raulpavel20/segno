#include "model/TrackEdits.h"

#include "model/Racks.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>

namespace fd::model
{
std::string nextCopyName(const std::string& name, const std::vector<std::string>& taken)
{
    // "Bass 2": its base and number; "Bass": the base, and copies from 2.
    std::string base = name;
    int n = 2;
    if (const auto space = name.find_last_of(' ');
        space != std::string::npos && space + 1 < name.size() &&
        std::all_of(name.begin() + static_cast<std::ptrdiff_t>(space) + 1, name.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; }) &&
        name.size() - space - 1 < 9)
    {
        base = name.substr(0, space);
        n = std::stoi(name.substr(space + 1)) + 1;
    }
    auto candidate = [&] { return base + " " + std::to_string(n); };
    while (std::find(taken.begin(), taken.end(), candidate()) != taken.end())
        ++n;
    return candidate();
}

namespace
{
/// A positive number written plainly ("12"; not "012", not too long to hold).
std::optional<int> number(const std::string& text)
{
    if (text.empty() || text.size() > 6 || text.front() == '0' ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return std::nullopt;
    return std::stoi(text);
}

/// Return letters: A is 0, Z 25, AA 26 (and back).
std::optional<int> letterIndex(const std::string& text)
{
    if (text.empty() || text.size() > 4 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
        return std::nullopt;
    int index = 0;
    for (const char c : text)
        index = index * 26 + (c - 'A' + 1);
    return index - 1;
}

std::string letters(int index)
{
    std::string out;
    for (int i = index; i >= 0; i = i / 26 - 1)
        out.insert(out.begin(), static_cast<char>('A' + i % 26));
    return out;
}

/// The highest `read` gives for the part of a name around `prefix` and `suffix` (-1: none).
template <typename Read>
int highest(const std::vector<std::string>& names, const std::string& prefix, const std::string& suffix, Read read)
{
    int top = -1;
    for (const auto& n : names)
        if (n.size() > prefix.size() + suffix.size() && n.starts_with(prefix) && n.ends_with(suffix))
            if (const auto value = read(n.substr(prefix.size(), n.size() - prefix.size() - suffix.size())))
                top = std::max(top, *value);
    return top;
}
} // namespace

std::string nextTrackName(const std::vector<std::string>& names)
{
    return std::to_string(std::max(highest(names, "", " Track", number), 0) + 1) + " Track";
}

std::string nextGroupName(const std::vector<std::string>& names)
{
    return std::to_string(std::max(highest(names, "", " Group", number), 0) + 1) + " Group";
}

std::string nextReturnName(const std::vector<std::string>& names)
{
    return letters(highest(names, "", " Return", letterIndex) + 1) + " Return";
}

std::string nextLaneName(const std::vector<std::string>& names)
{
    return "Automation " + std::to_string(std::max(highest(names, "Automation ", "", number), 0) + 1);
}

std::string uniqueName(const std::string& name, const std::vector<std::string>& taken)
{
    return std::find(taken.begin(), taken.end(), name) == taken.end() ? name : nextCopyName(name, taken);
}

std::string laneCopyName(const std::string& name, const std::vector<std::string>& taken)
{
    return highest({name}, "Automation ", "", number) >= 0 ? nextLaneName(taken) : nextCopyName(name, taken);
}

namespace
{
/// The row of the group rows[i] is in, at `depth` - 1 (the nearest row above it that shallow):
/// rows.size() when none.
std::size_t groupRowAt(const std::vector<ArrangementRow>& rows, std::size_t i, int depth)
{
    if (depth <= 0)
        return rows.size();
    for (auto k = i + 1; k-- > 0;)
        if (rows[k].depth <= depth - 1)
            return rows[k].isGroup() && rows[k].depth == depth - 1 ? k : rows.size();
    return rows.size();
}
} // namespace

RowDrop dropRow(const std::vector<ArrangementRow>& rows, std::size_t dragged, std::size_t boundary, int depth,
                std::optional<std::size_t> onto)
{
    RowDrop d;
    if (dragged >= rows.size())
        return d;
    boundary = std::min(boundary, rows.size());
    const auto mix = static_cast<std::size_t>(firstMixRow(rows)); // the tracks and lanes end there
    switch (rows[dragged].kind)
    {
    case ArrangementRow::Kind::Track:
    case ArrangementRow::Kind::Group:
    case ArrangementRow::Kind::Lane:
    {
        // What moves: a group with its rows.
        auto end = dragged + 1;
        if (rows[dragged].isGroup())
            while (end < mix && rows[end].depth > rows[dragged].depth)
                ++end;
        const auto groupOf = [&](std::size_t row) -> std::uint64_t
        {
            if (row == 0)
                return 0;
            const auto at = groupRowAt(rows, row - 1, rows[row].depth);
            return at < rows.size() ? rows[at].channel : 0;
        };
        const auto here = groupOf(dragged);
        const auto id = [&](std::size_t r) { return rows[r].isLane() ? rows[r].lane : rows[r].channel; };
        if (onto && *onto < mix && rows[*onto].isGroup())
        {
            // On a group's header: at its end, its row lit.
            auto last = *onto + 1;
            while (last < mix && rows[last].depth > rows[*onto].depth)
                ++last;
            d.boundary = last;
            d.onHeader = true;
            d.group = rows[*onto].channel;
            d.depth = rows[*onto].depth + 1;
            d.moves = (*onto < dragged || *onto >= end) && !(here == d.group && end == last);
            return d;
        }
        const auto b = std::min(boundary, mix);
        d.boundary = b;
        // The depths it can land at: the row below's, up to the row above's (inside a folded
        // group above, one more), never inside itself.
        const int below = b < mix ? rows[b].depth : 0;
        int deepest = below;
        if (b > 0)
        {
            const auto& above = rows[b - 1];
            deepest = std::max(below, above.depth + (above.isGroup() ? 1 : 0));
            if (b - 1 >= dragged && b - 1 < end)
                deepest = std::min(deepest, std::max(below, rows[dragged].depth));
        }
        d.depth = std::clamp(depth, below, deepest);
        if (d.depth > 0 && b > 0)
            if (const auto g = groupRowAt(rows, b - 1, d.depth); g < rows.size())
                d.group = rows[g].channel;
        if (b < mix && rows[b].depth == d.depth)
            d.before = id(b);
        const bool inside = b > dragged && b < end;
        const bool inPlace = (b == dragged || b == end) && d.group == here;
        d.moves = !inside && !inPlace;
        break;
    }
    case ArrangementRow::Kind::Return:
    {
        auto last = mix;
        while (last < rows.size() && rows[last].kind == ArrangementRow::Kind::Return)
            ++last;
        const auto b = std::clamp(boundary, mix, last);
        d.boundary = b;
        d.before = b < last ? rows[b].channel : 0;
        d.moves = b != dragged && b != dragged + 1;
        break;
    }
    }
    return d;
}

std::pair<std::vector<PastePlace>, int> pastePlacement(const std::vector<PasteItem>& items,
                                                       std::size_t trackCount, std::size_t from, double at)
{
    std::vector<PastePlace> out;
    std::set<int> leftOut;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const auto track = static_cast<long long>(from) + item.row;
        if (item.row < 0 || track >= static_cast<long long>(trackCount))
        {
            leftOut.insert(item.row);
            continue;
        }
        out.push_back({i, static_cast<std::size_t>(track), at + item.offset});
    }
    return {out, static_cast<int>(leftOut.size())};
}

DeletionPlan deletionPlan(const Snapshot& snapshot, std::uint64_t channel)
{
    DeletionPlan plan;
    const auto* deleted = snapshot.channel(channel);
    if (deleted == nullptr || deleted->role == TrackInfo::Role::Master)
        return plan;
    const bool isReturn = deleted->role == TrackInfo::Role::Return;
    const auto sendKey = "send/" + std::to_string(channel);
    for (const auto& lane : snapshot.automationLanes)
    {
        std::vector<DeletionPlan::Assignment> going;
        for (const auto& a : lane.assignments)
            if (a.channel == channel || (isReturn && a.key == sendKey))
                going.push_back({lane.id, a.channel, a.key});
        if (!going.empty() && going.size() == lane.assignments.size())
        {
            plan.lanes.push_back(lane.id); // it drove only what goes
            continue;
        }
        plan.assignments.insert(plan.assignments.end(), going.begin(), going.end());
    }

    auto sidechainsIn = [&](const TrackInfo& t)
    {
        forEachDevice(t.devices, [&](const DeviceSlot& d) { // modules in racks too
            if (d.sidechainSource == channel)
                plan.sidechains.push_back(d.id);
        });
    };
    for (const auto& t : snapshot.tracks)
        sidechainsIn(t);
    for (const auto& t : snapshot.returns)
        sidechainsIn(t);
    sidechainsIn(snapshot.master);

    if (isReturn)
        for (const auto& t : snapshot.tracks)
            for (const auto& s : t.mix.sends)
                if (s.returnId == channel)
                {
                    plan.sends.push_back(t.id);
                    break;
                }
    return plan;
}

std::vector<std::uint64_t> lanesOnlyFor(const Snapshot& snapshot, std::uint64_t channel)
{
    std::vector<std::uint64_t> out;
    for (const auto& lane : snapshot.automationLanes)
        if (!lane.assignments.empty() &&
            std::all_of(lane.assignments.begin(), lane.assignments.end(),
                        [channel](const auto& a) { return a.channel == channel; }))
            out.push_back(lane.id);
    return out;
}
} // namespace fd::model
