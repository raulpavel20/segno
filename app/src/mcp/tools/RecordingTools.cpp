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
    if (!id || std::none_of(track.takes.begin(), track.takes.end(), [&](const auto& t) { return t.id == *id; }))
        throw Problem{value.dump() + " isn't one of " + track.name + "'s takes (get_takes lists them)."};
    return *id;
}

Json takesJson(ToolEnv& env, std::uint64_t trackId)
{
    env.session.flushSnapshot();
    const auto* t = env.session.snapshot()->channel(trackId);
    const auto tm = tempo(env);
    Json takes = Json::array();
    if (t != nullptr)
        for (const auto& take : t->takes)
        {
            Json clips = Json::array();
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
                clips.push_back(std::move(clip));
            }
            takes.push_back({{"id", formatId(IdKind::Take, take.id)},
                             {"name", take.name},
                             {"star", take.star},
                             {"playing", model::isUsed(take, t->clips)},
                             {"clips", std::move(clips)}});
        }
    return {{"track", formatId(IdKind::Track, trackId)}, {"takes", std::move(takes)}};
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
         "\"2 bars\") applies to this recording; punch records only inside the loop. Each pass is one undo step. "
         "While recording, calling it again punches out and keeps playing.",
         object({{"from", position("Where to start (default: the playhead)")},
                 {"bars", number("Stop after this many bars")},
                 {"countIn", string("The count-in for this recording")},
                 {"punch", boolean("Record only inside the loop")}}),
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
            if (args.contains("punch"))
                s.setPunch(args["punch"].get<bool>());
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

    const auto takesResult = result({{"track", string("")}, {"takes", arrayOf(result({{"id", string("")}, {"name", string("")}}), "", 0)}},
                                     {"track", "takes"});
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations removes;
    removes.destructive = true;

    registry.tools.push_back(make(
        env,
        {"get_takes", "List a track's takes",
         "Lists a track's take lanes: every recording pass and loop lap is kept as a take (ADR 0031). Each take: its "
         "id, name, star, whether the track plays it, and its clips (where, the audio file or the number of notes, and "
         "which parts the track plays). To compare takes, render each one's time range (render) and measure it.",
         object({{"track", id(IdKind::Track, "The track")}}, {"track"}), takesResult, reads, false},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            const auto j = takesJson(env, t.id);
            return ToolResult::ok(j, t.name + " has " + std::to_string(j["takes"].size()) + " takes.");
        }));

    registry.tools.push_back(make(
        env,
        {"use_take", "Use a take",
         "Makes the track play a take: its clips replace what the track plays across their time ranges. One undo step.",
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
        {"set_take", "Rename or star a take",
         "Renames a take and/or stars it (a favourite). One undo step each.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"take", id(IdKind::Take, "The take")},
                 {"name", string("Its new name")},
                 {"star", boolean("A favourite")}},
                {"track", "take"}),
         takesResult, changes},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            const auto take = takeArg(t, args["take"]);
            if (args.contains("name"))
                env.session.renameTake(t.id, take, args["name"].get<std::string>());
            if (args.contains("star"))
                env.session.setTakeStar(t.id, take, args["star"].get<bool>());
            return ToolResult::ok(takesJson(env, t.id), "Done.");
        }));

    registry.tools.push_back(make(
        env,
        {"move_takes_to_tracks", "Takes onto new tracks",
         "Puts takes onto new tracks right after the track, each copying its modules and mix and playing one take (for "
         "doubles and layers). copy (default true) keeps them on this track too. One undo step.",
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
         "Deletes take lanes: the given takes, or with unused every take the track doesn't play. Their files stay. One "
         "undo step.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"takes", ids(IdKind::Take, "The takes")},
                 {"unused", boolean("Every take the track doesn't play")}},
                {"track"}),
         takesResult, removes},
        [&env](const Json& args, CallContext&) {
            const auto t = clipTrack(env, args["track"]);
            if (args.value("unused", false))
                env.session.deleteUnusedTakes(t.id);
            else if (args.contains("takes"))
            {
                std::vector<std::uint64_t> takes;
                for (const auto& v : args["takes"])
                    takes.push_back(takeArg(t, v));
                env.session.deleteTakes(t.id, takes);
            }
            else
                throw Problem{"Give takes, or unused: true."};
            return ToolResult::ok(takesJson(env, t.id), "Deleted.");
        }));

    registry.tools.push_back(make(
        env,
        {"capture_midi", "Capture MIDI",
         "Turns the MIDI just played (the last minute, not recorded) into a clip on the track it was played into: "
         "where it was played if the song was playing, else at the playhead. One undo step.",
         object({}), result({{"clip", {{"type", {"string", "null"}}, {"description", "The new clip, or null"}}}}),
         ToolAnnotations{}},
        [&env](const Json&, CallContext&) {
            const auto clip = env.session.captureMidi();
            env.session.flushSnapshot();
            if (clip == 0)
                return ToolResult::ok({{"clip", nullptr}}, "Nothing was played to capture.");
            return ToolResult::ok({{"clip", formatId(IdKind::Clip, clip)}}, "Captured " + formatId(IdKind::Clip, clip) + ".");
        }));
}
} // namespace fd::mcp
