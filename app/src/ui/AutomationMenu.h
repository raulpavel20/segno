#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// The automation items of an automatable control's right-click menu (docs/design/automation.md
/// §6): on a lane, Show automation lane, Turn automation back on (when off) and Remove from
/// automation lane; always, Assign to automation lane ▸ (its own ticked) and Automate in a new
/// lane. The same for module panels (through Session::automationMenu) and the mixer's controls.
void addAutomationItems(engine::Session& session, const engine::Session::ParamTarget& target, juce::PopupMenu& menu);

/// MIDI learn's items (docs/design/midi-learn.md §3, §4): Learn MIDI (the control waits for the
/// next message), and Remove MIDI mapping while it has one ("Remove MIDI mapping (CC 21)").
/// After the automation items, on every control that can be mapped.
void addMidiItems(engine::Session& session, const model::MidiTarget& target, juce::PopupMenu& menu);
} // namespace fd::app
