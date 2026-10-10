// Recording (M2 step C5, docs/design/recording.md §12): inputs, arming and recording passes.

#include "mcp/tools/Tools.h"

#include "model/Takes.h"

namespace fd::mcp
{
using namespace tools;

namespace
{
model::TrackInfo clipTrack(ToolEnv& env, const Json& value)
{
    const auto t = channel(env, value);
    if (!t.holdsClips())
        throw Problem{t.name + " is a " + kindName(t) + ": only tracks record."};
    return t;
}

std::optional<model::CountIn> countInArg(const Json& value)
{
    const auto v = value.get<std::string>();
    if (v == "off")
        return model::CountIn::Off;
    if (v == "1 beat")
        return model::CountIn::OneBeat;
    if (v == "2 beats")
        return model::CountIn::TwoBeats;
    if (v == "1 bar")
        return model::CountIn::OneBar;
    if (v == "2 bars")
        return model::CountIn::TwoBars;
    throw Problem{"countIn is \"off\", \"1 beat\", \"2 beats\", \"1 bar\" or \"2 bars\"."};
}

std::uint64_t takeArg(const model::TrackInfo& track, const Json& value)
{
    const auto id = parseId(value, IdKind::Take);
    if (!id || track.take(*id) == nullptr)
        throw Problem{value.dump() + " isn't one of " + track.name + "'s takes (get_takes lists them)."};
    return *id;
}

/// A track's takes (ADR 0032): per clip with takes, and flat. `onlyStack` (non-zero) limits it to
/// one clip's.
Json takesJson(ToolEnv& env, std::uint64_t trackId, std::uint64_t onlyStack = 0)
{
    env.session.flushSnapshot();
    const auto* t = env.session.snapshot()->channel(trackId);
    const auto tm = tempo(env);
    Json takes = Json::array(), clips = Json::array();
    auto takeJson = [&](const model::TakeInfo& take) {
        Json clipList = Json::array();
        for (const auto& c : take.clips)
        {
            Json used = Json::array();
            for (const auto& [from, to] : model::inUse(c, t->clips))
                used.push_back({{"from", positionJson(tm.beatsForSeconds(from), tm)}, {"to", positionJson(tm.beatsForSeconds(to), tm)}});
            Json clip{{"kind", c.kind == model::ClipInfo::Kind::Midi ? "midi" : "audio"},
                      {"start", positionJson(tm.beatsForSeconds(c.start), tm)},
                      {"end", positionJson(tm.beatsForSeconds(c.end()), tm)},
                      {"used", std::move(used)}};
            if (!c.sourcePath.empty())
                clip["file"] = c.sourcePath;
            else
                clip["notes"] = c.notes.size();
            clipList.push_back(std::move(clip));
        }
        return Json{{"id", formatId(IdKind::Take, take.id)},
                    {"name", take.name},
                    {"playing", model::isUsed(take, t->clips)},
                    {"clips", std::move(clipList)}};
    };
    if (t != nullptr)
        for (const auto& s : t->stacks)
        {
            if (onlyStack != 0 && s.id != onlyStack)
                continue;
            Json list = Json::array(), parts = Json::array();
            for (const auto& take : s.takes)
            {
                list.push_back(takeJson(take));
                takes.push_back(list.back());
            }
            for (const auto part : s.parts)
                parts.push_back(formatId(IdKind::Clip, part));
            const auto playing = model::playingTake(s, t->clips);
            clips.push_back({{"clip", s.parts.empty() ? Json(nullptr) : Json(formatId(IdKind::Clip, s.parts.front()))},
                             {"parts", std::move(parts)},
                             {"start", positionJson(tm.beatsForSeconds(s.start), tm)},
                             {"end", positionJson(tm.beatsForSeconds(s.end), tm)},
                             {"open", s.open},
                             {"playing", playing ? Json(formatId(IdKind::Take, s.takes[*playing].id))
                                         : s.parts.empty() ? Json(nullptr) : Json("comp")},
                             {"takes", std::move(list)}});
        }
    return {{"track", formatId(IdKind::Track, trackId)}, {"clips", std::move(clips)}, {"takes", std::move(takes)}};
}

Json trackInputResult(ToolEnv& env, std::uint64_t track)
{
    env.session.flushSnapshot();
    const auto* t = env.session.snapshot()->channel(track);
    return {{"track", formatId(IdKind::Track, track)},
            {"name", t != nullptr ? t->name : std::string()},
            {"input", t != nullptr ? inputJson(t->input) : Json(nullptr)},
            {"inputs", inputsJson(env)}};
}
} // namespace

void addRecordingTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations transport;

    registry.tools.push_back(make(
        env,
        {"set_track_input", "Set a track's inputs",
         "Sets what a track listens to and records: an audio input (by name, as get_project's inputs list them; null "
         "for none), a MIDI input (\"all\", \"keyboard\" for the computer keyboard, a device's name, or null), a MIDI "
         "channel (\"all\" or 1-16), monitoring (\"auto\": hear the input while armed; \"in\": always; \"off\": never), "
         "and what it records (\"midi\", \"audio\", or \"auto\": MIDI with an instrument, else audio). A track records "
         "one kind at a time. One undo step per setting.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"audioInput", {{"type", {"string", "null"}}, {"description", "An audio input's name, or null for none"}}},
                 {"midiInput", {{"type", {"string", "null"}}, {"description", "\"all\", \"keyboard\", a device's name, or null"}}},
                 {"channel", {{"type", {"string", "integer"}}, {"description", "\"all\" or 1-16"}}},
                 {"monitor", string("\"auto\", \"in\" or \"off\"")},
                 {"records", string("\"midi\", \"audio\" or \"auto\"")}},
                {"track"}),
         result({{"track", string("")}, {"input", result({{"armed", boolean("")}})}}), changes},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            const auto t = clipTrack(env, args["track"]);
            if (args.contains("audioInput"))
            {
                const auto& v = args["audioInput"];
                const auto name = v.is_null() ? std::string() : v.get<std::string>();
                const auto inputs = s.audioInputs();
                if (!name.empty() && std::none_of(inputs.begin(), inputs.end(), [&](const auto& i) { return i.id == name; }))
                    throw Problem{"There's no audio input \"" + name + "\" (get_project lists the inputs)."};
                s.setAudioInput(t.id, name);
            }
            if (args.contains("midiInput"))
            {
                const auto& v = args["midiInput"];
                const auto name = v.is_null() ? std::string() : v.get<std::string>();
                const auto inputs = s.midiInputs();
                if (!name.empty() && std::none_of(inputs.begin(), inputs.end(), [&](const auto& i) { return i.id == name; }))
                    throw Problem{"There's no MIDI input \"" + name + "\" (get_project lists the inputs)."};
                s.setMidiInput(t.id, name);
            }
            if (args.contains("channel"))
            {
                const auto& v = args["channel"];
                const int channel = v.is_string() && v.get<std::string>() == "all" ? 0 : v.is_number_integer() ? v.get<int>() : -1;
                if (channel < 0 || channel > 16)
                    throw Problem{"channel is \"all\" or 1-16."};
                s.setMidiChannel(t.id, channel);
            }
            if (args.contains("monitor"))
            {
                const auto m = args["monitor"].get<std::string>();
                if (m != "auto" && m != "in" && m != "off")
                    throw Problem{"monitor is \"auto\", \"in\" or \"off\"."};
                s.setMonitor(t.id, m == "in" ? model::Monitor::In : m == "off" ? model::Monitor::Off : model::Monitor::Auto);
            }
            if (args.contains("records"))
            {
                const auto r = args["records"].get<std::string>();
                if (r != "midi" && r != "audio" && r != "auto")
                    throw Problem{"records is \"midi\", \"audio\" or \"auto\"."};
                s.setRecordKind(t.id, r == "auto"    ? std::nullopt
                                      : r == "audio" ? std::optional(model::RecordKind::Audio)
                                                     : std::optional(model::RecordKind::Midi));
            }
            return ToolResult::ok(trackInputResult(env, t.id), t.name + "'s inputs are set.");
        }));

    registry.tools.push_back(make(
        env,
        {"arm_tracks", "Arm tracks for recording",
         "Arms tracks (or disarms them with on: false). Armed tracks record their input when recording starts. With "
         "exclusive, every other track is disarmed. Not an undo step. Without any armed track, recording records the "
         "track selected for live MIDI.",
         object({{"tracks", ids(IdKind::Track, "The tracks")},
                 {"on", boolean("Arm (default) or disarm")},
                 {"exclusive", boolean("Disarm every other track")}},
                {"tracks"}),
         result({{"armed", arrayOf(string(""), "The armed tracks", 0)}}), changes, false},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            const bool on = args.value("on", true);
            bool exclusive = args.value("exclusive", false);
            for (const auto& v : args["tracks"])
            {
                const auto t = clipTrack(env, v);
                s.setArmed(t.id, on, exclusive);
                exclusive = false; // only the first clears the others
            }
            s.flushSnapshot();
            Json armed = Json::array();
            for (const auto& t : s.snapshot()->tracks)
                if (t.input.armed)
                    armed.push_back(formatId(IdKind::Track, t.id));
            return ToolResult::ok({{"armed", armed}}, std::to_string(armed.size()) + " tracks armed.");
        }));

    registry.tools.push_back(make(
        env,
        {"record", "Record",
         "Starts recording the armed tracks from the playhead (or from \"from\"), as the Record button does: each "
         "track records its input (audio into the project's Recordings folder, or MIDI). With bars, it stops by "
         "itself after that many bars; otherwise stop ends it. countIn (\"off\", \"1 beat\", \"2 beats\", \"1 bar\", "
         "\"2 bars\") applies to this recording. While looping, only the loop records (a lead-in before it "
         "is heard), a take per lap. Each pass is one undo step. "
         "While recording, calling it again punches out and keeps playing.",
         object({{"from", position("Where to start (default: the playhead)")},
                 {"bars", number("Stop after this many bars")},
                 {"countIn", string("The count-in for this recording")}}),
         result({{"recording", boolean("")}, {"tracks", arrayOf(string(""), "The tracks recording", 0)}}), transport, false},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            if (s.isRecording())
            {
                s.record();
                return ToolResult::ok({{"recording", false}, {"tracks", Json::array()}}, "Punched out; still playing.");
            }
            if (args.contains("from"))
                s.setPosition(seconds(env, positionArg(env, args["from"])));
            const auto previous = s.countIn();
            if (args.contains("countIn"))
                s.setCountIn(*countInArg(args["countIn"]));
            s.flushSnapshot();
            Json tracks = Json::array();
            for (const auto& t : s.snapshot()->tracks)
                if (t.input.isArmed())
                    tracks.push_back(formatId(IdKind::Track, t.id));
            if (args.contains("bars"))
            {
                const auto tm = tempo(env);
                const double bars = args["bars"].get<double>();
                if (bars <= 0.0)
                    throw Problem{"bars must be more than 0."};
                const double start = s.position();
                s.recordUntil(tm.secondsForBeats(tm.beatsForSeconds(start) + bars * tm.beatsPerBar()));
            }
            else
                s.record();
            s.setCountIn(previous); // read when recording starts: the user's setting stays
            return ToolResult::ok({{"recording", true}, {"tracks", tracks}},
                                  tracks.empty() ? "Recording, but no track is armed (arm_tracks)."
                                                 : "Recording " + std::to_string(tracks.size()) + " tracks.");
        }));

    const auto takesResult =
        result({{"track", string("")},
                {"clips", arrayOf(result({{"clip", {{"type", {"string", "null"}}}}, {"parts", arrayOf(string(""), "", 0)}}), "", 0)},
                {"takes", arrayOf(result({{"id", string("")}, {"name", string("")}}), "", 0)}},
               {"track", "clips", "takes"});
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations removes;
    removes.destructive = true;

    registry.tools.push_back(make(
        env,
        {"get_takes", "List takes",
         "Lists the takes of a clip, or of every clip on a track: every recording pass and loop lap is kept as a take "
         "of the clip it made (ADR 0032). Per clip with takes: its parts (the clips playing from them; several when it "
         "plays more than one take), its range, whether they're shown, the take it plays (or \"comp\"), and its takes. "
         "Each take: its id, name, whether the clip plays it, and its clips (where, the audio file or the number "
         "of notes, and which parts are played). \"takes\" also lists every take of the track, flat. To compare "
         "takes, render each one's time range (render) and measure it.",
         object({{"track", id(IdKind::Track, "The track")}, {"clip", id(IdKind::Clip, "A clip: just its takes")}}),
         takesResult, reads, false},
        [&env](const Json& args, CallContext&) {
            std::uint64_t only = 0;
            model::TrackInfo t;
            if (args.contains("clip"))
            {
                const auto c = clip(env, args["clip"]);
                const auto found = model::findClip(*env.session.snapshot(), c.id);
                t = env.session.snapshot()->tracks.at(static_cast<size_t>(found->second));
                if (c.stack == 0)
                    throw Problem{formatId(IdKind::Clip, c.id) + " has no takes."};
                only = c.stack;
            }
            else if (args.contains("track"))
                t = clipTrack(env, args["track"]);
            else
                throw Problem{"Give a track or a clip."};
            const auto j = takesJson(env, t.id, only);
            return ToolResult::ok(j, t.name + ": " + std::to_string(j["clips"].size()) + " clips with " +
                                         std::to_string(j["takes"].size()) + " takes.");
        }));

    registry.tools.push_back(make(
        env,
        {"use_take", "Use a take",
         "Makes a clip play one of its takes, across the clip's range (cutting what's there). One undo step.",
         object({{"track", id(IdKind::Track, "The track")}, {"take", id(IdKind::Take, "The take")}}, {"track", "take"}),
         takesResult, ToolAnnotations{}},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            const auto take = takeArg(t, args["take"]);
            env.session.useTake(t.id, take);
            return ToolResult::ok(takesJson(env, t.id), "Playing " + formatId(IdKind::Take, take) + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"set_take", "Rename a take",
         "Renames a take. One undo step.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"take", id(IdKind::Take, "The take")},
                 {"name", string("Its new name")},
},
                {"track", "take"}),
         takesResult, changes},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            const auto take = takeArg(t, args["take"]);
            if (args.contains("name"))
                env.session.renameTake(t.id, take, args["name"].get<std::string>());
            return ToolResult::ok(takesJson(env, t.id), "Done.");
        }));

    registry.tools.push_back(make(
        env,
        {"move_takes_to_tracks", "Takes onto new tracks",
         "Puts takes onto new tracks right after the track, each copying its modules and mix and playing one take as a "
         "plain clip (for doubles and layers). copy (default true) keeps them on this track too. One undo step.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"takes", ids(IdKind::Take, "The takes")},
                 {"copy", boolean("Keep them on this track too (default true)")}},
                {"track", "takes"}),
         result({{"tracks", arrayOf(string(""), "The new tracks", 0)}}), ToolAnnotations{}},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            std::vector<std::uint64_t> takes;
            for (const auto& v : args["takes"])
                takes.push_back(takeArg(t, v));
            const auto made = env.session.takesToTracks(t.id, takes, !args.value("copy", true));
            env.session.flushSnapshot();
            Json tracks = Json::array();
            for (auto id : made)
                tracks.push_back(formatId(IdKind::Track, id));
            return ToolResult::ok({{"tracks", tracks}}, std::to_string(made.size()) + " new tracks.");
        }));

    registry.tools.push_back(make(
        env,
        {"delete_takes", "Delete takes",
         "Deletes takes: the given ones, with unused every take its clip doesn't play, or with keep every take of "
         "that one's clip but it (which the clip then plays). A clip playing a deleted take plays another. Files "
         "stay. One undo step.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"takes", ids(IdKind::Take, "The takes")},
                 {"unused", boolean("Every take the track doesn't play")},
                 {"keep", id(IdKind::Take, "Keep only this take of its clip")}},
                {"track"}),
         takesResult, removes},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            if (args.value("unused", false))
                env.session.deleteUnusedTakes(t.id);
            else if (args.contains("keep"))
                env.session.keepOnlyTake(t.id, takeArg(t, args["keep"]));
            else if (args.contains("takes"))
            {
                std::vector<std::uint64_t> takes;
                for (const auto& v : args["takes"])
                    takes.push_back(takeArg(t, v));
                env.session.deleteTakes(t.id, takes);
            }
            else
                throw Problem{"Give takes, keep, or unused: true."};
            return ToolResult::ok(takesJson(env, t.id), "Deleted.");
        }));

    registry.tools.push_back(make(
        env,
        {"capture_midi", "Capture MIDI",
         "Turns the last phrase played (not recorded: since a pause of two bars, or since playback started) into "
         "a clip on each armed MIDI track, else on track, else on a new track. No instrument is added. Where it was "
         "played if the song was playing, else at the playhead. One undo step.",
         object({{"track", id(IdKind::Track, "The track, when no track is armed")}}),
         result({{"clip", {{"type", {"string", "null"}}, {"description", "The first clip made, or null"}}},
                 {"track", {{"type", {"string", "null"}}, {"description", "Its track"}}}}),
         ToolAnnotations{}},
        [&env](const Json& args, CallContext&) {
            std::optional<std::uint64_t> selected;
            if (args.contains("track"))
                selected = clipTrack(env, args["track"]).id;
            const auto clip = env.session.captureMidi(selected);
            env.session.flushSnapshot();
            if (clip == 0)
                return ToolResult::ok({{"clip", nullptr}, {"track", nullptr}}, "Nothing was played to capture.");
            const auto found = model::findClip(*env.session.snapshot(), clip);
            const auto track = found ? Json(formatId(IdKind::Track, env.session.snapshot()->tracks[static_cast<size_t>(found->second)].id))
                                     : Json(nullptr);
            return ToolResult::ok({{"clip", formatId(IdKind::Clip, clip)}, {"track", track}},
                                  "Captured " + formatId(IdKind::Clip, clip) + ".");
        }));
}
} // namespace fd::mcp
