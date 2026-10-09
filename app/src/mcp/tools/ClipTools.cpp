// Tracks and clips (mcp.md §5). Placing a clip cuts what it covers (ADR 0019), as in the
// arrangement.

#include "mcp/tools/Tools.h"

#include "model/Editing.h"

#include <cmath>

namespace fd::mcp
{
using namespace tools;


namespace
{
/// A signed amount of beats: a number, or a length with an optional "-" ("-1 bar").
double signedBeats(ToolEnv& env, const Json& value)
{
    if (value.is_number())
        return value.get<double>();
    if (value.is_string())
    {
        auto text = value.get<std::string>();
        const bool negative = !text.empty() && text.front() == '-';
        if (negative)
            text.erase(0, 1);
        return (negative ? -1.0 : 1.0) * lengthArg(env, text);
    }
    throw Problem{"Give the amount in beats (a number, negative to move earlier) or text such as \"-1 bar\"."};
}

std::vector<std::uint64_t> clipIds(ToolEnv& env, const Json& list)
{
    std::vector<std::uint64_t> out;
    for (const auto& value : list)
        out.push_back(clip(env, value).id);
    return out;
}

Json clipsJson(ToolEnv& env, const std::vector<std::uint64_t>& ids)
{
    env.session.flushSnapshot();
    Json list = Json::array();
    for (auto id : ids)
        if (const auto found = model::findClip(*env.session.snapshot(), id))
            list.push_back(clipJson(env, *found->first, false));
    return list;
}
} // namespace

void addClipTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations adds;
    ToolAnnotations moves;
    moves.destructive = true; // covering clips cuts them (undoable)

    registry.tools.push_back(make(
        env,
        {"add_track", "Add a track",
         "Adds an empty track at the end. Any track takes audio and MIDI clips; MIDI clips sound once the track "
         "has an instrument (add_module).",
         object({{"name", string("Its name (default \"N Track\")")}}),
         result({{"track", string("The new track's id")}}, {"track"}), adds},
        [&env](const Json& args, CallContext&) {
            const auto id = env.session.addTrack(args.value("name", std::string()));
            if (id == 0)
                throw Problem{"The track couldn't be added."};
            return ToolResult::ok({{"track", formatId(IdKind::Track, id)}});
        }));

    registry.tools.push_back(make(
        env,
        {"create_midi_clip", "Create a MIDI clip",
         "Creates a MIDI clip on a track, optionally with notes. No instrument is added: on a track without one "
         "the clip is silent until one is added (add_module). The clip covers what was there.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"start", position("Where the clip starts.")},
                 {"length", length("How long it is (default one bar).")},
                 {"name", string("Its name (default: the track's)")},
                 {"notes", arrayOf(noteInput(), "Notes to put in it (see add_notes)", std::nullopt)}},
                {"track", "start"}),
         result({{"clip", clipResultSchema()}}, {"clip"}),
         moves},
        [&env](const Json& args, CallContext&) {
            const auto t = track(env, args["track"]);
            const double start = positionArg(env, args["start"]);
            const double length = args.contains("length") ? lengthArg(env, args["length"]) : tempo(env).beatsPerBar();
            const double startSeconds = seconds(env, start);
            const auto id = env.session.createMidiClip(t.id, startSeconds, seconds(env, start + length) - startSeconds);
            if (id == 0)
                throw Problem{"The clip couldn't be created."};
            if (args.contains("notes") && !args["notes"].empty())
            {
                env.session.flushSnapshot();
                writeNotes(env, clip(env, formatId(IdKind::Clip, id)), args["notes"], false); // as add_notes
            }
            env.session.flushSnapshot();
            const auto created = clip(env, formatId(IdKind::Clip, id));
            return ToolResult::ok({{"clip", clipJson(env, created, false)}},
                                  t.instrument.empty() ? "The track has no instrument, so the clip is silent until one "
                                                         "is added." : "");
        }));

    const auto insertFile = [&env](bool midi) {
        return [&env, midi](const Json& args, CallContext&) {
            const auto t = track(env, args["track"]);
            const auto file = readableFile(args["path"]);
            const double at = seconds(env, positionArg(env, args["start"]));
            const auto id = midi ? env.session.insertMidiFile(t.id, file, at) : env.session.insertAudioFile(t.id, file, at);
            if (id == 0)
                throw Problem{file.getFullPathName().toStdString() +
                              (midi ? " isn't a MIDI file the DAW can read." : " isn't an audio file the DAW can read.")};
            env.session.flushSnapshot();
            Json r{{"clip", clipJson(env, clip(env, formatId(IdKind::Clip, id)), false)}};
            const bool silent = midi && t.instrument.empty();
            return ToolResult::ok(std::move(r), silent ? "The track has no instrument, so the clip is silent until one "
                                                         "is added." : "");
        };
    };
    registry.tools.push_back(make(env,
                                  {"insert_audio_file", "Insert an audio file",
                                   "Inserts an audio file (WAV, AIFF, FLAC, MP3) as a clip. The clip covers what was there.",
                                   object({{"track", id(IdKind::Track, "The track")},
                                           {"start", position("Where the clip starts.")},
                                           {"path", string("The file's absolute path")}},
                                          {"track", "start", "path"}),
                                   result({{"clip", clipResultSchema()}}, {"clip"}), moves},
                                  insertFile(false)));
    registry.tools.push_back(make(env,
                                  {"insert_midi_file", "Insert a MIDI file",
                                   "Inserts a MIDI file as one clip (all of its tracks merged). No instrument is "
                                   "added.",
                                   object({{"track", id(IdKind::Track, "The track")},
                                           {"start", position("Where the clip starts.")},
                                           {"path", string("The file's absolute path")}},
                                          {"track", "start", "path"}),
                                   result({{"clip", clipResultSchema()}}, {"clip"}),
                                   moves},
                                  insertFile(true)));

    registry.tools.push_back(make(
        env,
        {"move_clips", "Move clips",
         "Moves clips in time by the same amount, and optionally to another track. Moved clips cut what they land on.",
         object({{"clips", ids(IdKind::Clip, "The clips")},
                 {"by", {{"type", {"number", "string"}}, {"description", "Beats, negative to move earlier, or text such as \"-1 bar\""}}},
                 {"to_track", id(IdKind::Track, "Move them to this track")}},
                {"clips"}),
         result({{"clips", arrayOf(clipResultSchema(), "Where they are now", std::nullopt)}}, {"clips"}), moves},
        [&env](const Json& args, CallContext&) {
            const auto ids = clipIds(env, args["clips"]);
            const double by = args.contains("by") ? signedBeats(env, args["by"]) : 0.0;
            const auto target = args.contains("to_track") ? std::optional(track(env, args["to_track"]).id) : std::nullopt;
            const auto snap = env.session.snapshot();
            std::vector<model::ClipEdit> edits;
            for (auto id : ids)
            {
                const auto found = model::findClip(*snap, id);
                const auto& c = *found->first;
                const double start = std::max(0.0, seconds(env, beats(env, c.start) + by));
                edits.push_back({id, target ? *target : snap->tracks[static_cast<size_t>(found->second)].id, start,
                                 c.length, c.offset});
            }
            env.session.applyClipEdits(edits);
            return ToolResult::ok({{"clips", clipsJson(env, ids)}});
        }));

    registry.tools.push_back(make(
        env,
        {"trim_clip", "Trim a clip",
         "Moves a clip's start or end edge, keeping its content in place (like dragging an edge). An audio clip "
         "can't extend past its file.",
         object({{"clip", id(IdKind::Clip, "The clip")},
                 {"start", position("Its new start.")},
                 {"end", position("Its new end.")}},
                {"clip"}),
         result({{"clip", clipResultSchema()}}, {"clip"}), moves},
        [&env](const Json& args, CallContext&) {
            const auto c = clip(env, args["clip"]);
            const auto found = model::findClip(*env.session.snapshot(), c.id);
            double start = c.start, end = c.end(), offset = c.offset;
            if (args.contains("start"))
            {
                const double s = std::max(seconds(env, positionArg(env, args["start"])), c.start - c.offset);
                offset += s - start;
                start = s;
            }
            if (args.contains("end"))
            {
                end = seconds(env, positionArg(env, args["end"]));
                if (c.kind == model::ClipInfo::Kind::Audio && c.sourceLength > 0.0)
                    end = std::min(end, start - offset + c.sourceLength);
            }
            if (!(end - start >= model::minClipLength))
                throw Problem{"The clip's end must come after its start."};
            env.session.applyClipEdits({{c.id, env.session.snapshot()->tracks[static_cast<size_t>(found->second)].id,
                                         start, end - start, offset}});
            return ToolResult::ok({{"clip", clipsJson(env, {c.id}).at(0)}});
        }));

    registry.tools.push_back(make(
        env,
        {"duplicate_clips", "Duplicate clips",
         "Copies clips right after themselves (the group keeps its spacing), as many times as asked.",
         object({{"clips", ids(IdKind::Clip, "The clips")}, {"times", integer("How many copies (default 1)", 1, 64)}},
                {"clips"}),
         result({{"clips", arrayOf(clipResultSchema(), "The copies", std::nullopt)}}, {"clips"}), moves},
        [&env](const Json& args, CallContext&) {
            const auto ids = clipIds(env, args["clips"]);
            const int times = args.value("times", 1);
            const auto snap = env.session.snapshot();
            std::vector<model::ClipEdit> placements;
            for (int k = 1; k <= times; ++k)
                for (auto p : model::duplicatePlacements(*snap, {ids.begin(), ids.end()}))
                {
                    const auto source = model::findClip(*snap, p.clip);
                    p.start = source->first->start + (p.start - source->first->start) * k;
                    placements.push_back(p);
                }
            const auto created = env.session.duplicateClips(placements);
            return ToolResult::ok({{"clips", clipsJson(env, created)}});
        }));

    registry.tools.push_back(make(
        env,
        {"split_clips", "Split clips", "Splits the given clips at a position (clips that don't contain it are left alone).",
         object({{"clips", ids(IdKind::Clip, "The clips")}, {"at", position("Where to split.")}}, {"clips", "at"}),
         result({{"clips", arrayOf(clipResultSchema(), "The new right-hand parts", std::nullopt)}}, {"clips"}), adds},
        [&env](const Json& args, CallContext&) {
            const auto ids = clipIds(env, args["clips"]);
            const auto created = env.session.splitClips(ids, seconds(env, positionArg(env, args["at"])));
            return ToolResult::ok({{"clips", clipsJson(env, created)}},
                                  created.empty() ? "None of the clips contains that position." : "");
        }));

    ToolAnnotations deletes;
    deletes.destructive = true;
    registry.tools.push_back(make(env,
                                  {"delete_clips", "Delete clips", "Deletes clips (undoable, like any edit).",
                                   object({{"clips", ids(IdKind::Clip, "The clips")}}, {"clips"}),
                                   result({{"deleted", integer("How many")}}, {"deleted"}), deletes},
                                  [&env](const Json& args, CallContext&) {
                                      const auto ids = clipIds(env, args["clips"]);
                                      env.session.deleteClips(ids);
                                      return ToolResult::ok({{"deleted", ids.size()}});
                                  }));
}
} // namespace fd::mcp
