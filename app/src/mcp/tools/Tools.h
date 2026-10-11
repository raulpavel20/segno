#pragma once

#include "engine/Session.h"
#include "mcp/Conventions.h"
#include "mcp/MainThread.h"
#include "mcp/protocol/Registry.h"

#include <map>

namespace fd::mcp
{
/// What the tools work on (docs/design/mcp.md §5). Tools reach the project only through
/// engine::Session and the module host binding (ADR 0021), like the UI.
struct ToolEnv
{
    engine::Session& session;
    MainThread& main;
    juce::File scratch; // renders in flight

    /// A module type's sections ({id, title}, in order), read from its panel once.
    std::map<std::string, Json> sections;
    /// The app's settings (ADR 0045): get_settings and set_settings.
    model::SettingsStore* settings = nullptr;
};

/// The tool, resource and prompt catalogue. The functions run on transport threads; each
/// tool's body runs on the message thread through env.main.
void addReadTools(Registry& registry, ToolEnv& env);
void addTransportTools(Registry& registry, ToolEnv& env);
void addClipTools(Registry& registry, ToolEnv& env);
void addNoteTools(Registry& registry, ToolEnv& env);
void addModuleTools(Registry& registry, ToolEnv& env);
void addPresetTools(Registry& registry, ToolEnv& env);
void addRenderTools(Registry& registry, ToolEnv& env);
void addMixTools(Registry& registry, ToolEnv& env);
void addAutomationTools(Registry& registry, ToolEnv& env);
void addRecordingTools(Registry& registry, ToolEnv& env);
void addProjectTools(Registry& registry, ToolEnv& env);
void addTrackTools(Registry& registry, ToolEnv& env);
void addSettingsTools(Registry& registry, ToolEnv& env);
void addBrowserTools(Registry& registry, ToolEnv& env);
void addResources(Registry& registry, ToolEnv& env);
void addPrompts(Registry& registry);
/// Everything above.
void registerAll(Registry& registry, ToolEnv& env);

namespace tools
{
// ---- building tools -------------------------------------------------------------------------

struct Spec
{
    std::string name;
    std::string title;
    std::string description;
    Json input;
    Json output = nullptr;
    ToolAnnotations annotations;
    /// Edits are one undo step, "Assistant: <name>" (ADR 0021). Off for tools that change no
    /// project state (transport, view state) and for undo/redo themselves.
    bool undoStep = true;
};
using Body = std::function<ToolResult(const Json& arguments, CallContext& context)>;

/// A tool whose body runs on the message thread with the latest snapshot published; edits are
/// grouped into one named undo step.
Tool make(ToolEnv& env, Spec spec, Body body);

// ---- schemas ----------------------------------------------------------------------------------

/// An object with these properties; no others allowed.
Json object(Json properties, const std::vector<std::string>& required = {});
/// An object result: these properties, extras allowed.
Json result(Json properties, const std::vector<std::string>& required = {});
Json string(const std::string& description);
Json number(const std::string& description);
Json integer(const std::string& description, std::optional<int> min = {}, std::optional<int> max = {});
Json boolean(const std::string& description);
Json arrayOf(Json items, const std::string& description, std::optional<int> minItems = 1);
Json id(IdKind kind, const std::string& description);
Json ids(IdKind kind, const std::string& description);
Json position(const std::string& description);
Json length(const std::string& description);
Json positionResult();
/// A note to write: pitch, start, length, velocity.
Json noteInput();

// ---- reading arguments --------------------------------------------------------------------------

/// Thrown by the helpers below: becomes the tool's error result.
struct Problem
{
    std::string message;
};

[[nodiscard]] model::TempoMap tempo(ToolEnv& env);
[[nodiscard]] double seconds(ToolEnv& env, double beats);
[[nodiscard]] double beats(ToolEnv& env, double seconds);

/// The track, clip or module an argument names (copies from the current snapshot), or a Problem.
/// `track` is a track that holds clips; `channel` any channel: a track, a return or the master.
model::TrackInfo track(ToolEnv& env, const Json& value);
model::TrackInfo channel(ToolEnv& env, const Json& value);
model::ClipInfo clip(ToolEnv& env, const Json& value);
/// The module and the track that holds it.
std::pair<model::DeviceSlot, model::TrackInfo> module(ToolEnv& env, const Json& value);
double positionArg(ToolEnv& env, const Json& value);
double lengthArg(ToolEnv& env, const Json& value);
/// A file the DAW can read (open question B18: any readable file).
juce::File readableFile(const Json& value);

// ---- describing things (shared by tools and resources) ------------------------------------

Json projectJson(ToolEnv& env);
Json transportJson(ToolEnv& env);
Json trackJson(ToolEnv& env, const model::TrackInfo& track, bool notes = false);
/// A track's inputs and arm state (recording.md §12), and the inputs there are.
Json inputJson(const model::InputInfo& input);
Json inputsJson(ToolEnv& env);
/// A channel's mix (C3): volume, pan, mute, solo and sends.
Json mixJson(ToolEnv& env, const model::TrackInfo& channel);
/// "track", "return" or "master".
std::string kindName(const model::TrackInfo& channel);
/// Pan as people write it: "C", "25L", "50R".
std::string panText(float pan);
Json clipJson(ToolEnv& env, const model::ClipInfo& clip, bool notes);
Json moduleSummary(const model::DeviceSlot& slot);
/// Everything about a module: parameters, sections, view, and non-parameter state.
Json moduleJson(ToolEnv& env, const model::DeviceSlot& slot, const model::TrackInfo& track);
/// The automation lanes (C4, ADR 0029), in row order, with their assignments.
Json automationLanesJson(ToolEnv& env);
/// A parameter's automation (its lane, points, range, on); null without a lane.
Json parameterAutomationJson(ToolEnv& env, const engine::Session::ParamTarget& target);
/// The module catalogue (our modules and scanned plugins).
Json moduleTypesJson(ToolEnv& env, const std::string& kind = {});
/// One type's reference; null if there's no such type.
Json moduleTypeJson(ToolEnv& env, const std::string& type);
/// A module type's sections, from its panel (cached).
Json sectionsOf(ToolEnv& env, engine::DeviceHandle& handle);

/// Writes notes to a MIDI clip, added to its notes or replacing them (add_notes, set_notes,
/// create_midi_clip). New notes win where they overlap notes of the same pitch; notes past the
/// clip's end lengthen it as in the piano roll (B15). Returns {clip, written, lengthened}.
Json writeNotes(ToolEnv& env, const model::ClipInfo& clip, const Json& notes, bool replace);

/// Result schemas shared between tools.
Json clipResultSchema();
Json projectResultSchema();
Json moduleResultSchema();

/// The beat (from the song's start) of a note's start, and back: notes count from the clip's
/// start in tools, from its content start in the model.
double clipStartBeat(ToolEnv& env, const model::ClipInfo& clip);
Json noteJson(const model::Note& note, double offsetBeats);
/// dB for results: rounded to 0.1, null for -inf.
Json db(double value);
} // namespace tools
} // namespace fd::mcp
