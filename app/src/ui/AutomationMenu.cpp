#include "ui/AutomationMenu.h"

namespace fd::app
{
void addAutomationItems(engine::Session& session, const engine::Session::ParamTarget& target, juce::PopupMenu& menu)
{
    const auto snapshot = session.snapshot();
    const auto [lane, assignment] = snapshot->assignmentOf(target.channel, target.key);
    if (lane != nullptr)
    {
        menu.addItem("Show automation lane", [&session, id = lane->id] { session.revealLane(id); });
        if (!lane->points.empty() && !assignment->on)
            menu.addItem("Turn automation back on", [&session, target] { session.setAssignmentOn(target, true); });
        menu.addItem("Remove from automation lane", [&session, target] { session.unassign(target); });
    }
    // Any lane can drive it; choosing another moves it there (one lane per parameter).
    juce::PopupMenu lanes;
    for (const auto& l : snapshot->automationLanes)
        lanes.addItem(juce::String::fromUTF8(l.displayName().c_str()), true, lane != nullptr && l.id == lane->id,
                      [&session, target, id = l.id] {
                          if (session.assign(id, target))
                              session.revealLane(id);
                      });
    if (lanes.getNumItems() == 0)
        lanes.addItem("No automation lanes yet", false, false, [] {});
    menu.addSubMenu("Assign to automation lane", lanes);
    menu.addItem("Automate in a new lane", [&session, target] {
        if (const auto id = session.automateInNewLane(target))
            session.revealLane(id);
    });
}
} // namespace fd::app
