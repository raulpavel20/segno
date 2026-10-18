// What MIDI effects follow (M3 P11, ADR 0074, docs/design/midi-effects.md §14): the song as they
// read it, which tracks make its harmony, and how often its chords may change. Each edit is one
// undo step.

#include "mcp/tools/Tools.h"
#include "model/MidiEffects.h"
#include "model/Sections.h"
#include "model/SongHarmony.h"

#include <algorithm>

namespace fd::mcp
{
using namespace tools;

namespace
{
/// A name spelled for assistants: sharps as #, flats as b.
std::string ascii(std::string text)
{
    for (const auto& [glyph, plain] :
         {std::pair<std::string, std::string>{"\xe2\x99\xaf", "#"}, {"\xe2\x99\xad", "b"}})
        for (auto at = text.find(glyph); at != std::string::npos; at = text.find(glyph, at + plain.size()))
            text.replace(at, glyph.size(), plain);
    return text;
}

std::string choices(const std::initializer_list<std::string_view>& ids)
{
    std::string out;
    for (const auto id : ids)
        out += (out.empty() ? "\"" : ", \"") + std::string(id) + "\"";
    return out;
}
} // namespace

void addMidiEffectTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    registry.tools.push_back(make(
        env,
        {"get_song_context", "Read what MIDI effects follow",
         "The song as MIDI effects read it (midi-effects.md §5): the key; the chords its notes make, with "
         "their bass note (each MIDI effect follows them by default); how often they may change; the "
         "sections, each with which time its name plays (\"Chorus 2\" is the chorus's second time); and "
         "every track's Harmony (auto, on, off) and whether its notes make the chords now. From `start` "
         "for `length` (default: the whole song).",
         object({{"start", position("Where to start (default: the song's start)")},
                 {"length", length("How much (default: to the song's end)")}}),
         result(
             {{"key", keyResultSchema()},
              {"chordsChange",
               string("How often the chords may change: \"bar\", \"half-bar\", \"beat\" or \"any\"")},
              {"chords",
               arrayOf(result({{"name", string("As the song row names it, sharps as #")}}), "By start", 0)},
              {"sections", arrayOf(result({{"name", string("")}}), "By start", 0)},
              {"tracks", arrayOf(result({{"track", string("")}, {"harmony", string("")}}), "In order", 0)}},
             {"key", "chordsChange", "chords", "sections", "tracks"}),
         reading},
        [&env](const Json& args, CallContext&)
        {
            env.session.flushSnapshot();
            const auto snap = env.session.snapshot();
            const auto t = snap->tempo;
            const double from = args.contains("start") ? positionArg(env, args["start"]) : 0.0;
            const double to = args.contains("length") ? from + lengthArg(env, args["length"])
                                                      : std::numeric_limits<double>::max();
            const auto* key = snap->keys.first() ? &*snap->keys.first() : nullptr;
            Json chords = Json::array();
            for (const auto& c : snap->songChords)
                if (c.endBeat > from && c.startBeat < to)
                    chords.push_back(
                        {{"name", ascii(c.name)},
                         {"start", positionJson(c.startBeat, t)},
                         {"end", positionJson(c.endBeat, t)},
                         {"root", music::pitchClassName(key, c.root, music::Accidentals::Ascii)},
                         {"bass", c.bass >= 0 ? Json(music::noteName(key, c.bass, music::Accidentals::Ascii))
                                              : Json(nullptr)}});
            Json sections = Json::array();
            std::map<std::string, int> times;
            for (const auto& s : snap->sections)
            {
                const int time = ++times[model::sectionBaseName(s.name)];
                const double start = t.beatsForSeconds(s.start), end = t.beatsForSeconds(s.end);
                if (end > from && start < to)
                    sections.push_back({{"section", formatId(IdKind::Section, s.id)},
                                        {"name", s.name},
                                        {"time", time},
                                        {"start", positionJson(start, t)},
                                        {"end", positionJson(end, t)}});
            }
            const auto counted = model::harmonyTracks(*snap);
            Json tracks = Json::array();
            for (const auto& tr : snap->tracks)
                tracks.push_back(
                    {{"track", formatId(IdKind::Track, tr.id)},
                     {"name", tr.name},
                     {"harmony", std::string(model::idOf(tr.harmony))},
                     {"makesChords", std::find(counted.begin(), counted.end(), tr.id) != counted.end()}});
            return ToolResult::ok({{"key", keyJson(snap->keys.first())},
                                   {"chordsChange", std::string(model::idOf(snap->chordChange))},
                                   {"chords", std::move(chords)},
                                   {"sections", std::move(sections)},
                                   {"tracks", std::move(tracks)}});
        }));

    ToolAnnotations edit;
    edit.idempotent = true;
    registry.tools.push_back(make(
        env,
        {"set_track_harmony", "Set whether a track makes the harmony",
         "Whether a track's notes make the song's chords, which MIDI effects follow (midi-effects.md §5.1): "
         "\"auto\" when it plays chords somewhere (never a Drum Kit's), \"on\" whatever it plays (a bass "
         "part gives the chords their bass; a muted track of chords guides the song unheard), \"off\" "
         "never. One undo step.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"harmony", {{"enum", {"auto", "on", "off"}}, {"description", "auto, on or off"}}}},
                {"track", "harmony"}),
         result({{"track", string("")}, {"harmony", string("")}}, {"track", "harmony"}), edit},
        [&env](const Json& args, CallContext&)
        {
            const auto t = track(env, args["track"]);
            const auto use = model::harmonyUseOf(args["harmony"].get<std::string>());
            if (!use)
                throw Problem{"Give harmony as " + choices({"auto", "on", "off"}) + "."};
            env.session.setTrackHarmony(t.id, *use);
            env.session.flushSnapshot();
            return ToolResult::ok(
                {{"track", formatId(IdKind::Track, t.id)}, {"harmony", std::string(model::idOf(*use))}});
        }));

    registry.tools.push_back(
        make(env,
             {"set_chords_change", "Set how often the song's chords change",
              "How often the chords the song's notes make may change (Song > Chords change): a shorter chord "
              "joins the one before it, as a passing chord does. \"beat\" by default. One undo step.",
              object({{"every",
                       {{"enum", {"bar", "half-bar", "beat", "any"}},
                        {"description", "bar, half-bar, beat or any"}}}},
                     {"every"}),
              result({{"chordsChange", string("")}}, {"chordsChange"}), edit},
             [&env](const Json& args, CallContext&)
             {
                 const auto change = model::chordChangeOf(args["every"].get<std::string>());
                 if (!change)
                     throw Problem{"Give every as " + choices({"bar", "half-bar", "beat", "any"}) + "."};
                 env.session.setChordChange(*change);
                 env.session.flushSnapshot();
                 return ToolResult::ok({{"chordsChange", std::string(model::idOf(*change))}});
             }));
}
} // namespace fd::mcp
