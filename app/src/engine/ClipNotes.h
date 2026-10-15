#pragma once

#include "model/Snapshot.h"

#include <optional>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::engine
{
// A MIDI clip's notes and controllers in the engine (M3 P4b, docs/design/piano-roll.md §10, §11):
// Segno's state on tracktion's notes and controller events, and the notes and lanes as the model
// sees them. Chance is decided as the notes play (ClipChance.h).

namespace noteIds
{
/// On a note: its chance and velocity spread (absent: always, as written). Its release velocity is
/// tracktion's own (`lift`).
inline const juce::Identifier chance{"fdChance"}, spread{"fdSpread"};
/// On a MIDI clip: what its notes' chance is decided from (absent: 0).
inline const juce::Identifier seed{"fdChanceSeed"};
/// On a controller event: one of a lane's points (and its curve), or one of the ramp between two.
/// Neither: played in (recorded), a point that holds.
inline const juce::Identifier point{"fdPoint"}, curve{"fdCurve"}, ramp{"fdRamp"};
} // namespace noteIds

/// A tracktion note as the model sees it.
[[nodiscard]] model::Note noteOf(const tracktion::MidiNote& note);
/// Writes what the model keeps beyond tracktion's own (chance, spread, release velocity).
void writeNoteExtras(tracktion::MidiNote& note, const model::Note& from, juce::UndoManager* undo);

/// The clip's controller lanes, by controller (model::controllers), only those with events.
[[nodiscard]] std::vector<model::ControllerLane> controllerLanesOf(const tracktion::MidiClip& clip);
/// Replaces each lane's controller's events with the lane's (an empty lane removes them).
void writeControllerLanes(tracktion::MidiClip& clip, const std::vector<model::ControllerLane>& lanes,
                          juce::UndoManager* undo);

[[nodiscard]] std::uint32_t chanceSeedOf(const tracktion::MidiClip& clip);

} // namespace fd::engine
