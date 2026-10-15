#pragma once

#include "model/NoteToolkit.h"

#include <string_view>
#include <vector>

namespace fd::model
{
/// The audio editor's tools (docs/design/audio-editor.md §11): each described once, as data, with
/// the note tools' types, so the Tools pane builds itself from the description and assistants check
/// their arguments against it. Their runs are the Session's (they change clips, not notes). Normalise
/// comes with P6a; Quantise, Tempo and Slice with their stages.
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
} // namespace fd::model
