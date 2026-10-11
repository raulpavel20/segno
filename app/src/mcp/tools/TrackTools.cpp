// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md §11): names and
// colours, deleting with what goes along, moving, duplicating, copying clips to a place.

#include "mcp/tools/Tools.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
/// A palette colour: 0..15.
int colourArg(const Json& value)
{
    if (!value.is_number_integer() || value.get<int>() < 0 || value.get<int>() > 15)
        throw Problem{"Give the colour as a palette index, 0 to 15."};
    return value.get<int>();
}

/// A track or return track (never the master).
model::TrackInfo trackOrReturn(ToolEnv& env, const Json& value)
{
    const auto c = channel(env, value);
    if (c.role == model::TrackInfo::Role::Master)
        throw Problem{"The master can't be renamed, recoloured, moved, duplicated or deleted."};
    return c;
}

Json orderJson(ToolEnv& env, bool returns)
{
    env.session.flushSnapshot();
    Json list = Json::array();
    for (const auto& t : returns ? env.session.snapshot()->returns : env.session.snapshot()->tracks)
        list.push_back(formatId(IdKind::Track, t.id));
    return list;
}
} // namespace

void addTrackTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations changes;
    ToolAnnotations deletes;
    deletes.destructive = true;
    ToolAnnotations moves;
    moves.destructive = true; // pasted clips cut what they land on (undoable)

    registry.tools.push_back(make(
        env,
        {"set_track", "Rename or recolour a track",
         "Sets a track's or return track's name and/or colour (a palette index, 0 to 15). Its clips without "
         "a "
         "colour of their own follow it. One undo step.",
         object({{"track", id(IdKind::Track, "The track or return track")},
                 {"name", string("Its new name")},
                 {"colour", integer("Its colour: a palette index", 0, 15)}},
                {"track"}),
         result({{"track", string("Its id")}, {"name", string("")}, {"colour", integer("")}},
                {"track", "name", "colour"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto t = trackOrReturn(env, args["track"]);
            if (!args.contains("name") && !args.contains("colour"))
                throw Problem{"Give a name or a colour to set."};
            if (args.contains("name"))
            {
                const auto name = args["name"].get<std::string>();
                if (juce::String(name).trim().isEmpty())
                    throw Problem{"A track's name can't be empty."};
                env.session.renameTrack(t.id, name);
            }
            if (args.contains("colour"))
                env.session.setTrackColour(t.id, colourArg(args["colour"]));
            env.session.flushSnapshot();
            const auto now = channel(env, args["track"]);
            return ToolResult::ok(
                {{"track", formatId(IdKind::Track, now.id)}, {"name", now.name}, {"colour", now.colour}});
        }));

    registry.tools.push_back(make(
        env,
        {"delete_tracks", "Delete tracks",
         "Deletes tracks or return tracks with what only made sense with them: their clips and takes, the "
         "automation lanes that drive only them (their parameters leave lanes they share), the sidechains "
         "they feed "
         "(back to Off) and, for a return, every send to it. Not while recording. Undoable.",
         object({{"tracks", ids(IdKind::Track, "The tracks or return tracks")}}, {"tracks"}),
         result({{"deleted", integer("How many")}}, {"deleted"}), deletes},
        [&env](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> ids;
            for (const auto& value : args["tracks"])
                ids.push_back(trackOrReturn(env, value).id);
            int deleted = 0;
            for (const auto id : ids)
            {
                std::string why;
                if (!env.session.deleteTrack(id, &why))
                    throw Problem{why.empty() ? std::string("A track couldn't be deleted.") : why};
                ++deleted;
            }
            return ToolResult::ok({{"deleted", deleted}});
        }));

    registry.tools.push_back(
        make(env,
             {"move_track", "Move a track",
              "Moves a track before another track (or to the end), the automation lanes right after it "
              "moving with it; "
              "or a return track among the returns. One undo step.",
              object({{"track", id(IdKind::Track, "The track or return track")},
                      {"before", id(IdKind::Track,
                                    "The track (or return) to put it before; leave it out for the end")}},
                     {"track"}),
              result({{"order", arrayOf(string("A track's id"), "The tracks (or returns) in their order now",
                                        std::nullopt)}},
                     {"order"}),
              changes},
             [&env](const Json& args, CallContext&)
             {
                 const auto t = trackOrReturn(env, args["track"]);
                 std::uint64_t before = 0;
                 if (args.contains("before"))
                 {
                     const auto b = trackOrReturn(env, args["before"]);
                     if (b.role != t.role)
                         throw Problem{"A track moves among the tracks, a return track among the returns."};
                     before = b.id;
                 }
                 env.session.moveTrack(t.id, before);
                 return ToolResult::ok({{"order", orderJson(env, t.role == model::TrackInfo::Role::Return)}});
             }));

    registry.tools.push_back(
        make(env,
             {"duplicate_track", "Duplicate a track",
              "Copies a track or return track right after it: its clips and takes, modules, mix, sends, "
              "input and colour, "
              "named with the next number (\"Bass 2\"), not armed or soloed. Automation lanes that drive "
              "only it are "
              "copied for the copy. A return's copy gets no sends. One undo step.",
              object({{"track", id(IdKind::Track, "The track or return track")}}, {"track"}),
              result({{"track", string("The copy's id")}, {"name", string("")}}, {"track", "name"}), changes},
             [&env](const Json& args, CallContext&)
             {
                 const auto t = trackOrReturn(env, args["track"]);
                 const auto copy = env.session.duplicateTrack(t.id);
                 if (copy == 0)
                     throw Problem{env.session.isRecording()
                                       ? "Not while recording."
                                       : "There's no room for another return track (32 at most)."};
                 env.session.flushSnapshot();
                 const auto c = channel(env, formatId(IdKind::Track, copy));
                 return ToolResult::ok({{"track", formatId(IdKind::Track, copy)}, {"name", c.name}},
                                       "Duplicated as " + c.name + ".");
             }));

    registry.tools.push_back(make(
        env,
        {"set_clips", "Rename or recolour clips",
         "Sets clips' name and/or colour (a palette index, 0 to 15; null: they follow their track's colour "
         "again). One "
         "undo step.",
         object({{"clips", ids(IdKind::Clip, "The clips")},
                 {"name", string("Their new name")},
                 {"colour",
                  {{"type", {"integer", "null"}},
                   {"minimum", 0},
                   {"maximum", 15},
                   {"description", "A palette index, or null to follow the track's colour"}}}},
                {"clips"}),
         result({{"clips", arrayOf(clipResultSchema(), "The clips now", std::nullopt)}}, {"clips"}), changes},
        [&env](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> ids;
            for (const auto& value : args["clips"])
                ids.push_back(clip(env, value).id);
            if (!args.contains("name") && !args.contains("colour"))
                throw Problem{"Give a name or a colour to set."};
            if (args.contains("name"))
            {
                const auto name = args["name"].get<std::string>();
                if (juce::String(name).trim().isEmpty())
                    throw Problem{"A clip's name can't be empty."};
                env.session.renameClips(ids, name);
            }
            if (args.contains("colour"))
                env.session.setClipColour(
                    ids, args["colour"].is_null() ? std::nullopt : std::optional(colourArg(args["colour"])));
            env.session.flushSnapshot();
            Json list = Json::array();
            for (const auto id : ids)
                if (const auto found = model::findClip(*env.session.snapshot(), id))
                    list.push_back(clipJson(env, *found->first, false));
            return ToolResult::ok({{"clips", list}});
        }));

    registry.tools.push_back(make(
        env,
        {"copy_clips", "Copy clips to a place",
         "Copies clips to a position on a track (the clipboard's paste, with the place named): clips from "
         "several "
         "tracks keep their rows, on the tracks below it, and their spacing. They cut what they land on and "
         "keep "
         "their takes and fades. Rows past the last track are left out. One undo step.",
         object({{"clips", ids(IdKind::Clip, "The clips")},
                 {"to", position("Where the earliest of them starts")},
                 {"to_track", id(IdKind::Track, "The track for the topmost of them (default: its own)")}},
                {"clips", "to"}),
         result({{"clips", arrayOf(clipResultSchema(), "The copies", std::nullopt)},
                 {"left_out", integer("Rows past the last track, left out")}},
                {"clips", "left_out"}),
         moves},
        [&env](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> ids;
            for (const auto& value : args["clips"])
                ids.push_back(clip(env, value).id);
            const auto clipboard = env.session.copyClips(ids);
            if (clipboard.empty())
                throw Problem{"Nothing to copy."};
            // The topmost copied clip's track, unless told.
            std::uint64_t target = 0;
            if (args.contains("to_track"))
                target = track(env, args["to_track"]).id;
            else
            {
                const auto& tracks = env.session.snapshot()->tracks;
                for (const auto& t : tracks)
                    if (std::any_of(t.clips.begin(), t.clips.end(), [&ids](const auto& c)
                                    { return std::find(ids.begin(), ids.end(), c.id) != ids.end(); }))
                    {
                        target = t.id;
                        break;
                    }
            }
            const auto pasted =
                env.session.pasteClips(clipboard, target, seconds(env, positionArg(env, args["to"])));
            env.session.flushSnapshot();
            Json list = Json::array();
            for (const auto id : pasted.clips)
                if (const auto found = model::findClip(*env.session.snapshot(), id))
                    list.push_back(clipJson(env, *found->first, false));
            return ToolResult::ok({{"clips", list}, {"left_out", pasted.rowsLeftOut}});
        }));
}
} // namespace fd::mcp
