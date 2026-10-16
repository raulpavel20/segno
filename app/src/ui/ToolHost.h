#pragma once

#include "model/NoteToolkit.h"

#include <cstdint>
#include <juce_core/juce_core.h>
#include <string>
#include <utility>
#include <vector>

namespace fd::app
{
/// What the clip view's Tools pane works on (piano-roll.md §25, audio-editor.md §11): the piano
/// roll's notes, or the audio clips the audio editor acts on. The pane shows a tool's tile and its
/// settings (ToolForm) the same way for both; its host shows the result as the settings change,
/// keeps it, or lets it go.
class ToolHost
{
public:
    virtual ~ToolHost() = default;

    /// A tool's last settings, and keeping them (view state, kept with the project).
    [[nodiscard]] virtual model::ToolValues toolValues(const model::ToolSpec& tool) const = 0;
    virtual void setToolValues(const model::ToolSpec& tool, const model::ToolValues& values) = 0;
    /// Shows what the tool would do with these settings (nothing, when it can't run).
    virtual void preview(const model::ToolSpec& tool, const model::ToolValues& values) = 0;
    /// Lets go of what preview showed.
    virtual void cancel() = 0;
    /// Keeps what the tool does: one undo step named for it.
    virtual void apply(const model::ToolSpec& tool, const model::ToolValues& values) = 0;
    /// What it acts on, for the tool's title: "14 notes", "4 bars", "2 clips".
    [[nodiscard]] virtual juce::String scope(const model::ToolSpec& tool) const = 0;
    /// The widest scope, for the title's width ("9999 notes").
    [[nodiscard]] virtual juce::String widestScope() const = 0;
    /// The song's key, for settings that name notes (null: none, or none needed).
    [[nodiscard]] virtual const music::Key* key() const = 0;
    /// Why a tool can't run now (empty: it can).
    [[nodiscard]] virtual juce::String unavailable(const model::ToolSpec& tool) const = 0;
    /// Changes whenever what the tools act on changes, so a chosen tool is shown again on it.
    [[nodiscard]] virtual std::uint64_t version() const = 0;
    /// What Apply's tooltip and the empty pane say ("the notes", "the clips").
    [[nodiscard]] virtual juce::String what() const = 0;
    /// On a Drum Kit's pads (drum-kit.md §23.4): each pad's note and name, low to high. A setting
    /// that names a pitch chooses a pad instead, and the melodic settings go. Empty elsewhere.
    [[nodiscard]] virtual std::vector<std::pair<int, juce::String>> pads() const { return {}; }
};
} // namespace fd::app
