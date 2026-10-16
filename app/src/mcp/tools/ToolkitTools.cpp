// The tools that transform and generate notes (M3 P4c, docs/design/piano-roll.md §12, §23), for
// assistants: the clip view's Notes ▸ Transform ▸ and Generate ▸, by the same descriptions and the
// same functions (model/NoteToolkit). Each call is one undo step.

#include "mcp/tools/Tools.h"
#include "model/NoteToolkit.h"

#include <cmath>
#include <sstream>

namespace fd::mcp
{
using namespace tools;

namespace
{
model::ClipInfo midiClipArg(ToolEnv& env, const Json& value)
{
    auto c = clip(env, value);
    if (c.kind != model::ClipInfo::Kind::Midi)
        throw Problem{formatId(IdKind::Clip, c.id) +
                      " is an audio clip; notes go in MIDI clips (create_midi_clip makes one)."};
    return c;
}

const model::ToolSpec& toolArg(const Json& value, model::ToolKind kind)
{
    const auto* tool = value.is_string() ? model::findNoteTool(value.get<std::string>()) : nullptr;
    if (tool == nullptr || tool->kind != kind)
    {
        std::string names;
        for (const auto& t : model::noteTools())
            if (t.kind == kind)
                names += (names.empty() ? "" : ", ") + std::string(t.id);
        throw Problem{"tool: " + value.dump() + " isn't one of " + names +
                      " (list_note_tools has their settings)."};
    }
    return *tool;
}

/// A setting as list_note_tools shows it.
Json settingJson(const model::ToolSpec& tool, const model::ToolSetting& s)
{
    Json j{{"id", s.id}, {"label", s.label}};
    if (s.isChoice())
    {
        j["kind"] = "choice";
        j["choices"] = s.words;
        j["default"] = s.words[static_cast<std::size_t>(s.initial)];
    }
    else
    {
        j["kind"] = "number";
        j["min"] = s.min;
        j["max"] = s.max;
        j["default"] = s.initial;
        if (s.unit == "pitch")
            j["unit"] = "MIDI note number (60 is C3), a name (\"C3\"), or on a Drum Kit's clip a pad's name";
        else if (!s.unit.empty())
            j["unit"] = s.unit;
    }
    if (const auto* other = s.when.empty() ? nullptr : tool.setting(s.when); other != nullptr)
        j["onlyWhen"] = {{s.when, other->words[static_cast<std::size_t>(s.whenIs)]}};
    return j;
}

/// The settings an assistant gave, as the tool's values: words for choices, numbers in range; a
/// pitch also as a name, or on a kit's clip a pad's (drum-kit.md §23.5).
model::ToolValues settingsArg(const model::ToolSpec& tool, const Json& settings, const Pads& pads = {})
{
    model::ToolValues out;
    if (settings.is_null())
        return out;
    if (!settings.is_object())
        throw Problem{"settings: an object of the tool's settings, such as {\"time\": 40}."};
    std::string known;
    for (const auto& s : tool.settings)
        known += (known.empty() ? "" : ", ") + std::string(s.id);
    for (const auto& [id, value] : settings.items())
    {
        const auto* s = tool.setting(id);
        if (s == nullptr)
            throw Problem{"settings." + id + ": " + std::string(tool.name) + " has " + known + "."};
        if (s->isChoice())
        {
            const auto it = value.is_string()
                                ? std::find(s->words.begin(), s->words.end(), value.get<std::string>())
                                : s->words.end();
            if (it == s->words.end())
            {
                std::string words;
                for (auto w : s->words)
                    words += (words.empty() ? "" : ", ") + std::string(w);
                throw Problem{"settings." + id + ": one of " + words + "."};
            }
            out[id] = static_cast<double>(it - s->words.begin());
            continue;
        }
        if (s->unit == "pitch" && value.is_string())
        {
            const auto pitch = pitchOrPad(value, pads);
            if (!pitch)
                throw Problem{
                    "settings." + id + ": " + value.dump() + " isn't a note name such as \"C3\"" +
                    (pads.empty() ? std::string(".") : std::string(", nor one of the kit's pads."))};
            out[id] = static_cast<double>(*pitch);
            continue;
        }
        if (!value.is_number() || value.get<double>() < s->min || value.get<double>() > s->max)
        {
            std::ostringstream range;
            range << s->min << " to " << s->max;
            throw Problem{"settings." + id + ": a number, " + range.str() + "."};
        }
        out[id] = value.get<double>();
    }
    return out;
}

model::ToolContext contextOf(ToolEnv& env, const model::ClipInfo& c, const std::optional<music::Key>& key)
{
    const auto t = tempo(env);
    return {.key = key ? &*key : nullptr,
            .beatsPerBar = t.beatsPerBar(),
            .secondsPerBeat = t.secondsForBeats(1.0),
            .origin = c.contentStartBeat};
}

Json applied(ToolEnv& env, const Json& args, const model::ClipInfo& before, const model::NoteEdit& edit)
{
    env.session.setNotes(before.id, edit.notes);
    env.session.flushSnapshot();
    const auto after = clip(env, args["clip"]);
    return {{"clip", clipJson(env, after, false)},
            {"notes", edit.selection.size()},
            {"lengthened", after.end() > before.end() + 1e-9}};
}
} // namespace

void addToolkitTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations writes;

    registry.tools.push_back(make(
        env,
        {"list_note_tools", "List note tools",
         "The tools that transform notes (transform_notes) and generate them (generate_notes), as the clip "
         "view's Notes > Transform and Generate have them: each with what it does and its settings (a choice "
         "of words, or a number with its range and unit), and their defaults.",
         object({}), result({{"transform", {{"type", "array"}}}, {"generate", {{"type", "array"}}}}), reads},
        [](const Json&, CallContext&)
        {
            Json transform = Json::array(), generate = Json::array();
            for (const auto& t : model::noteTools())
            {
                Json settings = Json::array();
                for (const auto& s : t.settings)
                    settings.push_back(settingJson(t, s));
                Json j{
                    {"tool", t.id}, {"name", t.name}, {"about", t.about}, {"settings", std::move(settings)}};
                (t.kind == model::ToolKind::Transform ? transform : generate).push_back(std::move(j));
            }
            return ToolResult::ok({{"transform", std::move(transform)}, {"generate", std::move(generate)}});
        }));

    registry.tools.push_back(make(
        env,
        {"transform_notes", "Transform notes",
         "Runs a transform on the notes that match, as the clip view's Notes > Transform does: quantise, "
         "humanize, strum, arpeggiate, chop, ornament, velocity, span, randomise. Settings left out take "
         "their defaults (list_note_tools); random ones are seeded, so the same settings give the same "
         "notes. Quantise moves to the grid given (default 1/16).",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")},
                 {"where", whereSchema()},
                 {"tool", string("The transform: quantise, humanize, strum, arpeggiate, chop, ornament, "
                                 "velocity, span, randomise")},
                 {"settings", {{"type", "object"}, {"description", "The tool's settings by id"}}},
                 {"grid", length("Quantise's grid (default 1/16).")}},
                {"clip", "where", "tool"}),
         notesResult(), writes},
        [&env](const Json& args, CallContext&)
        {
            const auto c = midiClipArg(env, args["clip"]);
            const auto& tool = toolArg(args["tool"], model::ToolKind::Transform);
            const auto values = settingsArg(tool, args.value("settings", Json()));
            const auto chosen = selectNotes(env, c, args["where"]);
            if (chosen.empty())
                return ToolResult::ok(
                    {{"clip", clipJson(env, c, false)}, {"notes", 0}, {"lengthened", false}},
                    "No notes match.");
            const auto key = env.session.snapshot()->keys.keyAt(c.contentStartBeat);
            auto context = contextOf(env, c, key);
            if (args.contains("grid"))
                context.gridStep = lengthArg(env, args["grid"]);
            // The matching notes, as a selection: on all of them, they're all chosen.
            const auto run = model::runNoteTool(tool, c.notes, chosen, values, context);
            if (!run.why.empty())
                throw Problem{run.why + "."};
            return ToolResult::ok(applied(env, args, c, run.edit));
        }));

    registry.tools.push_back(make(
        env,
        {"generate_notes", "Generate notes",
         "Writes new notes into a MIDI clip, as the clip view's Notes > Generate does: rhythm, melody, "
         "chords, bassline (which follows the clip's chords). Into start and length (beats from the "
         "clip's start), else the clip's loop, or the clip. The existing setting keeps the notes there "
         "(new notes go around kept notes of the same pitch) or replaces them. On a Drum Kit's clip a "
         "pitch setting takes a pad's name: rhythm with {\"pitch\": \"Closed hat\"}.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")},
                 {"tool", string("The generator: rhythm, melody, chords, bassline")},
                 {"settings", {{"type", "object"}, {"description", "The tool's settings by id"}}},
                 {"start", number("Beats from the clip's start (default 0)")},
                 {"length", length("How long a span to write (default: the clip's loop, or the clip).")}},
                {"clip", "tool"}),
         notesResult(), writes},
        [&env](const Json& args, CallContext&)
        {
            const auto c = midiClipArg(env, args["clip"]);
            const auto& tool = toolArg(args["tool"], model::ToolKind::Generate);
            const auto values = settingsArg(tool, args.value("settings", Json()), clipPads(env, c));
            const auto t = tempo(env);
            const double offset = noteOffsetBeats(env, c);
            const double whole = c.loop.on() ? t.beatsForSeconds(c.loop.length) : t.beatsForSeconds(c.length);
            const double start = args.value("start", 0.0);
            if (start < 0.0)
                throw Problem{"start: 0 or later."};
            const double span = args.contains("length") ? lengthArg(env, args["length"]) : whole - start;
            if (span <= 0.0)
                throw Problem{"length: the span is empty; give a length."};
            const auto key = env.session.snapshot()->keys.keyAt(c.contentStartBeat);
            auto context = contextOf(env, c, key);
            context.span = {offset + start, offset + start + span};
            const auto run = model::runNoteTool(tool, c.notes, {}, values, context);
            if (!run.why.empty())
                throw Problem{run.why + "."};
            return ToolResult::ok(applied(env, args, c, run.edit));
        }));
}
} // namespace fd::mcp
