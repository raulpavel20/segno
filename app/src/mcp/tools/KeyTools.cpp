// The song's key (M3 P3, ADR 0057, docs/design/key-and-scale.md §14): set it, list the scales, and
// find keys that fit the notes. set_key is one undo step.

#include "mcp/tools/Tools.h"
#include "model/Browser.h"

#include <cmath>
#include <fd/music/Scales.h>

namespace fd::mcp
{
using namespace tools;

namespace tools
{
Json keyJson(const std::optional<music::Key>& key)
{
    if (!key)
        return nullptr;
    using music::Accidentals;
    Json notes = Json::array();
    for (const auto& n : music::spelledNotes(*key, Accidentals::Ascii))
        notes.push_back(n);
    return {{"root", music::rootName(*key, Accidentals::Ascii)},
            {"scale", key->scale},
            {"name", music::keyName(*key, Accidentals::Ascii)},
            {"notes", std::move(notes)}};
}

ClipKeyArg clipKeyArg(const Json& value)
{
    const Problem problem{"Give the clip's key as a root and a scale (\"A minor\", \"F#m\", \"D dorian\"), "
                          "\"none\", or \"found\" for what's found in its file."};
    if (!value.is_string())
        throw problem;
    const auto text = juce::String(value.get<std::string>()).trim();
    if (text.equalsIgnoreCase("none"))
        return {};
    if (text.equalsIgnoreCase("found"))
        return {.found = true};
    if (const auto named = model::keyFromName(text.toStdString()))
        return {.key = named};
    // A root, then a scale's ID or name.
    const auto root = music::parsePitchClass(text.upToFirstOccurrenceOf(" ", false, false).toStdString());
    const auto* scale = music::findScale(text.fromFirstOccurrenceOf(" ", false, false).trim().toStdString());
    if (!root || scale == nullptr || !text.containsChar(' '))
        throw problem;
    return {.key = music::Key{*root, std::string(scale->id)}};
}

Json keyResultSchema()
{
    return {{"type", {"object", "null"}},
            {"description",
             "The song's key: root, scale (its ID), name and notes, sharps as # and flats as b; null for "
             "no key"}};
}
} // namespace tools

namespace
{
int rootArg(const Json& value)
{
    if (value.is_number_integer())
    {
        const int pc = value.get<int>();
        if (pc < 0 || pc > 11)
            throw Problem{"Give the root as a note name (\"F#\", \"Bb\") or a pitch class, 0 (C) to 11 (B)."};
        return pc;
    }
    if (value.is_string())
        if (const auto pc = music::parsePitchClass(value.get<std::string>()))
            return *pc;
    throw Problem{"Give the root as a note name (\"F#\", \"Bb\") or a pitch class, 0 (C) to 11 (B)."};
}

const music::Scale& scaleArg(const Json& value)
{
    if (value.is_string())
        if (const auto* s = music::findScale(value.get<std::string>()))
            return *s;
    std::string known;
    for (const auto& s : music::scales())
        known += (known.empty() ? "" : ", ") + std::string(s.id);
    throw Problem{"There's no such scale. The scales are: " + known + "."};
}
} // namespace

void addKeyTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations edit;
    edit.idempotent = true;
    registry.tools.push_back(make(
        env,
        {"set_key", "Set the song's key",
         "Sets the song's key: a root and a scale (list_scales lists them), or none. One key for the whole "
         "song. It "
         "describes the song: it moves no note, but the piano roll shows it, modules that follow the key "
         "play "
         "in it, and audio clips that follow it (set_audio's followKey) move into it. One undo step.",
         object({{"root",
                  {{"type", {"string", "integer"}},
                   {"description",
                    "The root: a note name (\"A\", \"F#\", \"Bb\") or a pitch class, 0 (C) to 11 (B)"}}},
                 {"scale", string("The scale: its ID (\"minor\", \"dorian\") or its name. Default: major")},
                 {"none", boolean("True for no key (leave out root and scale)")}}),
         result({{"key", keyResultSchema()}}, {"key"}), edit},
        [&env](const Json& args, CallContext&)
        {
            const bool none = args.value("none", false);
            if (none == args.contains("root"))
                throw Problem{"Give a \"root\" (and a \"scale\"), or \"none\": true for no key."};
            if (none)
                env.session.setKey(std::nullopt);
            else
            {
                const auto& scale =
                    args.contains("scale") ? scaleArg(args["scale"]) : *music::findScale("major");
                env.session.setKey(music::Key{rootArg(args["root"]), std::string(scale.id)});
            }
            env.session.flushSnapshot();
            return ToolResult::ok({{"key", keyJson(env.session.snapshot()->keys.first())}});
        }));

    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    registry.tools.push_back(
        make(env,
             {"list_scales", "List the scales",
              "Every scale a key can have, by group: its ID (for set_key), name, short name, and intervals "
              "in semitones "
              "from the root.",
              object({}),
              result({{"scales", arrayOf(result({{"id", string("")}, {"name", string("")}}),
                                         "In the catalogue's order", 1)}},
                     {"scales"}),
              reading},
             [](const Json&, CallContext&)
             {
                 Json list = Json::array();
                 for (const auto& s : music::scales())
                 {
                     Json intervals = Json::array();
                     for (int i = 0; i < 12; ++i)
                         if ((s.intervals & (1u << i)) != 0)
                             intervals.push_back(i);
                     Json entry{{"id", s.id},
                                {"name", s.name},
                                {"shortName", s.shortName},
                                {"group", s.group},
                                {"intervals", std::move(intervals)}};
                     if (!s.alsoCalled.empty())
                         entry["alsoCalled"] = s.alsoCalled;
                     list.push_back(std::move(entry));
                 }
                 return ToolResult::ok({{"scales", std::move(list)}});
             }));

    registry.tools.push_back(make(
        env,
        {"detect_key", "Find the key from the notes and the audio",
         "Up to three major or minor keys that fit the song's clips (or the clips given), best first, each "
         "with how well it fits (0 to 1): MIDI clips' notes, and audio clips' keys (each found in its file "
         "now if it wasn't: its metadata, its name, else its sound; a clip that follows the song's key "
         "says nothing of its own). None when there are too few notes to judge (8 notes, 5 different ones) "
         "and no audio clip has a key. It only suggests: set_key sets the song's, set_audio a clip's.",
         object({{"clips", ids(IdKind::Clip, "Clips to judge (default: every clip that plays)")}}),
         result({{"keys", arrayOf(result({{"name", string("")}, {"fit", number("")}}), "Best first", 0)}},
                {"keys"}),
         reading},
        [&env](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> clips;
            if (args.contains("clips"))
                for (const auto& c : args["clips"].is_array() ? args["clips"] : Json::array({args["clips"]}))
                    clips.push_back(clip(env, c).id);
            // Audio clips' keys found now, where they weren't.
            for (const auto& t : env.session.snapshot()->tracks)
                for (const auto& c : t.clips)
                    if (c.kind == model::ClipInfo::Kind::Audio &&
                        (clips.empty() || std::find(clips.begin(), clips.end(), c.id) != clips.end()))
                        (void)env.session.foundKey(c.id);
            env.session.flushSnapshot();
            Json keys = Json::array();
            for (const auto& c : env.session.detectKeys(clips))
            {
                auto entry = keyJson(c.key);
                entry["fit"] = std::round(c.fit * 1e3) / 1e3;
                keys.push_back(std::move(entry));
            }
            Json out{{"keys", keys}};
            if (keys.empty())
                out["note"] =
                    "Too few notes to judge (at least 8 notes, 5 of them different), and no audio clip "
                    "with a key.";
            return ToolResult::ok(out);
        }));
}
} // namespace fd::mcp
