#include "model/Takes.h"

#include <algorithm>
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
} // namespace fd::model
