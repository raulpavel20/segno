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

RowDrop dropRow(const std::vector<ArrangementRow>& rows, std::size_t dragged, std::size_t boundary)
{
    RowDrop d;
    if (dragged >= rows.size())
        return d;
    boundary = std::min(boundary, rows.size());
    const auto mix = static_cast<std::size_t>(firstMixRow(rows)); // the tracks and lanes end there
    switch (rows[dragged].kind)
    {
    case ArrangementRow::Kind::Track:
    case ArrangementRow::Kind::Lane:
    {
        const auto b = std::min(boundary, mix);
        d.boundary = b;
        if (b < mix)
            d.before = rows[b].isLane() ? rows[b].lane : rows[b].channel;
        d.moves = b != dragged && b != dragged + 1;
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

std::optional<RowOrder> moveRow(const std::vector<RowRef>& rows, std::uint64_t row, std::uint64_t before)
{
    auto find = [](const std::vector<RowRef>& in, std::uint64_t id)
    { return std::find_if(in.begin(), in.end(), [id](const RowRef& r) { return r.id == id; }); };
    const auto moved = find(rows, row);
    if (moved == rows.end() || before == row || (before != 0 && find(rows, before) == rows.end()))
        return std::nullopt;
    // Right above the row after it, or last while it's last: where it is.
    if (const auto next = std::next(moved); (next == rows.end() ? 0 : next->id) == before)
        return std::nullopt;
    auto order = rows;
    const auto dragged = *moved;
    order.erase(find(order, row));
    order.insert(before == 0 ? order.end() : find(order, before), dragged);
    RowOrder out;
    std::uint64_t above = 0; // the track above: 0 at the top
    for (const auto& r : order)
        if (r.lane)
            out.lanes.emplace_back(r.id, above);
        else
        {
            out.tracks.push_back(r.id);
            above = r.id;
        }
    return out;
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
    // Lanes after a deleted track sit after the track above it then (or at the top).
    std::uint64_t above = 0;
    for (std::size_t i = 0; i < snapshot.tracks.size(); ++i)
        if (snapshot.tracks[i].id == channel && i > 0)
            above = snapshot.tracks[i - 1].id;

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
        if (!isReturn && lane.after == channel)
            plan.reanchor.emplace_back(lane.id, above);
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
