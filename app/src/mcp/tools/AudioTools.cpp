// An audio clip's sound and ranges (M3 step P6, ADR 0063, docs/design/audio-editor.md §14): gain,
// the gain line, reverse, polarity and channels; the audio editor's range commands; its tools.

#include "mcp/tools/Tools.h"
#include "model/AudioSound.h"
#include "model/AudioToolkit.h"
#include "model/ClipLoops.h"
#include "model/Editing.h"
#include "model/Warp.h"

#include <cmath>

namespace fd::mcp
{
using namespace tools;

namespace
{
model::ClipInfo audioClip(ToolEnv& env, const Json& value)
{
    auto c = clip(env, value);
    if (c.kind != model::ClipInfo::Kind::Audio)
        throw Problem{c.name + " is a MIDI clip: these tools are for audio clips."};
    return c;
}

std::vector<std::uint64_t> audioClipIds(ToolEnv& env, const Json& list)
{
    std::vector<std::uint64_t> out;
    for (const auto& value : list)
        out.push_back(audioClip(env, value).id);
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

/// A gain in dB: a number, "-inf", or text such as "-3 dB".
float gainArg(const Json& value)
{
    if (value.is_number())
        return value.get<float>();
    if (value.is_string())
    {
        const auto text = juce::String(value.get<std::string>())
                              .trim()
                              .toLowerCase()
                              .upToFirstOccurrenceOf("db", false, false)
                              .trim();
        if (text == "-inf" || text == "−inf" || text == "-infinity")
            return model::minusInfinityDb;
        if (text.isNotEmpty() && text.containsOnly("+-0123456789."))
            return text.getFloatValue();
    }
    throw Problem{"Give the gain in dB: a number, \"-inf\", or text such as \"-3 dB\"."};
}

Json lineJson(const model::AutomationPoints& line)
{
    Json points = Json::array();
    for (const auto& p : line)
    {
        const double dbValue = p.value <= 0.0f ? -INFINITY : model::lineDbForValue(p.value);
        points.push_back({{"time", std::round(p.time * 1e6) / 1e6},
                          {"db", db(dbValue)},
                          {"bend", std::round(model::bendOf(p) * 1e3) / 1e3}});
    }
    return points;
}

model::AutomationPoints lineArg(const Json& points)
{
    model::AutomationPoints line;
    for (const auto& p : points)
    {
        if (!p.is_object() || !p.contains("time") || !p.contains("db"))
            throw Problem{
                "Each point needs \"time\" (seconds into the file) and \"db\" (a number, or null for -inf)."};
        const double time = p["time"].get<double>();
        if (time < 0.0)
            throw Problem{"A point's time is seconds into the file: it can't be negative."};
        const auto& d = p["db"];
        const float value = d.is_null() ? 0.0f : model::lineValueForDb(gainArg(d));
        const float bend = p.contains("bend") ? std::clamp(p["bend"].get<float>(), -1.0f, 1.0f) : 0.0f;
        line.push_back({time, value, model::curveForBend(bend)});
    }
    std::stable_sort(line.begin(), line.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return line;
}

engine::Session::RangeAction rangeActionArg(const Json& value)
{
    static const std::vector<std::pair<std::string, engine::Session::RangeAction>> actions{
        {"split", engine::Session::RangeAction::Split}, {"delete", engine::Session::RangeAction::Delete},
        {"crop", engine::Session::RangeAction::Crop},   {"silence", engine::Session::RangeAction::Silence},
        {"gain", engine::Session::RangeAction::Gain},   {"fade", engine::Session::RangeAction::Fade},
        {"loop", engine::Session::RangeAction::Loop}};
    const auto name = juce::String(value.get<std::string>()).trim().toLowerCase().toStdString();
    for (const auto& [word, action] : actions)
        if (word == name)
            return action;
    throw Problem{"The action is one of split, delete, crop, silence, gain, fade, loop."};
}

/// A grid step in beats: "1/4", "1/8", "1/16", "1/32", with a T for triplets ("1/8T").
double gridBeatsArg(const Json& value)
{
    auto text =
        juce::String(value.is_string() ? value.get<std::string>() : std::string()).trim().toLowerCase();
    const bool triplets = text.endsWithChar('t');
    if (triplets)
        text = text.dropLastCharacters(1);
    for (const auto& [name, beats] :
         {std::pair{"1/4", 1.0}, std::pair{"1/8", 0.5}, std::pair{"1/16", 0.25}, std::pair{"1/32", 0.125}})
        if (text == name)
            return triplets ? beats * 2.0 / 3.0 : beats;
    throw Problem{"The grid is 1/4, 1/8, 1/16 or 1/32, with a T for triplets (\"1/8T\")."};
}

/// A setting as list_audio_tools shows it.
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
        if (!s.unit.empty())
            j["unit"] = s.unit;
    }
    if (const auto* other = s.when.empty() ? nullptr : tool.setting(s.when); other != nullptr)
        j["onlyWhen"] = {{s.when, other->words[static_cast<std::size_t>(s.whenIs)]}};
    return j;
}

model::ToolValues settingsArg(const model::ToolSpec& tool, const Json& settings)
{
    model::ToolValues out;
    if (settings.is_null())
        return out;
    if (!settings.is_object())
        throw Problem{"\"settings\" is an object of the tool's settings by id (list_audio_tools)."};
    for (const auto& [key, value] : settings.items())
    {
        const auto* s = tool.setting(key);
        if (s == nullptr)
            throw Problem{std::string(tool.name) + " has no setting \"" + key + "\" (list_audio_tools)."};
        if (s->isChoice())
        {
            const auto word = value.is_string()
                                  ? juce::String(value.get<std::string>()).trim().toLowerCase().toStdString()
                                  : std::string();
            const auto it = std::find(s->words.begin(), s->words.end(), word);
            if (it == s->words.end())
                throw Problem{std::string(s->label) + " is one of: " +
                              juce::StringArray(
                                  [&]
                                  {
                                      juce::StringArray a;
                                      for (auto w : s->words)
                                          a.add(juce::String(std::string(w)));
                                      return a;
                                  }())
                                  .joinIntoString(", ")
                                  .toStdString() +
                              "."};
            out[key] = static_cast<double>(it - s->words.begin());
        }
        else
        {
            if (!value.is_number())
                throw Problem{std::string(s->label) + " is a number."};
            const double v = value.get<double>();
            if (v < s->min || v > s->max)
                throw Problem{std::string(s->label) + " is from " + juce::String(s->min).toStdString() +
                              " to " + juce::String(s->max).toStdString() + "."};
            out[key] = v;
        }
    }
    return out;
}
} // namespace

void addAudioTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations writes;
    ToolAnnotations sets;
    sets.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"set_audio", "Set audio clips' sound",
         "Sets audio clips' sound, never their files (audio-editor.md §6): gain in dB (-inf to +24; "
         "\"gainBy\" moves "
         "each clip's by an amount), reverse (it plays a reversed copy of its file, written into the "
         "project's Reversed "
         "folder, the same audio from its other end), polarity (\"inverted\"), and how a stereo file's "
         "channels play "
         "(\"stereo\", \"left\", \"right\", \"mono\", \"swapped\"). A clip with takes isn't reversed. "
         "Its warping and pitch (§7, §8.1): \"warp\" (the clip follows the song's tempo, keeping the audio "
         "it "
         "plays), its \"mode\" (beats, tones, repitch), \"tails\" (Beats: fade, loop), \"formants\" (Tones), "
         "\"tempo\" (its file's BPM: it plays the same audio at it, markers scaled), \"sensitivity\" (0 to "
         "1: "
         "how many transients are found), \"transpose\" (semitones, -48 to 48) and \"fine\" (cents, -50 to "
         "50); transposed, a clip keeps its speed. Its key (§8.2, §8.3): \"key\" (\"A minor\", \"F#m\", "
         "\"D dorian\"; \"none\"; \"found\" for what's found in its file) and \"followKey\" (it plays in the "
         "song's key, moved by the smallest step, a key and its relative counting as the same; the song "
         "needs a key, set_key, and a clip with no key can't).",
         object({{"clips", ids(IdKind::Clip, "The audio clips")},
                 {"warp", boolean("Follows the song's tempo")},
                 {"mode", string("beats, tones or repitch")},
                 {"tails", string("Beats' tails: fade or loop")},
                 {"formants", boolean("Tones keeps a voice's character when transposed")},
                 {"tempo", number("The clip's tempo: its file's BPM (20 to 999)")},
                 {"sensitivity", number("How many transients are found: 0 to 1")},
                 {"transpose", integer("Semitones, -48 to 48")},
                 {"fine", integer("Cents, -50 to 50")},
                 {"key", string("Its key: \"A minor\", \"F#m\", \"D dorian\", \"none\", or \"found\"")},
                 {"followKey", boolean("Plays in the song's key")},
                 {"gain",
                  {{"type", {"number", "string"}},
                   {"description", "dB: a number, \"-inf\", or text such as \"-3 dB\""}}},
                 {"gainBy", number("dB to move each clip's gain by")},
                 {"reverse", boolean("Plays its audio backwards")},
                 {"inverted", boolean("Polarity inverted")},
                 {"channels", string("stereo, left, right, mono or swapped")}},
                {"clips"}),
         result({{"clips", arrayOf(clipResultSchema(), "The clips", std::nullopt)}}, {"clips"}),
         writes}, // gainBy moves the gain again each call
        [&env](const Json& args, CallContext&)
        {
            const auto ids = audioClipIds(env, args["clips"]);
            engine::Session::SoundChange change;
            if (args.contains("gain"))
                change.gainDb = gainArg(args["gain"]);
            if (args.contains("gainBy"))
                change.gainBy = args["gainBy"].get<float>();
            if (args.contains("reverse"))
                change.reversed = args["reverse"].get<bool>();
            if (args.contains("inverted"))
                change.inverted = args["inverted"].get<bool>();
            if (args.contains("channels"))
            {
                const auto mode = model::channelModeFromName(args["channels"].get<std::string>());
                if (!mode)
                    throw Problem{"Channels are stereo, left, right, mono or swapped."};
                change.channels = *mode;
            }
            // Everything's read and checked before anything changes: a call is whole, or does nothing.
            engine::Session::WarpChange warp;
            if (args.contains("warp"))
                warp.on = args["warp"].get<bool>();
            if (args.contains("mode"))
            {
                const auto mode = model::warpModeFromName(args["mode"].get<std::string>());
                if (!mode)
                    throw Problem{"The mode is beats, tones or repitch."};
                warp.mode = *mode;
            }
            if (args.contains("tails"))
            {
                const auto tails = model::tailsFromName(args["tails"].get<std::string>());
                if (!tails)
                    throw Problem{"Tails are fade or loop."};
                warp.tails = *tails;
            }
            if (args.contains("formants"))
                warp.formants = args["formants"].get<bool>();
            if (args.contains("tempo"))
            {
                const double bpm = args["tempo"].get<double>();
                if (bpm < model::kMinClipTempo || bpm > model::kMaxClipTempo)
                    throw Problem{"A clip's tempo is from 20 to 999 BPM."};
                warp.tempo = bpm;
            }
            if (args.contains("sensitivity"))
                warp.sensitivity = std::clamp(args["sensitivity"].get<double>(), 0.0, 1.0);
            if (args.contains("transpose"))
                warp.transpose =
                    std::clamp(args["transpose"].get<int>(), -model::kMaxTranspose, model::kMaxTranspose);
            if (args.contains("fine"))
                warp.fine = std::clamp(args["fine"].get<int>(), -model::kMaxFine, model::kMaxFine);
            if (args.contains("key"))
            {
                const auto key = clipKeyArg(args["key"]);
                if (key.found)
                    warp.keyFound = true;
                else
                    warp.key = key.key;
            }
            if (args.contains("followKey"))
                warp.followKey = args["followKey"].get<bool>();
            if (warp.followKey.value_or(false) && !env.session.snapshot()->keys.first())
                throw Problem{"The song has no key to follow: set one with set_key first."};
            const auto asked = [&args](std::initializer_list<const char*> keys)
            {
                return std::any_of(keys.begin(), keys.end(),
                                   [&args](const char* k) { return args.contains(k); });
            };
            const engine::Session::EditGroup group(env.session, "Clip sound");
            if (asked({"gain", "gainBy", "reverse", "inverted", "channels"}))
                env.session.setSound(ids, change, "Clip sound");
            if (asked({"warp", "mode", "tails", "formants", "tempo", "sensitivity", "transpose", "fine",
                       "key", "followKey"}))
                env.session.setWarp(ids, warp, "Clip sound");
            Json out{{"clips", clipsJson(env, ids)}};
            if (warp.followKey.value_or(false))
            {
                std::string keyless, repitched;
                const auto add = [](std::string& list, const Json& c)
                { list += (list.empty() ? "" : ", ") + c["id"].get<std::string>(); };
                for (const auto& c : out["clips"])
                    if (!c["followKey"]["on"].get<bool>())
                        add(keyless, c);
                    else if (c["warp"]["on"].get<bool>() && c["warp"]["mode"] == "repitch")
                        add(repitched, c);
                std::string note;
                if (!keyless.empty())
                    note = "No key was found in " + keyless +
                           ", so it can't follow the song's: set its \"key\".";
                if (!repitched.empty())
                    note += (note.empty() ? "" : " ") + repitched +
                            " warps in Repitch, whose speed sets its pitch: it moves into the key in Beats "
                            "or Tones.";
                if (!note.empty())
                    out["note"] = note;
            }
            return ToolResult::ok(out);
        }));

    registry.tools.push_back(make(
        env,
        {"get_gain_line", "Get a clip's gain line",
         "An audio clip's gain line (audio-editor.md §6.2): its points, each at a time in seconds into its "
         "file as "
         "recorded (so it stays on its audio wherever the clip plays it, reversed too), a level in dB (null: "
         "-inf; at "
         "most +6) and a bend (-1 to 1). It shapes the clip's level on top of its gain; with no points, 0 dB "
         "throughout.",
         object({{"clip", id(IdKind::Clip, "The audio clip")}}, {"clip"}),
         result({{"clip", string("The clip")}, {"points", {{"type", "array"}}}}, {"clip", "points"}), reads},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            return ToolResult::ok(
                {{"clip", formatId(IdKind::Clip, c.id)}, {"points", lineJson(c.sound.line)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_gain_line", "Set a clip's gain line",
         "Replaces an audio clip's gain line with these points (get_gain_line): times in seconds into its "
         "file, levels "
         "in dB (null or \"-inf\" for silence; at most +6), bends -1 to 1. An empty list removes it.",
         object({{"clip", id(IdKind::Clip, "The audio clip")},
                 {"points", arrayOf({{"type", "object"}}, "Points: {time, db, bend?}", std::nullopt)}},
                {"clip", "points"}),
         result({{"clip", string("The clip")}, {"points", {{"type", "array"}}}}, {"clip", "points"}), sets},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            const auto line = lineArg(args["points"]);
            env.session.setGainLine(c.id, line);
            env.session.flushSnapshot();
            return ToolResult::ok({{"clip", formatId(IdKind::Clip, c.id)},
                                   {"points", lineJson(audioClip(env, args["clip"]).sound.line)}});
        }));

    registry.tools.push_back(make(
        env,
        {"edit_audio_range", "Edit a range of an audio clip",
         "Does what the audio editor does to a range of a clip (audio-editor.md §5), from and to being song "
         "positions "
         "inside it: \"split\" (the range becomes a clip of its own), \"delete\" (that part goes, the rest "
         "stays where "
         "it is), \"crop\" (only the range is left), \"silence\" (its gain line falls to silence over the "
         "range, with "
         "4 ms ramps), \"gain\" (its gain line moves by \"db\" over the range), \"fade\" (a range from the "
         "clip's start "
         "is its fade-in, to its end its fade-out), \"loop\" (the clip loops the range). Says why when it "
         "can't.",
         object({{"clip", id(IdKind::Clip, "The audio clip")},
                 {"from", position("Where the range starts")},
                 {"to", position("Where it ends")},
                 {"action", string("split, delete, crop, silence, gain, fade or loop")},
                 {"db", number("For gain: dB to move the range by")}},
                {"clip", "from", "to", "action"}),
         result({{"clips", arrayOf(clipResultSchema(), "What's left of the clip", std::nullopt)}}, {"clips"}),
         writes},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            const double from = seconds(env, positionArg(env, args["from"]));
            const double to = seconds(env, positionArg(env, args["to"]));
            const auto action = rangeActionArg(args["action"]);
            if (action == engine::Session::RangeAction::Gain && !args.contains("db"))
                throw Problem{"Give \"db\" for gain: how far to move the range."};
            const auto r = env.session.editRange(c.id, from, to, action, args.value("db", 0.0));
            if (!r.why.empty())
                throw Problem{r.why};
            return ToolResult::ok({{"clips", clipsJson(env, r.clips)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_warp_markers", "Set a clip's warp markers",
         "Warp markers pin moments of an audio clip's file to beats of its content (audio-editor.md §7.5): "
         "between two, the audio stretches evenly; outside them it plays at the clip's tempo, and the file's "
         "start is always pinned to beat 0. Do one of: \"markers\" (replace them all: each {file: seconds "
         "into the file as recorded, beat: quarter notes from the content's start}, later than the one "
         "before "
         "in both, each piece within x0.25 to x4 of the clip's tempo); \"add\" (seconds into the file: a "
         "marker at each, at the beat it plays now, so nothing moves); \"atTransients\" (a marker on every "
         "transient, between \"from\" and \"to\" if given); \"warpFrom\" (a marker's index: one tempo "
         "from it on, the markers after it gone); \"remove\" (markers' indices). Warp goes on with them. "
         "get_clip lists a clip's markers.",
         object({{"clip", id(IdKind::Clip, "The audio clip")},
                 {"markers", arrayOf({{"type", "object"}}, "Every marker: {file, beat}", std::nullopt)},
                 {"add", arrayOf({{"type", "number"}}, "Seconds into the file to pin where they play",
                                 std::nullopt)},
                 {"atTransients", boolean("A marker on every transient")},
                 {"from", position("With atTransients: from here")},
                 {"to", position("With atTransients: to here")},
                 {"warpFrom", integer("A marker's index")},
                 {"remove", arrayOf({{"type", "integer"}}, "Markers' indices", std::nullopt)}},
                {"clip"}),
         result({{"clip", clipResultSchema()}}, {"clip"}), writes},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            auto warp = c.warp;
            const int asked =
                static_cast<int>(args.contains("markers")) + static_cast<int>(args.contains("add")) +
                static_cast<int>(args.value("atTransients", false)) +
                static_cast<int>(args.contains("warpFrom")) + static_cast<int>(args.contains("remove"));
            if (asked != 1)
                throw Problem{"Give one of markers, add, atTransients, warpFrom or remove."};
            if (args.contains("markers"))
            {
                warp.markers.clear();
                for (const auto& m : args["markers"])
                {
                    if (!m.is_object() || !m.contains("file") || !m.contains("beat"))
                        throw Problem{"Each marker is {file, beat}: seconds into the file, and its beat."};
                    warp.markers.push_back({m["file"].get<double>(), m["beat"].get<double>()});
                }
            }
            else if (args.contains("add"))
            {
                for (const auto& t : args["add"])
                    warp = model::withMarkerAt(warp, t.get<double>()).first;
            }
            else if (args.value("atTransients", false))
            {
                double from = 0.0, to = c.fileLength;
                if (args.contains("from") || args.contains("to"))
                {
                    const auto map = model::contentMap(c);
                    const double origin = model::clipViewOrigin(c);
                    const double a =
                        args.contains("from") ? seconds(env, positionArg(env, args["from"])) : c.start;
                    const double b =
                        args.contains("to") ? seconds(env, positionArg(env, args["to"])) : c.end();
                    const double fa = map.fileAt(a - origin), fb = map.fileAt(b - origin);
                    from = std::min(fa, fb);
                    to = std::max(fa, fb);
                }
                warp = model::withMarkersAtTransients(warp, env.session.clipTransients(c.id), from, to);
            }
            else if (args.contains("warpFrom"))
            {
                const auto index = args["warpFrom"].get<int>();
                if (index < 0 || static_cast<size_t>(index) >= warp.markers.size())
                    throw Problem{"No marker " + std::to_string(index) + ": the clip has " +
                                  std::to_string(warp.markers.size()) + "."};
                warp = model::warpedFrom(warp, static_cast<size_t>(index));
            }
            else
            {
                std::vector<size_t> gone;
                for (const auto& i : args["remove"])
                    if (i.get<int>() >= 0)
                        gone.push_back(static_cast<size_t>(i.get<int>()));
                warp = model::withoutMarkers(warp, gone);
            }
            if (!env.session.setWarpMarkers(c.id, warp.markers, "Warp markers"))
                throw Problem{"Those markers are out of order, or stretch a piece past x0.25 to x4 of the "
                              "clip's tempo."};
            env.session.flushSnapshot();
            return ToolResult::ok({{"clip", clipJson(env, audioClip(env, args["clip"]), false)}});
        }));

    registry.tools.push_back(make(
        env,
        {"get_transients", "Get a clip's transients",
         "Where things start in an audio clip's file (audio-editor.md §7.4): seconds into the file as "
         "recorded, at the clip's sensitivity (set_audio), with those added or removed by hand. Beats starts "
         "its pieces at them; Quantise and markers at transients use them.",
         object({{"clip", id(IdKind::Clip, "The audio clip")}}, {"clip"}),
         result({{"clip", string("The clip")},
                 {"sensitivity", number("0 to 1")},
                 {"transients", arrayOf({{"type", "number"}}, "Seconds into the file", std::nullopt)}},
                {"clip", "sensitivity", "transients"}),
         reads},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            Json times = Json::array();
            for (double t : env.session.clipTransients(c.id))
                times.push_back(std::round(t * 1e4) / 1e4);
            return ToolResult::ok({{"clip", formatId(IdKind::Clip, c.id)},
                                   {"sensitivity", c.warp.sensitivity},
                                   {"transients", std::move(times)}});
        }));

    registry.tools.push_back(make(
        env,
        {"edit_transients", "Add or remove transients",
         "Transients by hand (audio-editor.md §7.4): \"add\" puts one at each time, \"remove\" hides "
         "those at each (as get_transients lists them); seconds into the file as recorded. The clip keeps "
         "them through any sensitivity. One undo step. Returns the transients as get_transients does.",
         object({{"clip", id(IdKind::Clip, "The audio clip")},
                 {"add", arrayOf({{"type", "number"}}, "Seconds into the file", std::nullopt)},
                 {"remove", arrayOf({{"type", "number"}}, "Transients' times, as listed", std::nullopt)}},
                {"clip"}),
         result({{"clip", string("The clip")},
                 {"transients", arrayOf({{"type", "number"}}, "Seconds into the file", std::nullopt)}},
                {"clip", "transients"}),
         writes},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            const auto times = [&](const char* key)
            {
                std::vector<double> out;
                for (const auto& t : args.value(key, Json::array()))
                {
                    if (!t.is_number())
                        throw Problem{std::string(key) + " lists seconds into the file."};
                    out.push_back(t.get<double>());
                }
                return out;
            };
            const auto add = times("add"), remove = times("remove");
            if (add.empty() && remove.empty())
                throw Problem{"Give add or remove: seconds into the file."};
            const auto now = env.session.clipTransients(c.id);
            const auto near = [&now](double t)
            {
                return std::any_of(now.begin(), now.end(), [t](double n) { return std::abs(n - t) < 0.001; });
            };
            const auto shown = [](double t) { return Json(std::round(t * 1e4) / 1e4).dump(); };
            for (double t : add)
                if (t < 0.0 || t > c.fileLength)
                    throw Problem{"The file is " + shown(c.fileLength) + " s long: " + shown(t) +
                                  " isn't in it."};
            for (double t : remove)
                if (!near(t))
                    throw Problem{"No transient at " + shown(t) + " s: get_transients lists them."};
            env.session.editTransients(c.id, add, remove);
            env.session.flushSnapshot();
            Json out = Json::array();
            for (double t : env.session.clipTransients(c.id))
                out.push_back(std::round(t * 1e4) / 1e4);
            return ToolResult::ok({{"clip", formatId(IdKind::Clip, c.id)}, {"transients", std::move(out)}},
                                  std::to_string(out.size()) + " transients.");
        }));

    registry.tools.push_back(make(
        env,
        {"detect_tempo", "Find a clip's tempo",
         "The tempo found in an audio clip's sound (audio-editor.md §7.8): its BPM (60 to 180; a loop "
         "under 32 s a whole number of bars), its first downbeat (seconds into its file) and how sure it is "
         "(0 to 1: a drum loop's is around 0.4, a sound with no beat under 0.1). Changes nothing: "
         "set_audio's "
         "tempo, or transform_audio's tempo, applies it. Halve or double it when it's an octave off.",
         object({{"clip", id(IdKind::Clip, "The audio clip")}}, {"clip"}),
         result({{"clip", string("The clip")},
                 {"found", boolean("Whether a tempo is heard")},
                 {"tempo", number("BPM")},
                 {"downbeat", number("Seconds into the file")},
                 {"confidence", number("0 to 1")}},
                {"clip", "found"}),
         reads},
        [&env](const Json& args, CallContext&)
        {
            const auto c = audioClip(env, args["clip"]);
            Json out{{"clip", formatId(IdKind::Clip, c.id)}, {"found", false}};
            if (const auto t = env.session.foundTempo(c.id))
            {
                out["found"] = true;
                out["tempo"] = std::round(t->bpm * 100.0) / 100.0;
                out["downbeat"] = std::round(t->downbeat * 1e4) / 1e4;
                out["confidence"] = std::round(t->confidence * 1e3) / 1e3;
            }
            return ToolResult::ok(std::move(out));
        }));

    registry.tools.push_back(
        make(env,
             {"list_audio_tools", "List audio tools",
              "The audio editor's tools (transform_audio), as its Tools pane has them: each with what it "
              "does and its "
              "settings (a choice of words, or a number with its range and unit), and their defaults.",
              object({}), result({{"tools", {{"type", "array"}}}}), reads},
             [](const Json&, CallContext&)
             {
                 Json tools = Json::array();
                 for (const auto& t : model::audioTools())
                 {
                     Json settings = Json::array();
                     for (const auto& s : t.settings)
                         settings.push_back(settingJson(t, s));
                     tools.push_back({{"tool", t.id},
                                      {"name", t.name},
                                      {"about", t.about},
                                      {"settings", std::move(settings)}});
                 }
                 return ToolResult::ok({{"tools", std::move(tools)}});
             }));

    registry.tools.push_back(make(
        env,
        {"transform_audio", "Transform audio clips",
         "Runs one of the audio editor's tools (list_audio_tools) on audio clips, as its Tools pane does: "
         "normalise sets each clip's gain so it reaches a peak (dBFS) or a loudness (LUFS), or moves them "
         "together so the loudest does; quantise moves each clip's transients towards the grid (\"grid\": "
         "1/4, "
         "1/8, 1/16, 1/32, with a T for triplets; 1/16 by default) through warp markers, Warp on; tempo "
         "warps "
         "each clip at a tempo starting at its downbeat (seconds into its file), or sets the song's tempo "
         "from "
         "it (\"then\": song); slice cuts each clip into clips on its track (\"at\": transients, markers, "
         "grid, bars; the grid's step is \"grid\"), seamless until they're moved, and \"clips\" lists every "
         "part. Tempo's settings left out are what's found in the clip (detect_tempo); others take their "
         "defaults.",
         object({{"clips", ids(IdKind::Clip, "The audio clips")},
                 {"tool", string("The tool: normalise, quantise, tempo or slice")},
                 {"settings", {{"type", "object"}, {"description", "The tool's settings by id"}}},
                 {"grid", string("Quantise's and Slice's grid: 1/4, 1/8, 1/16, 1/32 (T for triplets)")}},
                {"clips", "tool"}),
         result({{"clips", arrayOf(clipResultSchema(), "The clips", std::nullopt)},
                 {"changed", integer("How many changed")}},
                {"clips", "changed"}),
         writes},
        [&env](const Json& args, CallContext&)
        {
            const auto ids = audioClipIds(env, args["clips"]);
            const auto name = args["tool"].get<std::string>();
            const auto* tool = model::findAudioTool(juce::String(name).trim().toLowerCase().toStdString());
            if (tool == nullptr)
                throw Problem{"No audio tool \"" + name + "\" (list_audio_tools)."};
            const auto given = settingsArg(*tool, args.value("settings", Json()));
            if (tool->id == "quantise")
            {
                const double grid = gridBeatsArg(args.value("grid", Json("1/16")));
                const auto values = model::toolValues(*tool, given);
                const engine::Session::EditGroup group(env.session, "Quantise");
                size_t changed = 0;
                std::string refused; // the clips it couldn't quantise, and why
                for (auto id : ids)
                {
                    const auto why = env.session.quantiseAudio(
                        id, {.grid = model::warpQuantiseChoice(values, grid),
                             .sensitivity = given.contains("sensitivity")
                                                ? std::optional(model::quantiseSensitivity(values))
                                                : std::nullopt});
                    if (why.empty())
                        ++changed;
                    else if (ids.size() == 1)
                        throw Problem{why};
                    else
                        refused += (refused.empty() ? "" : " ") + formatId(IdKind::Clip, id) + ": " + why;
                }
                Json out{{"clips", clipsJson(env, ids)}, {"changed", changed}};
                if (!refused.empty())
                    out["note"] = refused;
                return ToolResult::ok(out);
            }
            if (tool->id == "slice")
            {
                const double grid = gridBeatsArg(args.value("grid", Json("1/16")));
                const auto choice = model::sliceChoice(given);
                const engine::Session::EditGroup group(env.session, "Slice");
                std::vector<std::uint64_t> parts;
                size_t changed = 0;
                for (auto id : ids)
                {
                    std::string why;
                    auto made = env.session.slice(id,
                                                  {.at = choice.at,
                                                   .sensitivity = given.contains("sensitivity")
                                                                      ? std::optional(choice.sensitivity)
                                                                      : std::nullopt,
                                                   .gridBeats = grid},
                                                  &why);
                    if (made.empty())
                    {
                        if (ids.size() == 1)
                            throw Problem{why};
                        parts.push_back(id);
                        continue;
                    }
                    ++changed;
                    parts.insert(parts.end(), made.begin(), made.end());
                }
                return ToolResult::ok({{"clips", clipsJson(env, parts)}, {"changed", changed}});
            }
            if (tool->id == "tempo")
            {
                // Each clip's choice first: a clip with no tempo heard refuses the call before any changes.
                std::vector<model::TempoChoice> choices;
                for (auto id : ids)
                {
                    auto values = given;
                    const auto found = env.session.foundTempo(id);
                    if (!values.contains("tempo"))
                    {
                        if (!found)
                            throw Problem{"No tempo is heard in " + formatId(IdKind::Clip, id) +
                                          ": give \"tempo\"."};
                        values["tempo"] = found->bpm;
                    }
                    if (!values.contains("downbeat"))
                        values["downbeat"] = found ? found->downbeat : 0.0;
                    choices.push_back(model::tempoChoice(values));
                }
                const engine::Session::EditGroup group(env.session, "Tempo");
                for (size_t i = 0; i < ids.size(); ++i)
                    env.session.applyTempo(ids[i], choices[i].bpm, choices[i].downbeat, choices[i].songTempo);
                return ToolResult::ok({{"clips", clipsJson(env, ids)}, {"changed", ids.size()}});
            }
            const auto choice = model::normaliseChoice(given);
            const auto changed = env.session.normalise(
                ids, {.to = choice.loudness ? model::NormaliseTo::Loudness : model::NormaliseTo::Peak,
                      .targetDb = choice.targetDb,
                      .together = choice.together});
            return ToolResult::ok({{"clips", clipsJson(env, ids)}, {"changed", changed.size()}},
                                  changed.empty() ? "Nothing to normalise: the clips are silent." : "");
        }));
}
} // namespace fd::mcp
