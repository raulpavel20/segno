#pragma once

#include "model/NoteTools.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fd::model
{
/// The tools that transform and generate notes (docs/design/piano-roll.md §12, §23): each one
/// described once, as data (its settings, their ranges and defaults), and run by one pure function.
/// The popover builds itself from the description, assistants check their arguments against it,
/// and both run the same function.

/// One setting of a tool: a choice, or a number.
struct ToolSetting
{
    std::string_view id;    // as assistants write it: "time"
    std::string_view label; // as the popover shows it: "Time"
    /// A choice's options, as shown and as assistants write them ("Up" / "up"); empty for a number.
    std::vector<std::string_view> choices, words;
    double min = 0.0, max = 1.0, step = 1.0;
    double initial = 0.0;  // a number's default, or a choice's index
    std::string_view unit; // after a number: "ms", "%", "per note"; "pitch" shows a note's name
    /// Shown only while the setting `when` has the choice `whenIs` (the Velocity tool's shapes).
    std::string_view when;
    int whenIs = -1;
    bool menu = false; // a choice too long for segments: a choice field
    bool halveDouble = false; // a number with ×2 and ÷2 beside it (the audio Tempo tool's tempo)
    /// Not on a Drum Kit's pads (drum-kit.md §23.4): hidden there, its first choice taken (Rhythm's
    /// On: a pitch, never the chords).
    bool melodic = false;

    [[nodiscard]] bool isChoice() const noexcept { return !choices.empty(); }
};

enum class ToolKind
{
    Transform, // the selected notes, or all
    Generate   // new notes into a span of the clip
};

struct ToolSpec
{
    std::string_view id;   // "strum"
    std::string_view name; // "Strum"
    ToolKind kind = ToolKind::Transform;
    std::string_view about; // one line, for assistants and tooltips
    std::vector<ToolSetting> settings;
    /// It works on a Drum Kit's pads (drum-kit.md §23.4): shown there. Strum, Arpeggiate, Randomise
    /// pitch, Melody, Chords and Bassline don't.
    bool drums = true;

    [[nodiscard]] const ToolSetting* setting(std::string_view which) const noexcept;
};

/// A tool's settings by id; a choice is its option's index.
using ToolValues = std::map<std::string, double>;

/// Every tool, transforms first, in the menus' order.
[[nodiscard]] const std::vector<ToolSpec>& noteTools();
[[nodiscard]] const ToolSpec* findNoteTool(std::string_view id) noexcept;

/// The values with every setting's default where it has none, each kept in its range and on its
/// step (a choice's index whole). Settings the tool hasn't are dropped.
[[nodiscard]] ToolValues toolValues(const ToolSpec& tool, const ToolValues& given);

/// Whether a setting shows for these values (one shown only for a choice of another), and on a
/// Drum Kit's pads (`drums`: a melodic one doesn't).
[[nodiscard]] bool settingShown(const ToolSpec& tool, const ToolSetting& setting, const ToolValues& values,
                                bool drums = false);

/// A value as the popover writes it: "40 ms", "−8 per note", "C3", "Up".
[[nodiscard]] std::string settingText(const ToolSetting& setting, double value, const music::Key* key);

/// What a tool works with besides the notes.
struct ToolContext
{
    const music::Key* key = nullptr;
    double beatsPerBar = 4.0;
    double secondsPerBeat = 0.5; // settings in ms to beats
    /// The song beat of the content's zero, so bars and strong beats fall where the song's do.
    double origin = 0.0;
    /// Where a generator writes: content beats [first, second).
    std::pair<double, double> span{0.0, 16.0};
    /// The grid's step, in beats: what Quantise moves notes to.
    double gridStep = 0.25;
    /// On a Drum Kit's pads (drum-kit.md §23.4): melodic settings take their first choice.
    bool drums = false;
};

/// A tool's run: the notes as it leaves them (the changed or new notes selected), or why it
/// can't run (nothing changed).
struct ToolRun
{
    NoteEdit edit;
    std::string why;
};

/// Why a tool can't run on these notes now (empty: it can): a transform without notes, the
/// bassline without chords.
[[nodiscard]] std::string toolUnavailable(const ToolSpec& tool, const std::vector<Note>& notes);

/// Runs a tool: a transform on the selected notes (or every note: then nothing is selected after),
/// a generator into the context's span. `values` as toolValues gives them.
[[nodiscard]] ToolRun runNoteTool(const ToolSpec& tool, const std::vector<Note>& notes,
                                  const std::set<std::size_t>& selection, const ToolValues& values,
                                  const ToolContext& context);
} // namespace fd::model
