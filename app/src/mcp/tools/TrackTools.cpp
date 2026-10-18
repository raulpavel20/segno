// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md §11): names and
// colours, deleting with what goes along, moving, duplicating, copying clips to a place.

#include "mcp/tools/Tools.h"
#include "model/Groups.h"

#include <map>

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
         "Sets a track's, group's or return track's name and/or colour (a palette index, 0 to 15). Its clips "
         "keep their colours: a new clip takes its track's colour once, when it's made. For a group, "
         "colourMembers gives its tracks and groups, and their clips, its colour once (after any colour "
         "set here). One undo step.",
         object({{"track", id(IdKind::Track, "The track, group or return track")},
                 {"name", string("Its new name")},
                 {"colour", integer("Its colour: a palette index", 0, 15)},
                 {"colourMembers", boolean("A group: its tracks and their clips take its colour")}},
                {"track"}),
         result({{"track", string("Its id")}, {"name", string("")}, {"colour", integer("")}},
                {"track", "name", "colour"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto t = trackOrReturn(env, args["track"]);
            const bool members = args.value("colourMembers", false);
            if (!args.contains("name") && !args.contains("colour") && !members)
                throw Problem{"Give a name or a colour to set."};
            if (members && !t.isGroup())
                throw Problem{t.name + " isn't a group: colourMembers is a group's."};
            const engine::Session::EditGroup step(env.session, "Track colour");
            if (args.contains("name"))
            {
                const auto name = args["name"].get<std::string>();
                if (juce::String(name).trim().isEmpty())
                    throw Problem{"A track's name can't be empty."};
                env.session.renameTrack(t.id, name);
            }
            if (args.contains("colour"))
                env.session.setTrackColour(t.id, colourArg(args["colour"]));
            if (members)
                env.session.colourMembers(t.id);
            env.session.flushSnapshot();
            const auto now = channel(env, args["track"]);
            return ToolResult::ok(
                {{"track", formatId(IdKind::Track, now.id)}, {"name", now.name}, {"colour", now.colour}});
        }));

    registry.tools.push_back(make(
        env,
        {"delete_tracks", "Delete tracks",
         "Deletes tracks, groups or return tracks with what only made sense with them: their clips and "
         "takes, "
         "the automation lanes that drive only them (their parameters leave lanes they share), the "
         "sidechains "
         "they feed "
         "(back to Off) and, for a return, every send to it. A group goes with everything in it; a group "
         "left empty goes too. Not while recording. Undoable.",
         object({{"tracks", ids(IdKind::Track, "The tracks, groups or return tracks")}}, {"tracks"}),
         result({{"deleted", integer("How many")}}, {"deleted"}), deletes},
        [&env](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> ids;
            for (const auto& value : args["tracks"])
                ids.push_back(trackOrReturn(env, value).id);
            int deleted = 0;
            for (const auto id : ids)
            {
                env.session.flushSnapshot();
                if (env.session.snapshot()->channel(id) == nullptr)
                    continue; // gone with its group

                std::string why;
                if (!env.session.deleteTrack(id, &why))
                    throw Problem{why.empty() ? std::string("A track couldn't be deleted.") : why};
                ++deleted;
            }
            return ToolResult::ok({{"deleted", deleted}});
        }));

    registry.tools.push_back(make(
        env,
        {"move_track", "Move a track",
         "Moves a track or group right above another track or group, in its group; or to the end of "
         "`group` (a group's id; null for no group); or to the end of the tracks and lanes. A group "
         "moves with everything in it, never into itself; a group left empty goes. Nothing else moves, "
         "the automation lanes that were after it included (move_automation_lane moves a lane). Or a "
         "return track among the returns. One undo step.",
         object({{"track", id(IdKind::Track, "The track, group or return track")},
                 {"before", id(IdKind::Track,
                               "The track, group (or return) to put it above, in its group; leave it out "
                               "for the end")},
                 {"group",
                  {{"type", {"string", "null"}},
                   {"description", "Without before: the group to put it at the end of; null for none"}}}},
                {"track"}),
         result({{"order", arrayOf(string("A track's id"), "The tracks (or returns) in their order now",
                                   std::nullopt)}},
                {"order"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto t = trackOrReturn(env, args["track"]);
            const bool isReturn = t.role == model::TrackInfo::Role::Return;
            std::uint64_t before = 0;
            if (args.contains("before"))
            {
                const auto b = trackOrReturn(env, args["before"]);
                if ((b.role == model::TrackInfo::Role::Return) != isReturn)
                    throw Problem{"A track moves among the tracks, a return track among the returns."};
                before = b.id;
            }
            // In a group (groups.md §13): before's; else `group`'s end; else the end of all.
            std::optional<std::uint64_t> into;
            if (!isReturn && before == 0)
            {
                into = 0;
                if (args.contains("group") && !args["group"].is_null())
                {
                    const auto g = channel(env, args["group"]);
                    if (!g.isGroup())
                        throw Problem{g.name + " isn't a group."};
                    into = g.id;
                }
            }
            if (into && *into != 0 && t.isGroup() &&
                (*into == t.id ||
                 model::isInGroup(*env.session.snapshot(), channel(env, args["group"]), t.id)))
                throw Problem{"A group can't go inside itself."};
            env.session.moveTrack(t.id, before, into);
            return ToolResult::ok({{"order", orderJson(env, t.role == model::TrackInfo::Role::Return)}});
        }));

    registry.tools.push_back(
        make(env,
             {"duplicate_track", "Duplicate a track",
              "Copies a track, group or return track right after it: its clips and takes, modules, mix, "
              "sends, input and colour, "
              "named with the next number (\"Bass 2\"), not armed or soloed. Automation lanes that drive "
              "only it are "
              "copied for the copy. A group's copy holds copies of what it holds. A return's copy gets no "
              "sends. One undo step.",
              object({{"track", id(IdKind::Track, "The track, group or return track")}}, {"track"}),
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

    // Groups (ADR 0072, groups.md §13).
    registry.tools.push_back(make(
        env,
        {"group_tracks", "Group tracks",
         "Puts tracks and groups into a new group, a channel that sums them through its modules, volume and "
         "pan: at the topmost one's place, in its group, the others gathered after it in their order. One "
         "inside another given goes with that one. Named \"N Group\" (or name), in the topmost's colour (or "
         "colour). A group left empty goes. Not return tracks or the master. One undo step.",
         object({{"tracks", ids(IdKind::Track, "The tracks and groups")},
                 {"name", string("Its name (default: the next \"N Group\")")},
                 {"colour", integer("Its colour: a palette index", 0, 15)}},
                {"tracks"}),
         result({{"group", string("Its id")}, {"name", string("")}}, {"group", "name"}), changes},
        [&env](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> ids;
            for (const auto& value : args["tracks"])
                ids.push_back(trackOrGroup(env, value).id);
            if (ids.empty())
                throw Problem{"Give the tracks to group."};
            if (env.session.isRecording())
                throw Problem{"Not while recording."};
            std::uint64_t group = 0;
            {
                const engine::Session::EditGroup step(env.session, "Group tracks");
                const auto name = args.value("name", std::string());
                group = env.session.groupTracks(ids, juce::String(name).trim().toStdString());
                if (group != 0 && args.contains("colour"))
                    env.session.setTrackColour(group, colourArg(args["colour"]));
            }
            if (group == 0)
                throw Problem{"The tracks couldn't be grouped."};
            env.session.flushSnapshot();
            const auto g = channel(env, formatId(IdKind::Track, group));
            return ToolResult::ok({{"group", formatId(IdKind::Track, group)}, {"name", g.name}});
        }));

    registry.tools.push_back(make(
        env,
        {"ungroup_tracks", "Ungroup tracks",
         "Ungroups a group: what it holds takes its place, in order, in its group; it goes with its modules, "
         "mix and sends and the automation lanes that drive only it, so whatever its modules did to the "
         "sound "
         "is gone. One undo step.",
         object({{"group", id(IdKind::Track, "The group")}}, {"group"}),
         result(
             {{"tracks", arrayOf(string("A track's or group's id"), "What it held, in order", std::nullopt)}},
             {"tracks"}),
         deletes},
        [&env](const Json& args, CallContext&)
        {
            const auto g = channel(env, args["group"]);
            if (!g.isGroup())
                throw Problem{g.name + " isn't a group."};
            const auto freed = env.session.ungroupTracks(g.id);
            if (freed.empty())
                throw Problem{env.session.isRecording() ? "Not while recording."
                                                        : "It couldn't be ungrouped."};
            Json list = Json::array();
            for (const auto id : freed)
                list.push_back(formatId(IdKind::Track, id));
            const auto modules = g.devices.size();
            return ToolResult::ok({{"tracks", list}}, modules == 0   ? std::string()
                                                      : modules == 1 ? std::string("Its module went with it.")
                                                                     : "Its " + std::to_string(modules) +
                                                                           " modules went with it.");
        }));

    registry.tools.push_back(make(
        env,
        {"set_clips", "Rename or recolour clips",
         "Sets clips' name and/or colour (a palette index, 0 to 15; null: each takes its track's colour now, "
         "and keeps it if the track's changes later). A clip's takes get its colour too. One undo step.",
         object({{"clips", ids(IdKind::Clip, "The clips")},
                 {"name", string("Their new name")},
                 {"colour",
                  {{"type", {"integer", "null"}},
                   {"minimum", 0},
                   {"maximum", 15},
                   {"description", "A palette index, or null for each clip's track's colour now"}}}},
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
            if (args.contains("colour") && !args["colour"].is_null())
                env.session.setClipColour(ids, colourArg(args["colour"]));
            else if (args.contains("colour"))
            {
                // Each its track's colour, once (ADR 0043).
                const auto& snap = *env.session.snapshot();
                std::map<int, std::vector<std::uint64_t>> byColour;
                for (const auto id : ids)
                    if (const auto found = model::findClip(snap, id))
                        byColour[snap.tracks[static_cast<std::size_t>(found->second)].colour].push_back(id);
                for (const auto& [colour, clips] : byColour)
                    env.session.setClipColour(clips, colour);
            }
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
