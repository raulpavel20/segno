// Notes in MIDI clips (mcp.md §5): the piano roll's edits, by the same pure functions
// (model/NoteEditing), each call one undo step.

#include "mcp/tools/Tools.h"

#include "model/NoteEditing.h"
#include "model/NoteTools.h"
#include "model/PianoRollOptions.h"

#include <cmath>

namespace fd::mcp
{
using namespace tools;

namespace
{
/// The beats a note's start is shifted by between tools (from the clip's start) and the model
/// (from its content start).
double contentOffset(ToolEnv& env, const model::ClipInfo& c)
{
    return noteOffsetBeats(env, c);
}

model::ClipInfo midiClip(ToolEnv& env, const Json& value)
{
    auto c = clip(env, value);
    if (c.kind != model::ClipInfo::Kind::Midi)
        throw Problem{formatId(IdKind::Clip, c.id) + " is an audio clip; notes go in MIDI clips (create_midi_clip makes one)."};
    return c;
}

double noteStart(ToolEnv& env, const Json& value, const std::string& where)
{
    if (value.is_number() && value.get<double>() >= 0.0)
        return value.get<double>();
    if (value.is_string() && (value.get<std::string>() == "0" || value.get<std::string>() == "0.0"))
        return 0.0;
    try
    {
        return lengthArg(env, value);
    }
    catch (const Problem& p)
    {
        throw Problem{where + ".start: " + p.message};
    }
}

model::Note parseNote(ToolEnv& env, const Json& n, double offset, const std::string& where)
{
    const auto pitch = parsePitch(n["pitch"]);
    if (!pitch)
        throw Problem{where + ".pitch: " + n["pitch"].dump() + " isn't a MIDI note number (0-127) or a name such as \"C3\" (60)."};
    double length = 0.0;
    try
    {
        length = lengthArg(env, n["length"]);
    }
    catch (const Problem& p)
    {
        throw Problem{where + ".length: " + p.message};
    }
    return {*pitch, noteStart(env, n["start"], where) + offset, std::max(length, model::minNoteLength),
            velocityFromMidi(n.value("velocity", 100.0)), n.value("muted", false)};
}

/// Which notes a `where` argument picks: "all", or a filter by pitch and time.
std::set<std::size_t> select(ToolEnv& env, const std::vector<model::Note>& notes, double offset, const Json& where)
{
    std::set<std::size_t> out;
    if (where.is_string())
    {
        for (size_t i = 0; i < notes.size(); ++i)
            out.insert(i);
        return out;
    }
    const auto pitchOf = [](const Json& v, const char* name) -> std::optional<int> {
        if (v.is_null())
            return std::nullopt;
        if (auto p = parsePitch(v))
            return p;
        throw Problem{std::string("where.") + name + ": " + v.dump() + " isn't a pitch."};
    };
    const auto pitch = pitchOf(where.value("pitch", Json()), "pitch");
    const auto lowest = pitchOf(where.value("lowest", Json()), "lowest");
    const auto highest = pitchOf(where.value("highest", Json()), "highest");
    const auto from = where.contains("from") ? std::optional(noteStart(env, where["from"], "where")) : std::nullopt;
    const auto to = where.contains("to") ? std::optional(noteStart(env, where["to"], "where")) : std::nullopt;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const double start = n.start - offset;
        if ((pitch && n.pitch != *pitch) || (lowest && n.pitch < *lowest) || (highest && n.pitch > *highest) ||
            (from && start < *from - 1e-9) || (to && start >= *to - 1e-9))
            continue;
        out.insert(i);
    }
    return out;
}

Json whereSchema()
{
    return {{"anyOf",
             {{{"enum", {"all"}}},
              object({{"pitch", {{"type", {"integer", "string"}}, {"description", "Only this pitch"}}},
                      {"lowest", {{"type", {"integer", "string"}}, {"description", "Pitches from this one up"}}},
                      {"highest", {{"type", {"integer", "string"}}, {"description", "Pitches up to this one"}}},
                      {"from", {{"type", {"number", "string"}}, {"description", "Notes starting at or after this beat of the clip"}}},
                      {"to", {{"type", {"number", "string"}}, {"description", "Notes starting before this beat of the clip"}}}})}},
            {"description", "\"all\", or a filter: notes matching every condition given"}};
}

Json notesResult()
{
    return result({{"clip", clipResultSchema()},
                   {"notes", integer("How many notes the call wrote, moved or removed")},
                   {"lengthened", boolean("The clip grew to fit notes past its end")},
                   {"loopLengthened", boolean("A looped clip's loop grew to fit notes past its end")}},
                  {"clip", "notes", "lengthened"});
}
} // namespace

namespace tools
{
std::set<std::size_t> selectNotes(ToolEnv& env, const model::ClipInfo& c, const Json& where)
{
    return select(env, c.notes, contentOffset(env, c), where);
}

Json writeNotes(ToolEnv& env, const model::ClipInfo& c, const Json& input, bool replace)
{
    const double offset = contentOffset(env, c);
    std::vector<model::Note> notes = replace ? std::vector<model::Note>{} : c.notes;
    std::set<std::size_t> placed;
    for (size_t i = 0; i < input.size(); ++i)
    {
        placed.insert(notes.size());
        notes.push_back(parseNote(env, input[i], offset, "notes[" + std::to_string(i) + "]"));
    }
    const auto edit = model::resolveOverlaps(notes, placed);
    env.session.setNotes(c.id, edit.notes);
    env.session.flushSnapshot();
    const auto after = clip(env, formatId(IdKind::Clip, c.id));
    return {{"clip", clipJson(env, after, false)},
            {"notes", input.size()},
            {"lengthened", after.end() > c.end() + 1e-9},
            {"loopLengthened", after.loop.length > c.loop.length + 1e-9}};
}
} // namespace tools

void addNoteTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations writes;
    ToolAnnotations replaces;
    replaces.destructive = true;
    replaces.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"add_notes", "Add notes",
         "Adds notes to a MIDI clip. Times are beats from the clip's start. A new note replaces the part of a note "
         "of the same pitch it overlaps. Notes past the clip's end lengthen it to the end of that bar when nothing "
         "is in the way.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")}, {"notes", arrayOf(noteInput(), "The notes")}},
                {"clip", "notes"}),
         notesResult(), writes},
        [&env](const Json& args, CallContext&) {
            return ToolResult::ok(writeNotes(env, midiClip(env, args["clip"]), args["notes"], false));
        }));

    registry.tools.push_back(make(
        env,
        {"set_notes", "Replace all notes", "Replaces every note in a MIDI clip (an empty list clears it).",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")}, {"notes", arrayOf(noteInput(), "The notes", 0)}},
                {"clip", "notes"}),
         notesResult(), replaces},
        [&env](const Json& args, CallContext&) {
            return ToolResult::ok(writeNotes(env, midiClip(env, args["clip"]), args["notes"], true));
        }));

    registry.tools.push_back(make(
        env,
        {"remove_notes", "Remove notes", "Removes the notes that match: all of them, or a filter by pitch and time.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")}, {"where", whereSchema()}}, {"clip", "where"}),
         notesResult(), replaces},
        [&env](const Json& args, CallContext&) {
            const auto c = midiClip(env, args["clip"]);
            const auto chosen = select(env, c.notes, contentOffset(env, c), args["where"]);
            env.session.setNotes(c.id, model::deleteNotes(c.notes, chosen).notes);
            env.session.flushSnapshot();
            return ToolResult::ok({{"clip", clipJson(env, clip(env, args["clip"]), false)},
                                   {"notes", chosen.size()},
                                   {"lengthened", false}});
        }));

    registry.tools.push_back(make(
        env,
        {"edit_notes", "Edit notes",
         "Changes the notes that match, as the clip view's Notes menu does: velocity; transpose by semitones; "
         "shift; transpose by steps of the song's key; fit to the key; quantise (with strength and swing); stretch; "
         "legato, fixed length, join, split; reverse, mirror; invert and re-voice chords; mute; add a bass. They "
         "apply in that order. Edited notes win over notes of the same pitch they land on.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")},
                 {"where", whereSchema()},
                 {"transpose", integer("Semitones (limited so every note stays in 0-127)", -127, 127)},
                 {"transpose_steps", integer("Steps of the song key's scale (C to D in C major is one)", -127, 127)},
                 {"fit_to_key", boolean("Notes outside the song's key move to its nearest note (a tie: the way the melody moves)")},
                 {"shift", {{"type", {"number", "string"}}, {"description", "Beats, negative to move earlier, or text such as \"1/16\""}}},
                 {"velocity", {{"type", {"integer", "string"}},
                               {"description", "Set it (1-127), or change it by a signed amount such as \"+10\" or \"-20\""}}},
                 {"quantise", length("Move starts to the nearest line of this grid.")},
                 {"strength", number("How far quantise moves them, 0-1 (default 1: all the way)")},
                 {"swing", number("Quantise's swing, 0-1: every other line later, by up to a third of a step")},
                 {"stretch", number("Times the timing and lengths from the first note's start: 2 is half speed, 0.5 double; negative reverses")},
                 {"legato", boolean("Each note lasts until the next one starts")},
                 {"fixed_length", length("Every note this long.")},
                 {"join", boolean("Notes of the same pitch that touch or overlap become one")},
                 {"split_at", {{"type", {"number", "string"}}, {"description", "Split the notes crossing this beat of the clip"}}},
                 {"reverse", boolean("The notes' order in time mirrored within their span")},
                 {"mirror", boolean("Pitches upside down around the middle of their range (by the key's steps with a song key)")},
                 {"invert", {{"type", "string"}, {"enum", {"up", "down"}}, {"description", "Each chord's lowest note up an octave, or its highest down"}}},
                 {"voicing", {{"type", "string"}, {"enum", {"smooth", "close", "open", "drop2", "drop3", "spread"}},
                              {"description", "Re-voice each chord; smooth keeps the first and moves the rest least"}}},
                 {"mute", boolean("true mutes them (kept, not played), false plays them again")},
                 {"add_bass", boolean("Each chord's root an octave or more under it")}},
                {"clip", "where"}),
         notesResult(), writes},
        [&env](const Json& args, CallContext&) {
            const auto c = midiClip(env, args["clip"]);
            const double offset = contentOffset(env, c);
            auto chosen = select(env, c.notes, offset, args["where"]);
            if (chosen.empty())
                return ToolResult::ok({{"clip", clipJson(env, c, false)}, {"notes", 0}, {"lengthened", false}},
                                      "No notes match.");
            model::NoteEdit edit{c.notes, chosen};
            if (args.contains("velocity"))
            {
                const auto& v = args["velocity"];
                const auto text = v.is_string() ? v.get<std::string>() : std::string();
                if (!text.empty() && (text[0] == '+' || text[0] == '-'))
                {
                    char* end = nullptr;
                    const double delta = std::strtod(text.c_str(), &end);
                    if (end == text.c_str() || *end != '\0')
                        throw Problem{"velocity: \"" + text + "\" isn't a signed amount such as \"+10\"."};
                    edit.notes = model::offsetVelocity(edit.notes, edit.selection, static_cast<float>(delta / 127.0));
                }
                else
                {
                    const double value = v.is_number() ? v.get<double>() : std::atof(text.c_str());
                    if (!(value >= 1.0 && value <= 127.0))
                        throw Problem{"velocity: set it to 1-127, or change it by a signed amount such as \"+10\"."};
                    for (auto i : edit.selection)
                        edit.notes[i].velocity = velocityFromMidi(value);
                }
            }
            if (args.contains("transpose"))
                edit = model::transposeNotes(edit.notes, edit.selection, args["transpose"].get<int>());
            if (args.contains("shift"))
            {
                const auto& s = args["shift"];
                double by = 0.0;
                if (s.is_number())
                    by = s.get<double>();
                else
                {
                    auto text = s.get<std::string>();
                    const bool negative = !text.empty() && text[0] == '-';
                    by = (negative ? -1.0 : 1.0) * lengthArg(env, negative ? Json(text.substr(1)) : s);
                }
                edit = model::nudgeNotes(edit.notes, edit.selection, by);
            }
            const auto key = env.session.snapshot()->keys.keyAt(c.contentStartBeat);
            const auto needsKey = [&key](const char* arg)
            {
                if (!key)
                    throw Problem{std::string(arg) + ": the song has no key; set_key sets one."};
            };
            if (args.contains("transpose_steps"))
            {
                needsKey("transpose_steps");
                edit = model::transposeInKey(edit.notes, edit.selection, *key, args["transpose_steps"].get<int>());
            }
            if (args.value("fit_to_key", false))
            {
                needsKey("fit_to_key");
                edit = model::fitToKey(edit.notes, edit.selection, *key);
            }
            if (args.contains("quantise"))
            {
                model::QuantiseSettings settings;
                settings.strength = std::clamp(args.value("strength", 1.0), 0.0, 1.0);
                settings.swing = std::clamp(args.value("swing", 0.0), 0.0, 1.0);
                edit = model::quantiseNotes(edit.notes, edit.selection,
                                            {.step = lengthArg(env, args["quantise"]), .origin = c.contentStartBeat,
                                             .bar = tempo(env).beatsPerBar()},
                                            settings);
            }
            if (args.contains("stretch"))
            {
                const double factor = args["stretch"].get<double>();
                if (factor == 0.0)
                    throw Problem{"stretch: 0 would squash every note to one point; 2 is half speed, 0.5 double."};
                if (const auto span = model::notesSpan(edit.notes, edit.selection))
                    edit = model::stretchNotes(edit.notes, edit.selection, span->first, factor, true);
            }
            if (args.value("legato", false))
                edit = model::legatoNotes(edit.notes, edit.selection);
            if (args.contains("fixed_length"))
                edit = model::fixedLength(edit.notes, edit.selection, lengthArg(env, args["fixed_length"]));
            if (args.value("join", false))
                edit = model::joinNotes(edit.notes, edit.selection);
            if (args.contains("split_at"))
                edit = model::splitNotes(edit.notes, edit.selection, noteStart(env, args["split_at"], "split_at") + offset);
            if (args.value("reverse", false))
                edit = model::reverseNotes(edit.notes, edit.selection);
            if (args.value("mirror", false))
                edit = model::mirrorPitches(edit.notes, edit.selection, key ? &*key : nullptr);
            if (args.contains("invert"))
                edit = model::invertChords(edit.notes, edit.selection, args["invert"].get<std::string>() == "up");
            if (args.contains("voicing"))
            {
                const auto voicing = model::voicingFromWord(args["voicing"].get<std::string>());
                if (!voicing)
                    throw Problem{"voicing: one of smooth, close, open, drop2, drop3, spread."};
                edit = model::voiceChords(edit.notes, edit.selection, *voicing);
            }
            if (args.contains("mute"))
                for (auto i : edit.selection)
                    edit.notes[i].muted = args["mute"].get<bool>();
            if (args.value("add_bass", false))
                edit = model::addBass(edit.notes, edit.selection);
            const auto resolved = model::resolveOverlaps(edit.notes, edit.selection);
            env.session.setNotes(c.id, resolved.notes);
            env.session.flushSnapshot();
            const auto after = clip(env, args["clip"]);
            return ToolResult::ok({{"clip", clipJson(env, after, false)},
                                   {"notes", chosen.size()},
                                   {"lengthened", after.end() > c.end() + 1e-9},
                                   {"loopLengthened", after.loop.length > c.loop.length + 1e-9}});
        }));
}
} // namespace fd::mcp
