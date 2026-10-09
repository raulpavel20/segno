#pragma once

namespace fd::ui
{
/// Whether a control's parameter is automated (docs/design/automation.md §6): its lane plays
/// (On), or the parameter keeps the value set by hand (Off).
enum class AutomationState
{
    None,
    On,
    Off
};

/// The automation indicator: a 4 px disc in the automation colour centred on `centre`, filled
/// while the lane plays and a ring while it's off (the shape tells them apart, not only colour).
void paintAutomationIndicator(juce::Graphics& g, juce::Point<float> centre, AutomationState state);
} // namespace fd::ui
