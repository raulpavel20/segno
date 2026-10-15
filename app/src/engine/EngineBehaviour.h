#pragma once

#include <functional>
#include <memory>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

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
/// - warped audio clips play through Segno's piece player (ADR 0063, the warped-clips patch);
/// - a MIDI clip's notes left to chance play or not, and their velocity spreads, the same in
///   playback, a bounce and an export (piano-roll.md §10, ClipNotes.h).
[[nodiscard]] std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour();

/// On an Edit's root: its muted tracks still feed the return tracks. Only render copies that
/// export a return track's stem set it (export.md §12): the tracks sending to it are muted there
/// so only the return is heard.
inline const juce::Identifier sendsWhileMutedId{"fdSendsWhileMuted"};

/// The file for a new audio recording on a track ("wav" or another extension); an empty file lets
/// tracktion choose. One per engine: the last one set wins.
using RecordingFiles = std::function<juce::File(tracktion::Track& track, const juce::String& extension)>;
void setRecordingFiles(tracktion::Engine& engine, RecordingFiles files);

/// A clip's transients (seconds of its file as recorded, audio-editor.md §7.4), where the piece
/// player starts Beats' pieces; none while they're being found. One per engine: the last one set
/// wins.
using TransientSource = std::function<std::vector<double>(tracktion::AudioClipBase& clip)>;
void setTransientSource(tracktion::Engine& engine, TransientSource source);
} // namespace fd::engine
