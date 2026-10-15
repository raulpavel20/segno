#pragma once

#include "model/NoteToolkit.h"
#include "model/Warp.h"

#include <string_view>
#include <vector>

namespace fd::model
{
/// The audio editor's tools (docs/design/audio-editor.md §11): each described once, as data, with
/// the note tools' types, so the Tools pane builds itself from the description and assistants check
/// their arguments against it. Their runs are the Session's (they change clips, not notes). Normalise
/// came with P6a, Quantise and Tempo with P6b; Slice comes with P6c.
[[nodiscard]] const std::vector<ToolSpec>& audioTools();
[[nodiscard]] const ToolSpec* findAudioTool(std::string_view id) noexcept;

/// Normalise's settings as the Session takes them (§6.4).
struct NormaliseChoice
{
    bool loudness = false; // else peak
    double targetDb = -1.0;
    bool together = false;
};
[[nodiscard]] NormaliseChoice normaliseChoice(const ToolValues& values);

/// Quantise's settings as the warp model takes them (§7.7), at a grid step in beats; and the
/// sensitivity it shows (the clip's, 0 … 1).
[[nodiscard]] WarpQuantise warpQuantiseChoice(const ToolValues& values, double gridBeats);
[[nodiscard]] double quantiseSensitivity(const ToolValues& values);

/// The Tempo tool's settings (§7.8): the tempo, the downbeat (seconds of the file), and what Apply
/// does: warp the clip at it, or set the song's tempo from it.
struct TempoChoice
{
    double bpm = 120.0;
    double downbeat = 0.0;
    bool songTempo = false;
};
[[nodiscard]] TempoChoice tempoChoice(const ToolValues& values);
} // namespace fd::model
