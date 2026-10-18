// A stretch of time on chosen rows (ADR 0073, docs/design/time-selection.md §18): its commands on
// those rows, a stretch copied or moved, the song-wide time commands, and time inside a MIDI clip.
// Assistants never see the selection (selection.md §1), so each tool names the stretch and its rows.
// Each is one undo step.

#include "mcp/tools/Tools.h"
#include "model/TimeEdits.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
using Command = engine::Session::StretchCommand;

const std::vector<std::pair<std::string, Command>> kActions{
    {"delete", Command::Delete}, {"duplicate", Command::Duplicate},
    {"split", Command::Split},   {"crop", Command::Crop},
    {"mute", Command::Mute},     {"reverse", Command::Reverse},
    {"fade", Command::Fade},     {"new_midi_clip", Command::NewMidiClip}};

/// The stretch: `from` and `to`, positions; `to` after `from`.
model::TimeSpan spanArg(ToolEnv& env, const Json& args)
{
    const double from = seconds(env, positionArg(env, args["from"])),
                 to = seconds(env, positionArg(env, args["to"]));
    if (!(to > from))
        throw Problem{"\"to\" comes after \"from\"."};
    return {from, to};
}

/// Its rows: track ids ("t:12") and automation lane ids ("a:40"), or "all" for every track and lane.
model::TimeSelection stretchArg(ToolEnv& env, const Json& args)
{
    model::TimeSelection out{.span = spanArg(env, args)};
    const auto snap = env.session.snapshot();
    const auto& rows = args["rows"];
    const auto all = [](const Json& v) { return v.is_string() && v.get<std::string>() == "all"; };
    if (all(rows) || (rows.is_array() && std::any_of(rows.begin(), rows.end(), all)))
    {
        for (const auto& t : snap->tracks)
            out.tracks.insert(t.id);
        for (const auto& l : snap->automationLanes)
            out.lanes.insert(l.id);
        return out;
    }
    for (const auto& v : rows.is_array() ? rows : Json::array({rows}))
    {
        if (const auto t = parseId(v, IdKind::Track);
            t && snap->channel(*t) != nullptr && snap->channel(*t)->holdsClips())
            out.tracks.insert(*t);
        else if (const auto l = parseId(v, IdKind::Lane); l && snap->automationLane(*l) != nullptr)
            out.lanes.insert(*l);
        else
            throw Problem{v.dump() + " isn't a track or an automation lane (get_project lists them; return "
                                     "tracks and the master hold no clips)."};
    }
    if (out.tracks.empty() && out.lanes.empty())
        throw Problem{"Give the rows: track and lane ids, or \"all\"."};
    return out;
}

Json spanJson(ToolEnv& env, model::TimeSpan span)
{
    const auto t = tempo(env);
    return {{"from", positionJson(t.beatsForSeconds(span.start), t)},
            {"to", positionJson(t.beatsForSeconds(span.end), t)}};
}

Json clipsJson(const std::vector<std::uint64_t>& clips)
{
    Json out = Json::array();
    for (const auto id : clips)
        out.push_back(formatId(IdKind::Clip, id));
    return out;
}

Json stretchInput(Json extra, std::vector<std::string> required)
{
    Json properties{{"from", position("Where the stretch starts.")},
                    {"to", position("Where it ends.")},
                    {"rows", arrayOf({{"type", {"string", "integer"}},
                                      {"description",
                                       "A track id (\"t:3\"), an automation lane id (\"a:40\"), or \"all\""}},
                                     "Its rows: tracks and automation lanes, or [\"all\"] for every one")}};
    for (auto& [k, v] : extra.items())
        properties[k] = v;
    required.insert(required.begin(), {"from", "to", "rows"});
    return object(properties, required);
}

Json rangeResult()
{
    return result({{"from", positionResult()}, {"to", positionResult()}}, {"from", "to"});
}

Json sectionsJson(ToolEnv& env)
{
    env.session.flushSnapshot();
    const auto snap = env.session.snapshot();
    const auto t = snap->tempo;
    Json list = Json::array();
    for (const auto& s : snap->sections)
        list.push_back({{"section", formatId(IdKind::Section, s.id)},
                        {"name", s.name},
                        {"start", positionJson(t.beatsForSeconds(s.start), t)},
                        {"end", positionJson(t.beatsForSeconds(s.end), t)}});
    return list;
}
} // namespace

void addTimeTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations changes;
    ToolAnnotations destructive;
    destructive.destructive = true;

    Json actionNames = Json::array();
    for (const auto& [name, command] : kActions)
        actionNames.push_back(name);
    registry.tools.push_back(make(
        env,
        {"edit_range", "Edit a stretch of time on some rows",
         "Acts on the stretch from `from` to `to` on its `rows` alone (time-selection.md §8); the rest of "
         "the song stays where it is. `action`: delete (clips cut at its edges and the parts inside gone, "
         "lanes' points inside gone with the curve outside kept; the gap stays), duplicate (a copy right "
         "after it, overwriting), split (clips cut at its edges), crop (the clips crossing it lose what's "
         "outside), mute, reverse (audio), fade (a fade-in from a clip's start, a fade-out to its end, or a "
         "crossfade across two audio clips that meet), new_midi_clip (one over it on each track). One undo "
         "step. Refused, saying why, when it would do nothing.",
         stretchInput(
             {{"action", {{"type", "string"}, {"enum", actionNames}, {"description", "What to do"}}}},
             {"action"}),
         result({{"clips", arrayOf(id(IdKind::Clip, "One of them"),
                                   "Split, crop: the clips inside; new_midi_clip: the new ones", 0)},
                 {"range", rangeResult()}},
                {"clips", "range"}),
         destructive},
        [&env](const Json& args, CallContext&)
        {
            const auto stretch = stretchArg(env, args);
            const auto name = args.value("action", std::string());
            const auto it = std::find_if(kActions.begin(), kActions.end(),
                                         [&](const auto& a) { return a.first == name; });
            if (it == kActions.end())
                throw Problem{
                    "`action` is one of delete, duplicate, split, crop, mute, reverse, fade, new_midi_clip."};
            if (const auto why = env.session.stretchRefusal(stretch, it->second); !why.empty())
                throw Problem{why + "."};
            const auto r = env.session.editStretch(stretch, it->second);
            return ToolResult::ok(
                {{"clips", clipsJson(r.clips)}, {"range", spanJson(env, r.changed ? r.span : stretch.span)}});
        }));

    registry.tools.push_back(make(
        env,
        {"copy_range", "Copy or move a stretch of time on some rows",
         "Copies the stretch from `from` to `to` on its `rows` to `at`, its tracks landing from `onto` down "
         "(else from the topmost of them), keeping their places; lanes' curves stay on their own lanes "
         "(time-selection.md §8, §10). It overwrites what it lands on. With `move`, the place it left is "
         "cleared. One undo step.",
         stretchInput({{"at", position("Where it lands.")},
                       {"onto", id(IdKind::Track, "The track its topmost track lands on")},
                       {"move", boolean("Move it rather than copy it (default false)")}},
                      {"at"}),
         result({{"clips", arrayOf(id(IdKind::Clip, "One of them"), "The clips it put down", 0)},
                 {"range", rangeResult()}},
                {"clips", "range"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto stretch = stretchArg(env, args);
            const double at = seconds(env, positionArg(env, args["at"]));
            const auto snap = env.session.snapshot();
            int top = -1, onto = -1;
            for (int i = 0; i < static_cast<int>(snap->tracks.size()); ++i)
            {
                if (top < 0 && stretch.tracks.contains(snap->tracks[static_cast<size_t>(i)].id))
                    top = i;
                if (args.contains("onto") &&
                    snap->tracks[static_cast<size_t>(i)].id == track(env, args["onto"]).id)
                    onto = i;
            }
            const int shift = top >= 0 && onto >= 0 ? onto - top : 0;
            const auto r =
                env.session.moveStretch(stretch, at - stretch.span.start, shift, !args.value("move", false));
            if (!r.changed)
                throw Problem{"It wouldn't land anywhere new, or a track would land past the last one."};
            return ToolResult::ok({{"clips", clipsJson(r.clips)}, {"range", spanJson(env, r.span)}});
        }));

    registry.tools.push_back(make(
        env,
        {"edit_time", "Insert, delete, duplicate or copy time in the whole song",
         "Acts on every track and lane and the sections' marks (time-selection.md §9): `insert` puts empty "
         "time `length` long at `at`; `delete` takes `from` .. `to` out, the gap closing; `duplicate` puts a "
         "copy of `from` .. `to` right after it; `copy` puts a copy of `from` .. `to` at `at`. Everything "
         "after moves; a section loses or gains the time taken out of or put into it. One undo step.",
         object({{"action",
                  {{"type", "string"},
                   {"enum", {"insert", "delete", "duplicate", "copy"}},
                   {"description", "What to do"}}},
                 {"from", position("Where the stretch starts (delete, duplicate, copy)")},
                 {"to", position("Where it ends (delete, duplicate, copy)")},
                 {"at", position("Where time goes in (insert) or the copy lands (copy)")},
                 {"length", length("How much time to insert (insert)")}},
                {"action"}),
         result({{"sections", arrayOf(result({{"section", string("Its id")}, {"name", string("")}}),
                                      "The song's sections after", 0)}},
                {"sections"}),
         destructive},
        [&env](const Json& args, CallContext&)
        {
            const auto action = args.value("action", std::string());
            auto& s = env.session;
            bool done = false;
            if (action == "insert")
            {
                if (!args.contains("at") || !args.contains("length"))
                    throw Problem{"insert takes `at` and `length`."};
                done = s.insertTime(seconds(env, positionArg(env, args["at"])),
                                    seconds(env, lengthArg(env, args["length"])));
            }
            else if (action == "delete" || action == "duplicate" || action == "copy")
            {
                const auto span = spanArg(env, args);
                if (action == "delete")
                    done = s.deleteTime(span.start, span.end);
                else if (action == "duplicate")
                    done = s.duplicateTime(span.start, span.end);
                else
                {
                    if (!args.contains("at"))
                        throw Problem{"copy takes `at`: where the copy lands."};
                    const auto piece = s.copyTime(span.start, span.end);
                    done = !piece.empty();
                    if (done)
                        s.pasteTime(piece, seconds(env, positionArg(env, args["at"])));
                }
            }
            else
                throw Problem{"`action` is one of insert, delete, duplicate, copy."};
            if (!done)
                throw Problem{"Nothing to do there."};
            return ToolResult::ok({{"sections", sectionsJson(env)}});
        }));

    registry.tools.push_back(make(
        env,
        {"edit_clip_time", "Insert, delete, duplicate or split time inside a MIDI clip",
         "Acts on the stretch from `from` to `to` inside one MIDI clip (time-selection.md §14.1), where it "
         "plays in the song (a looped clip: its first lap). `insert` puts empty time as long as the stretch "
         "at "
         "its start; `delete` takes the stretch out; `duplicate` puts a copy of it right after it. The notes "
         "and controller points after it move, a note crossing an edge is cut there (one held across deleted "
         "time is shortened), and the clip's end (a looped clip's loop) follows. `split` cuts the notes "
         "crossing its edges. One undo step.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")},
                 {"action",
                  {{"type", "string"},
                   {"enum", {"insert", "delete", "duplicate", "split"}},
                   {"description", "What to do"}}},
                 {"from", position("Where the stretch starts.")},
                 {"to", position("Where it ends.")}},
                {"clip", "action", "from", "to"}),
         result({{"clip", clipResultSchema()}}, {"clip"}), changes},
        [&env](const Json& args, CallContext&)
        {
            const auto c = clip(env, args["clip"]);
            if (c.kind != model::ClipInfo::Kind::Midi)
                throw Problem{formatId(IdKind::Clip, c.id) +
                              " is an audio clip: time inside it is the audio editor's "
                              "(edit_audio_range)."};
            const auto span = spanArg(env, args);
            if (span.start < c.start - 1e-6 || span.end > c.end() + 1e-6)
                throw Problem{"The stretch isn't inside the clip: it plays from " +
                              positionJson(tempo(env).beatsForSeconds(c.start), tempo(env)).dump() + " to " +
                              positionJson(tempo(env).beatsForSeconds(c.end()), tempo(env)).dump() + "."};
            // Where it plays in the song, as the clip view's time.
            const double from = model::clipViewTime(c, span.start), to = from + span.length();
            const auto action = args.value("action", std::string());
            bool done = false;
            if (action == "split")
            {
                const auto& t = tempo(env);
                const double origin = model::clipViewOrigin(c);
                const auto split = model::splitNotesAt(c.notes, {}, t.beatsForSeconds(from - origin),
                                                       t.beatsForSeconds(to - origin));
                done = split.notes != c.notes;
                if (done)
                    env.session.setNotes(c.id, split.notes);
            }
            else if (action == "insert" || action == "delete" || action == "duplicate")
                done = env.session.editClipTime(c.id,
                                                action == "insert"   ? model::ClipTimeCommand::Insert
                                                : action == "delete" ? model::ClipTimeCommand::Delete
                                                                     : model::ClipTimeCommand::Duplicate,
                                                from, to);
            else
                throw Problem{"`action` is one of insert, delete, duplicate, split."};
            if (!done)
                throw Problem{action == "split" ? "No note crosses the stretch's edges."
                                                : "Nothing to do there."};
            env.session.flushSnapshot();
            return ToolResult::ok({{"clip", clipJson(env, clip(env, args["clip"]), false)}});
        }));
}
} // namespace fd::mcp
