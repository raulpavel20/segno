#include "model/Snapshot.h"

#include <algorithm>

namespace fd::model
{
std::string AutomationLaneInfo::displayName() const
{
    if (!name.empty())
        return name;
    if (assignments.empty())
        return "Automation lane";
    if (assignments.size() == 1)
        return assignments.front().name;
    return assignments.front().name + " + " + std::to_string(assignments.size() - 1);
}

LaneRules AutomationLaneInfo::rules() const noexcept
{
    if (assignments.size() == 1 && assignments.front().stepped() && assignments.front().range.full())
        return {.steps = assignments.front().steps};
    return {};
}

bool AutomationLaneInfo::allOn() const noexcept
{
    return std::all_of(assignments.begin(), assignments.end(), [](const auto& a) { return a.on; });
}

bool AutomationLaneInfo::anyOff() const noexcept
{
    return !allOn();
}

const AssignmentInfo* AutomationLaneInfo::assignment(std::uint64_t channel, const std::string& key) const noexcept
{
    for (const auto& a : assignments)
        if (a.channel == channel && a.key == key)
            return &a;
    return nullptr;
}
} // namespace fd::model
