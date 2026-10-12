// Tracks and clips (mcp.md §5). Placing a clip cuts what it covers (ADR 0019), as in the
// arrangement.

#include "mcp/tools/Tools.h"

#include "model/ClipLoops.h"
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

/// Seconds written as a plain number before a unit ("10 ms", "0.5s"); nullopt for anything else
/// (a musical length).
std::optional<double> secondsInText(const Json& value)
{
    if (!value.is_string())
        return std::nullopt;
    const auto text = juce::String(value.get<std::string>()).trim().toLowerCase();
    auto numberBefore = [&text](int suffix) -> std::optional<double> {
        const auto n = text.dropLastCharacters(suffix).trim();
        if (n.isNotEmpty() && n.containsOnly("0123456789."))
            return n.getDoubleValue();
        return std::nullopt;
    };
    if (text.endsWith("ms"))
        if (const auto ms = numberBefore(2))
            return *ms / 1000.0;
    if (text.endsWith("s"))
        return numberBefore(1);
    return std::nullopt;
}

/// A fade or crossfade length in seconds: a number of milliseconds, or text such as "10 ms",
/// "0.5 s" or a musical length ("1/16", "1 beat").
double fadeSeconds(ToolEnv& env, const Json& value)
{
    if (value.is_number())
        return std::max(0.0, value.get<double>() / 1000.0);
    if (value.is_string())
    {
        if (const auto s = secondsInText(value))
            return *s;
        return seconds(env, lengthArg(env, value));
    }
    throw Problem{"Give the length in ms (a number) or as text such as \"10 ms\", \"0.5 s\" or \"1/16\"."};
}

/// A loop's length in seconds: beats (a number, "1 bar", "4 beats") or, for audio, "1.5 s".
double loopSeconds(ToolEnv& env, const Json& value)
{
    if (const auto s = secondsInText(value))
        return *s;
    return seconds(env, lengthArg(env, value));
}

/// Seconds as milliseconds for results, to the microsecond.
double msOf(double seconds)
{
    return std::round(seconds * 1e6) / 1e3;
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
         "can't extend past its file unless it loops (loop_clips): a looped clip's edges go anywhere, repeating its loop.",
         object({{"clip", id(IdKind::Clip, "The clip")},
                 {"start", position("Its new start.")},
                 {"end", position("Its new end.")}},
                {"clip"}),
         result({{"clip", clipResultSchema()}}, {"clip"}), moves},
        [&env](const Json& args, CallContext&) {
            auto c = clip(env, args["clip"]);
            const auto found = model::findClip(*env.session.snapshot(), c.id);
            // The same rules as dragging an edge (model::ClipLoops): the end first, then the start.
            const double start = args.contains("start") ? seconds(env, positionArg(env, args["start"])) : c.start;
            const double end = args.contains("end") ? seconds(env, positionArg(env, args["end"])) : c.end();
            if (!(end - start >= model::minClipLength))
                throw Problem{"The clip's end must come after its start."};
            const auto ended = model::trimmedEnd(c, end);
            c.length = ended.length;
            const auto placed = model::trimmedStart(c, start);
            env.session.applyClipEdits({{c.id, env.session.snapshot()->tracks[static_cast<size_t>(found->second)].id,
                                         placed.start, placed.length, placed.offset}});
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

    // Fades and crossfades (fades.md §7).
    ToolAnnotations fades;
    fades.idempotent = true;
    registry.tools.push_back(make(
        env,
        {"set_fades", "Set clip fades",
         "Sets audio clips' fade-in and fade-out (a clip with takes: at its outer edges). Lengths are times, not beats: "
         "a number of milliseconds, or text such as \"10 ms\", \"0.5 s\" or a musical length (\"1/16\"); 0 removes a "
         "fade. A bend (-1 to 1) curves it as an automation segment bends. Cut edges keep a 4 ms edge fade (see "
         "set_edge_fades).",
         object({{"clips", ids(IdKind::Clip, "The audio clips")},
                 {"in", length("The fade-in's length")},
                 {"out", length("The fade-out's length")},
                 {"inBend", number("The fade-in's bend, -1 to 1 (default: as it is)")},
                 {"outBend", number("The fade-out's bend, -1 to 1")}},
                {"clips"}),
         result({{"clips", arrayOf(clipResultSchema(), "The clips", std::nullopt)}}, {"clips"}), fades},
        [&env](const Json& args, CallContext&) {
            const auto ids = clipIds(env, args["clips"]);
            std::optional<model::Fade> in, out;
            for (const auto id : ids)
            {
                const auto c = clip(env, formatId(IdKind::Clip, id));
                if (c.kind != model::ClipInfo::Kind::Audio)
                    throw Problem{"Only audio clips fade: " + c.name + " is a MIDI clip."};
                if (args.contains("in") || args.contains("inBend"))
                    in = model::Fade{args.contains("in") ? fadeSeconds(env, args["in"]) : c.fadeIn.length,
                                     args.contains("inBend") ? std::clamp(args["inBend"].get<float>(), -1.0f, 1.0f) : c.fadeIn.bend};
                if (args.contains("out") || args.contains("outBend"))
                    out = model::Fade{args.contains("out") ? fadeSeconds(env, args["out"]) : c.fadeOut.length,
                                      args.contains("outBend") ? std::clamp(args["outBend"].get<float>(), -1.0f, 1.0f) : c.fadeOut.bend};
                env.session.setFades({id}, in, out);
            }
            return ToolResult::ok({{"clips", clipsJson(env, ids)}});
        }));

    registry.tools.push_back(make(
        env,
        {"crossfade", "Crossfade clips",
         "Crossfades every join between the given audio clips that meet: the clips stay edge to edge and each plays its "
         "spare audio past the join while the crossfade lasts, centred on it unless only one side has spare audio. "
         "Shapes: \"equal power\" (the default, for different material), \"equal gain\" (for the same material) and "
         "\"s-curve\". \"placement\" sits it \"before\" the join, \"centred\" on it or \"after\" it, where the spare "
         "audio allows. A join that has a crossfade keeps it (taking a new shape, length or placement if given); with "
         "\"remove\": true, its crossfade goes. A join without spare audio, or a seamless one, says why.",
         object({{"clips", ids(IdKind::Clip, "The audio clips on both sides of each join")},
                 {"length", length("The crossfade's length (default 10 ms): ms, or text such as \"20 ms\"")},
                 {"shape", string("\"equal power\", \"equal gain\" or \"s-curve\"")},
                 {"placement", string("\"before\", \"centred\" or \"after\" the join (default: centred where it fits)")},
                 {"remove", boolean("Remove the joins' crossfades instead")}},
                {"clips"}),
         result({{"joins", arrayOf(result({{"out", string("The clip before the join")}, {"in", string("The clip after it")}}),
                                   "Each join between the clips", std::nullopt)}},
                {"joins"}),
         fades},
        [&env](const Json& args, CallContext&) {
            const auto ids = clipIds(env, args["clips"]);
            std::optional<model::CrossfadeShape> shape;
            if (args.contains("shape"))
            {
                shape = model::shapeFromName(args["shape"].get<std::string>());
                if (!shape)
                    throw Problem{"The shape is \"equal power\", \"equal gain\" or \"s-curve\"."};
            }
            std::optional<model::CrossfadePlacement> placement;
            if (args.contains("placement"))
            {
                const auto p = juce::String(args["placement"].get<std::string>()).trim().toLowerCase();
                placement = p == "before" ? std::optional(model::CrossfadePlacement::Before)
                            : p == "after" ? std::optional(model::CrossfadePlacement::After)
                            : p == "centred" || p == "centered" || p == "centre" || p == "center"
                                ? std::optional(model::CrossfadePlacement::Centred)
                                : std::nullopt;
                if (!placement)
                    throw Problem{"The placement is \"before\", \"centred\" or \"after\" the join."};
            }
            static constexpr const char* placementNames[] = {"before", "centred", "after"};
            Json joins = Json::array();
            if (args.value("remove", false))
            {
                env.session.flushSnapshot();
                for (const auto id : ids)
                    if (const auto c = clip(env, formatId(IdKind::Clip, id)); c.crossfadeOut)
                        if (env.session.setCrossfade(id, std::nullopt, "Remove crossfade"))
                            joins.push_back({{"out", formatId(IdKind::Clip, id)}, {"removed", true}});
                return ToolResult::ok({{"joins", joins}}, joins.empty() ? "None of the clips had a crossfade." : "");
            }
            const double length = args.contains("length") ? fadeSeconds(env, args["length"]) : model::kCrossfadeSeconds;
            // A join with a crossfade takes a new length when given (kept centred where it was).
            if (args.contains("length"))
            {
                env.session.flushSnapshot();
                for (const auto id : ids)
                    if (const auto c = clip(env, formatId(IdKind::Clip, id)); c.crossfadeOut)
                        env.session.setCrossfade(id, model::Crossfade{length, c.crossfadeOut->from + (c.crossfadeOut->length - length) * 0.5,
                                                                      shape.value_or(c.crossfadeOut->shape)});
            }
            std::string why;
            for (auto r : env.session.crossfade(ids, length, shape))
            {
                Json j{{"out", formatId(IdKind::Clip, r.out)}, {"in", formatId(IdKind::Clip, r.in)}};
                // Placed as asked, as far as the spare audio lets it.
                if (r.crossfade && placement)
                {
                    const auto wanted = model::placementFrom(*placement, r.crossfade->length);
                    env.session.setCrossfade(r.out, model::Crossfade{r.crossfade->length, wanted, r.crossfade->shape});
                    env.session.flushSnapshot();
                    r.crossfade = clip(env, formatId(IdKind::Clip, r.out)).crossfadeOut;
                    if (r.crossfade && model::placementOf(*r.crossfade) != placement)
                        why = "Not enough spare audio to sit the crossfade " + std::string(placementNames[static_cast<int>(*placement)]) +
                              (*placement == model::CrossfadePlacement::Centred ? " on" : "") + " the join; it sits as near as it can.";
                }
                if (r.crossfade)
                {
                    const auto at = model::placementOf(*r.crossfade);
                    j["crossfade"] = {{"ms", msOf(r.crossfade->length)},
                                      {"startMs", msOf(r.crossfade->from)},
                                      {"placement", at ? Json(placementNames[static_cast<int>(*at)]) : Json(nullptr)},
                                      {"shape", model::shapeName(r.crossfade->shape)}};
                }
                else
                {
                    j["why"] = r.why;
                    why = r.why;
                }
                joins.push_back(std::move(j));
            }
            if (joins.empty())
                throw Problem{"None of these clips meet another of them: select clips on both sides of a join."};
            return ToolResult::ok({{"joins", joins}}, why);
        }));

    registry.tools.push_back(make(env,
                                  {"set_edge_fades", "Edge fades on or off",
                                   "Turns the project's edge fades on or off: every cut edge of an audio clip (trimmed or "
                                   "split, not a file's own start or end) fades 4 ms so edits don't click. Off for "
                                   "sample-accurate cuts, such as a drum hit at its attack.",
                                   object({{"on", boolean("On (the default for new projects) or off")}}, {"on"}),
                                   result({{"on", boolean("")}}, {"on"}), fades},
                                  [&env](const Json& args, CallContext&) {
                                      env.session.setEdgeFades(args["on"].get<bool>());
                                      return ToolResult::ok({{"on", env.session.edgeFades()}});
                                  }));

    ToolAnnotations mutes;
    mutes.idempotent = true;
    registry.tools.push_back(make(env,
                                  {"mute_clips", "Mute or unmute clips",
                                   "Mutes clips: they keep their place and play nothing (a clip with takes, all of it). "
                                   "With \"muted\": false, unmutes them.",
                                   object({{"clips", ids(IdKind::Clip, "The clips")},
                                           {"muted", boolean("Muted (default true) or not")}},
                                          {"clips"}),
                                   result({{"clips", arrayOf(clipResultSchema(), "The clips", std::nullopt)}}, {"clips"}), mutes},
                                  [&env](const Json& args, CallContext&) {
                                      const auto ids = clipIds(env, args["clips"]);
                                      env.session.setClipsMuted(ids, args.value("muted", true));
                                      return ToolResult::ok({{"clips", clipsJson(env, ids)}});
                                  }));

    // Clip loops (ADR 0048, clip-loops.md §11).
    ToolAnnotations loops;
    loops.idempotent = true;
    registry.tools.push_back(make(
        env,
        {"loop_clips", "Loop clips",
         "Loops clips: each plays a stretch of its content again and again for as long as it is, and its edges then go "
         "anywhere (trim_clip). Without \"start\" or \"length\", each loops what it shows now, so nothing moves. "
         "\"start\" is where the loop starts in the clip's content (beats from the content's start, or \"2.1.1\" for its "
         "second bar) and \"length\" how long it is (beats, \"1 bar\", \"4 beats\"; for audio also \"1.5 s\"). With "
         "\"loop\": false, the repeats go: each clip ends where its first lap ends. A clip with takes, or an audio "
         "clip stretched to the song's tempo, can't loop.",
         object({{"clips", ids(IdKind::Clip, "The clips")},
                 {"loop", boolean("Loop (default true) or not")},
                 {"start", position("Where the loop starts in the clip's content")},
                 {"length", {{"type", {"number", "string"}}, {"description", "The loop's length: beats, \"1 bar\", or for audio \"1.5 s\""}}}},
                {"clips"}),
         result({{"clips", arrayOf(clipResultSchema(), "The clips", std::nullopt)}}, {"clips"}), loops},
        [&env](const Json& args, CallContext&) {
            const auto ids = clipIds(env, args["clips"]);
            const bool on = args.value("loop", true);
            const auto snapshot = env.session.snapshot();
            // Every clip can loop, or nothing changes.
            if (on)
                for (const auto id : ids)
                    if (const auto found = model::findClip(*snapshot, id); found && !found->first->loop.on())
                        if (const auto why = model::whyCantLoop(*found->first, snapshot->tracks[static_cast<size_t>(found->second)]);
                            !why.empty())
                            throw Problem{why};
            if (!on || (!args.contains("start") && !args.contains("length")))
                env.session.setClipsLooping(ids, on);
            else
                for (const auto id : ids)
                {
                    const auto c = clip(env, formatId(IdKind::Clip, id));
                    auto loop = c.loop.on() ? c.loop : model::loopOn(c).loop;
                    if (args.contains("start"))
                        loop.start = seconds(env, positionArg(env, args["start"]));
                    if (args.contains("length"))
                        loop.length = loopSeconds(env, args["length"]);
                    if (!(loop.length >= model::minClipLength))
                        throw Problem{"A loop must be longer than that."};
                    env.session.setClipLoop(id, loop);
                }
            env.session.flushSnapshot();
            return ToolResult::ok({{"clips", clipsJson(env, ids)}});
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
