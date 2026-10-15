// An audio clip's sound and ranges (M3 step P6, ADR 0063, docs/design/audio-editor.md §14): gain,
// the gain line, reverse, polarity and channels; the audio editor's range commands; its tools.

#include "mcp/tools/Tools.h"
#include "model/AudioSound.h"
#include "model/AudioToolkit.h"
#include "model/Editing.h"

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

    registry.tools.push_back(
        make(env,
             {"set_audio", "Set audio clips' sound",
              "Sets audio clips' sound, never their files (audio-editor.md §6): gain in dB (-inf to +24; "
              "\"gainBy\" moves "
              "each clip's by an amount), reverse (it plays a reversed copy of its file, written into the "
              "project's Reversed "
              "folder, the same audio from its other end), polarity (\"inverted\"), and how a stereo file's "
              "channels play "
              "(\"stereo\", \"left\", \"right\", \"mono\", \"swapped\"). A clip with takes isn't reversed.",
              object({{"clips", ids(IdKind::Clip, "The audio clips")},
                      {"gain",
                       {{"type", {"number", "string"}},
                        {"description", "dB: a number, \"-inf\", or text such as \"-3 dB\""}}},
                      {"gainBy", number("dB to move each clip's gain by")},
                      {"reverse", boolean("Plays its audio backwards")},
                      {"inverted", boolean("Polarity inverted")},
                      {"channels", string("stereo, left, right, mono or swapped")}},
                     {"clips"}),
              result({{"clips", arrayOf(clipResultSchema(), "The clips", std::nullopt)}}, {"clips"}), sets},
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
                 env.session.setSound(ids, change, "Clip sound");
                 return ToolResult::ok({{"clips", clipsJson(env, ids)}});
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
         "normalise "
         "sets each clip's gain so it reaches a peak (dBFS) or a loudness (LUFS), or moves them together so "
         "the "
         "loudest does. Settings left out take their defaults.",
         object({{"clips", ids(IdKind::Clip, "The audio clips")},
                 {"tool", string("The tool: normalise")},
                 {"settings", {{"type", "object"}, {"description", "The tool's settings by id"}}}},
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
            const auto choice = model::normaliseChoice(settingsArg(*tool, args.value("settings", Json())));
            const auto changed = env.session.normalise(
                ids, {.to = choice.loudness ? model::NormaliseTo::Loudness : model::NormaliseTo::Peak,
                      .targetDb = choice.targetDb,
                      .together = choice.together});
            return ToolResult::ok({{"clips", clipsJson(env, ids)}, {"changed", changed.size()}},
                                  changed.empty() ? "Nothing to normalise: the clips are silent." : "");
        }));
}
} // namespace fd::mcp
