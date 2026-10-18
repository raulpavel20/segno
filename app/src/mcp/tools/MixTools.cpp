// The mixer (M2 step C3, ADR 0026, docs/design/mixer.md §10): levels, pan, mute and solo, return
// tracks and sends, and the channels' meters.

#include "mcp/tools/Tools.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace fd::mcp
{
using namespace tools;

namespace
{
/// A level in dB from a number or text ("-6", "-6 dB", "-inf").
float levelArg(const Json& value, float max)
{
    if (value.is_number())
        return std::min(max, value.get<float>());
    if (value.is_string())
    {
        auto text = value.get<std::string>();
        if (text.find("inf") != std::string::npos)
            return model::minusInfinityDb;
        try
        {
            return std::min(max, std::stof(text));
        }
        catch (const std::exception&)
        {
        }
    }
    throw Problem{value.dump() + " isn't a level: give dB as a number (-6) or text (\"-6 dB\", \"-inf\")."};
}

/// Pan from a number (-1 .. 1) or text ("C", "25L", "50R").
float panArg(const Json& value)
{
    if (value.is_number())
        return std::clamp(value.get<float>(), -1.0f, 1.0f);
    if (value.is_string())
    {
        auto text = value.get<std::string>();
        if (text == "C" || text == "c" || text == "0")
            return 0.0f;
        if (!text.empty() && (text.back() == 'L' || text.back() == 'l' || text.back() == 'R' || text.back() == 'r'))
        {
            try
            {
                const float amount = std::clamp(std::stof(text.substr(0, text.size() - 1)), 0.0f, 50.0f) / 50.0f;
                return text.back() == 'L' || text.back() == 'l' ? -amount : amount;
            }
            catch (const std::exception&)
            {
            }
        }
    }
    throw Problem{value.dump() + " isn't a pan position: give -1 (left) to 1 (right), or \"C\", \"25L\", \"50R\"."};
}

Json levelSchema(const std::string& description)
{
    return {{"type", {"number", "string"}}, {"description", description}};
}
} // namespace

void addMixTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations adds;

    registry.tools.push_back(make(
        env,
        {"set_mix", "Set a channel's mix",
         "Sets any of a channel's volume, pan, mute and solo: a track, a group, a return track, or the "
         "master "
         "(volume and pan only). A group's mute silences what's in it; soloing it solos what's in it. Volume "
         "is in dB, from -inf to +6 (0 dB leaves the sound as it is). Pan is a balance: 0 dB at "
         "the centre. Solo is in place: other tracks go silent, return tracks keep playing; it is exclusive "
         "unless "
         "exclusive is false, and it is never an undo step. The rest is one undo step.",
         object({{"track", id(IdKind::Track, "The track, group, return track or master")},
                 {"volume", levelSchema("dB (-inf .. +6), e.g. -6, \"-6 dB\", \"-inf\"")},
                 {"pan",
                  {{"type", {"number", "string"}},
                   {"description", "-1 (left) .. 1 (right), or \"C\", \"25L\", \"50R\""}}},
                 {"mute", boolean("Muted")},
                 {"solo", boolean("Soloed")},
                 {"exclusive", boolean("Soloing clears the other solos (default true)")}},
                {"track"}),
         result({{"track", string("")},
                 {"mix",
                  result({{"volume", {{"type", {"number", "null"}}, {"description", "dB; null is -inf"}}}})}},
                {"track", "mix"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto t = channel(env, args["track"]);
            const bool master = t.role == model::TrackInfo::Role::Master;
            if (master && (args.contains("mute") || args.contains("solo")))
                throw Problem{"The master has no mute or solo; set its volume and pan."};
            auto& s = env.session;
            if (args.contains("volume"))
                s.setVolume(t.id, levelArg(args["volume"], 6.0f));
            if (args.contains("pan"))
                s.setPan(t.id, panArg(args["pan"]));
            if (args.contains("mute"))
                s.setMute(t.id, args["mute"].get<bool>());
            if (args.contains("solo"))
                s.setSolo(t.id, args["solo"].get<bool>(), args.value("exclusive", true));
            s.flushSnapshot();
            const auto now = channel(env, args["track"]);
            const auto mix = mixJson(env, now);
            return ToolResult::ok({{"track", formatId(IdKind::Track, t.id)}, {"mix", mix}},
                                  now.name + ": " + (mix["volume"].is_null() ? std::string("-inf") : mix["volume"].dump()) +
                                      " dB, " + panText(now.mix.pan) + (now.mix.mute ? ", muted" : "") +
                                      (now.mix.solo ? ", soloed" : "") + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"add_return_track", "Add a return track",
         "Adds a return track after the others (\"A Return\", \"B Return\"...): a channel without clips that tracks "
         "send to, for shared effects such as a reverb. Every track can then send to it with set_send. Put its "
         "effects in its chain with add_module.",
         object({{"name", string("Its name (default: the next letter, \"A Return\")")}}),
         result({{"track", string("Its id")}, {"name", string("")}}, {"track", "name"}), adds},
        [&env](const Json& args, CallContext&) {
            const auto id = env.session.addReturnTrack(args.value("name", std::string()));
            if (id == 0)
                throw Problem{"There's no room for another return track (32 at most)."};
            env.session.flushSnapshot();
            const auto r = channel(env, formatId(IdKind::Track, id));
            return ToolResult::ok({{"track", formatId(IdKind::Track, id)}, {"name", r.name}},
                                  "Added " + r.name + "; tracks send to it with set_send.");
        }));

    registry.tools.push_back(make(
        env,
        {"set_send", "Set a track's send",
         "Sets how much of a track or group goes to a return track (from -inf, off, to 0 dB) and whether it "
         "is taken before "
         "the track's volume (preFader) or after it (the default, so it follows the volume). One undo step.",
         object({{"track", id(IdKind::Track, "The track or group (not a return or the master)")},
                 {"return", id(IdKind::Track, "The return track")},
                 {"level", levelSchema("dB (-inf .. 0), e.g. -12, \"-inf\"")},
                 {"preFader", boolean("Taken before the volume")}},
                {"track", "return"}),
         result({{"track", string("")}, {"return", string("")}, {"preFader", boolean("")}},
                {"track", "return", "preFader"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto t = channel(env, args["track"]);
            const auto r = channel(env, args["return"]);
            if (t.role != model::TrackInfo::Role::Track && t.role != model::TrackInfo::Role::Group)
                throw Problem{"Only tracks and groups send; " + t.name + " is a " + kindName(t) + "."};
            if (r.role != model::TrackInfo::Role::Return)
                throw Problem{r.name + " isn't a return track; add_return_track makes one."};
            std::optional<float> level;
            if (args.contains("level"))
                level = levelArg(args["level"], 0.0f);
            std::optional<bool> pre;
            if (args.contains("preFader"))
                pre = args["preFader"].get<bool>();
            if (!env.session.setSend(t.id, r.id, level, pre))
                throw Problem{"The send couldn't be set."};
            env.session.flushSnapshot();
            for (const auto& s : channel(env, args["track"]).mix.sends)
                if (s.returnId == r.id)
                    return ToolResult::ok({{"track", formatId(IdKind::Track, t.id)},
                                           {"return", formatId(IdKind::Track, r.id)},
                                           {"level", s.levelDb <= model::minusInfinityDb ? Json(nullptr) : db(s.levelDb)},
                                           {"preFader", s.preFader}},
                                          t.name + " sends " +
                                              (s.levelDb <= model::minusInfinityDb ? std::string("nothing")
                                                                                    : db(s.levelDb).dump() + " dB") +
                                              " to " + r.name + (s.preFader ? ", before its volume." : "."));
            throw Problem{"The send couldn't be read back."};
        }));

    // measure_mix: every channel's meter over a window, read without taking the views' peaks.
    Tool measure;
    measure.name = "measure_mix";
    measure.title = "Read the channels' meters";
    measure.description =
        "Reads every channel's level meter (tracks, return tracks and the master) while the project plays, over a "
        "short window (half a second by default): the loudest peak of each, in dBFS, after its volume and pan, and "
        "whether it went over 0 dBFS. Channels that aren't heard (muted, or silenced by a solo) read silent. Meters "
        "read silence when nothing plays, so start playback first; render measures a range offline.";
    measure.inputSchema = object({{"seconds", number("How long to watch, 0.1 to 5 (default 0.5)")}});
    measure.outputSchema = result({{"playing", boolean("")},
                                   {"channels", arrayOf(result({{"track", string("")}, {"kind", string("")}}), "",
                                                        std::nullopt)}},
                                  {"playing", "channels"});
    measure.annotations.readOnly = true;
    measure.call = [&env](const Json& args, CallContext& context) -> ToolResult {
        struct Channel
        {
            model::TrackInfo info;
            std::unique_ptr<engine::Session::LevelReader> reader;
            std::array<float, 2> peaks{};
        };
        std::vector<Channel> channels;
        bool playing = false;
        const double seconds = std::clamp(args.value("seconds", 0.5), 0.1, 5.0);
        if (!env.main.run([&] {
                env.session.flushSnapshot();
                const auto snap = env.session.snapshot();
                std::vector<model::TrackInfo> all(snap->tracks);
                all.insert(all.end(), snap->returns.begin(), snap->returns.end());
                all.push_back(snap->master);
                for (auto& t : all)
                    if (auto reader = env.session.levelReader(t.id))
                    {
                        (void)reader->takePeaks(); // the window starts now
                        channels.push_back({std::move(t), std::move(reader), {}});
                    }
                playing = env.session.isPlaying();
            }))
            return ToolResult::error("The DAW is closing.");

        // Peaks are gathered on the message thread while this thread waits, and the readers are
        // let go there too.
        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < until && !context.cancelled() && env.main.isOpen())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            env.main.run([&] {
                for (auto& c : channels)
                {
                    const auto now = c.reader->takePeaks();
                    for (size_t i = 0; i < 2; ++i)
                        c.peaks[i] = std::max(c.peaks[i], now[i]);
                }
            });
        }
        Json list = Json::array();
        bool clipped = false;
        std::string loudest;
        float loudestPeak = 0.0f;
        for (const auto& c : channels)
        {
            const float peak = std::max(c.peaks[0], c.peaks[1]);
            auto toDb = [](float v) { return v > 1e-5f ? db(20.0 * std::log10(v)) : Json(nullptr); };
            clipped = clipped || peak > 1.0f;
            if (c.info.role != model::TrackInfo::Role::Master && peak > loudestPeak)
            {
                loudestPeak = peak;
                loudest = c.info.name;
            }
            list.push_back({{"track", formatId(IdKind::Track, c.info.id)},
                            {"kind", kindName(c.info)},
                            {"name", c.info.name},
                            {"peak", toDb(peak)},
                            {"peakLeft", toDb(c.peaks[0])},
                            {"peakRight", toDb(c.peaks[1])},
                            {"clipped", peak > 1.0f}});
        }
        env.main.run([&] { channels.clear(); });
        std::string summary = playing ? "" : "Nothing was playing, so the meters read silence. ";
        if (!loudest.empty())
            summary += "The loudest channel is " + loudest + " (" + db(20.0 * std::log10(loudestPeak)).dump() + " dBFS).";
        if (clipped)
            summary += " Something went over 0 dBFS.";
        return ToolResult::ok({{"playing", playing}, {"seconds", seconds}, {"channels", list}, {"clipped", clipped}},
                              summary.empty() ? std::string("Every channel is silent.") : summary);
    };
    registry.tools.push_back(std::move(measure));
}
} // namespace fd::mcp
