#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
/// Reduced latency when monitoring (recording.md §22.1): on each track played live, the modules
/// that add delay are left out (their power as set) and the track isn't delayed to line up with
/// the song's delay compensation. Kept with the project. The engine behaviour asks these as it
/// builds the playback graph (the reduced-monitoring-latency patch), and the Session as it shows
/// them, so what's shown is what's heard.
inline const juce::Identifier reducedLatencyId{"fdReducedLatency"};

/// Whether the project has it on.
[[nodiscard]] bool reducesLatency(const tracktion::Edit& edit);

/// A track is monitored while an input (not another track's output) plays live through it: armed
/// with Monitor on Auto, or Monitor In (model::hearsInput), as the graph was last built for.
[[nodiscard]] bool isMonitored(const tracktion::AudioTrack& track);

/// Monitored, with the project's reduced latency on.
[[nodiscard]] bool isMonitoredWithReducedLatency(const tracktion::AudioTrack& track);

/// Whether a monitored track leaves a module out: it's on, delays too much (a rack: holds a
/// module that does), and doesn't play the notes (leaving that out would silence the track).
/// Asked as the graph is built; what it decided is what leftOutWhileMonitoring
/// (EngineBehaviour.h) says.
[[nodiscard]] bool shouldBypassWhileMonitoring(tracktion::Plugin& module);

/// A module delays too much to keep while monitored from this many ms: a lookahead or a
/// linear-phase EQ does; oversampling's fraction of a millisecond is worth keeping (a Saturator's
/// sound). Studio One's limit too (recording.md §22.1).
inline constexpr double kBypassFromMs = 3.0;

/// A module delaying its track by kBypassFromMs or more (a rack: a module in it, on, that does).
[[nodiscard]] bool delaysTooMuch(tracktion::Plugin& module);
} // namespace fd::engine
