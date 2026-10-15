#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>

namespace fd::engine
{
/// How tracktion behaves in Segno. Every tracktion::Engine is made with it (the app and the
/// tests), so mixing and recording work the same everywhere:
/// - a track that isn't heard (muted, or silenced by another's solo) sends nothing to the
///   return tracks, even when it keeps running to feed a sidechain (ADR 0025, ADR 0026);
/// - a new audio recording's file is named by whoever set `setRecordingFiles` (the Session: its
///   project's Recordings folder, ADR 0030);
/// - audio clips play their fades and crossfades through Segno's fade node, and a crossfade's
///   spare audio past their edges (ADR 0036, the clip-playback patch);
/// - a MIDI clip's notes left to chance play or not, and their velocity spreads, rolled again
///   each pass and each lap of the loop, or as kept: the same in playback, a bounce and an export
///   then (piano-roll.md §26, ClipChance.h);
/// - with the project's reduced latency when monitoring on, a monitored track skips the song's
///   delay compensation and leaves out its modules that add delay (recording.md §22.1,
///   MonitoringLatency.h);
/// - with `scanPluginsOutOfProcess`, plugins are scanned in a child process, so one that crashes
///   takes only that process down (M3 C5, PluginScan.h). The app sets it; tests that don't scan
///   leave it off, as their binary can't serve as the child.
struct EngineOptions
{
    bool scanPluginsOutOfProcess = false;
};
[[nodiscard]] std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour(EngineOptions options = {});

/// On an Edit's root: its muted tracks still feed the return tracks. Only render copies that
/// export a return track's stem set it (export.md §12): the tracks sending to it are muted there
/// so only the return is heard.
inline const juce::Identifier sendsWhileMutedId{"fdSendsWhileMuted"};

/// Whether the playback graph, as last built, left a module out of its monitored track's chain
/// (reduced latency when monitoring, MonitoringLatency.h): what its header shows, so it says
/// what's heard.
[[nodiscard]] bool leftOutWhileMonitoring(tracktion::Plugin& module);

/// The file for a new audio recording on a track ("wav" or another extension); an empty file lets
/// tracktion choose. One per engine: the last one set wins.
using RecordingFiles = std::function<juce::File(tracktion::Track& track, const juce::String& extension)>;
void setRecordingFiles(tracktion::Engine& engine, RecordingFiles files);
} // namespace fd::engine
