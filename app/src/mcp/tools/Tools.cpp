#include "mcp/tools/Tools.h"

#include "ModulePurposes.h"
#include "model/Editing.h"
#include "model/Modulators.h"
#include "model/Racks.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fd/device/Registry.h>

namespace fd::mcp
{
void registerAll(Registry& registry, ToolEnv& env)
{
    addReadTools(registry, env);
    addTransportTools(registry, env);
    addClipTools(registry, env);
    addAudioTools(registry, env);
    addNoteTools(registry, env);
    addModuleTools(registry, env);
    addWavetableTools(registry, env);
    addPresetTools(registry, env);
    addRenderTools(registry, env);
    addMixTools(registry, env);
    addAutomationTools(registry, env);
    addRecordingTools(registry, env);
    addSettingsTools(registry, env);
    addBrowserTools(registry, env);
    addGridTools(registry, env);
    addProjectTools(registry, env);
    addTrackTools(registry, env);
    addSectionTools(registry, env);
    addKeyTools(registry, env);
    addRackTools(registry, env);
    addModulatorTools(registry, env);
    addChordTools(registry, env);
    addControllerTools(registry, env);
    addMidiMapTools(registry, env);
    addToolkitTools(registry, env);
    addResources(registry, env);
    addPrompts(registry);
}
} // namespace fd::mcp

namespace fd::mcp::tools
{
namespace
{
double rounded(double value, double scale = 1e4)
{
    return std::round(value * scale) / scale;
}

const char* unitName(Unit unit)
{
    switch (unit)
    {
    case Unit::None: return "";
    case Unit::Decibels: return "dB";
    case Unit::Hertz: return "Hz";
    case Unit::Milliseconds: return "ms";
    case Unit::Seconds: return "s";
    case Unit::Percent: return "%";
    case Unit::Semitones: return "semitones";
    case Unit::Cents: return "cents";
    case Unit::Ratio: return "ratio";
    case Unit::Pan: return "pan (-1 left .. 1 right)";
    case Unit::Degrees: return "degrees";
    case Unit::Note: return "MIDI note (C3 = 60)";
    case Unit::Lufs:
        return "LUFS";
    case Unit::Dbtp:
        return "dBTP";
    }
    return "";
}

const char* kindName(ParamKind kind)
{
    switch (kind)
    {
    case ParamKind::Continuous: return "continuous";
    case ParamKind::Discrete: return "stepped";
    case ParamKind::Choice: return "choice";
    case ParamKind::Toggle: return "toggle";
    }
    return "continuous";
}

Json parameterJson(const ParamSpec& spec, std::optional<float> plain)
{
    Json p{{"id", std::string(spec.id)},
           {"name", std::string(spec.name)},
           {"description", std::string(spec.description)},
           {"kind", kindName(spec.kind)},
           {"unit", unitName(spec.unit)},
           {"min", rounded(spec.min)},
           {"max", rounded(spec.max)},
           {"default", rounded(spec.def)},
           {"defaultText", spec.toText(spec.def)},
           {"automatable", spec.hasFlag(ParamFlags::Automatable)}};
    if (plain)
    {
        p["value"] = rounded(*plain);
        p["text"] = spec.toText(*plain);
    }
    if (!spec.choices.empty())
    {
        Json choices = Json::array();
        for (const auto& c : spec.choices)
            choices.push_back(std::string(c));
        p["choices"] = std::move(choices);
    }
    return p;
}

std::string_view purposeOf(std::string_view id)
{
    for (const auto& p : modulePurposes)
        if (p.id == id)
            return p.text;
    return {};
}

Json viewJson(const std::string& state)
{
    Json view{{"collapsed", Json::array()}, {"folded", false}};
    const auto parsed = Json::parse(state, nullptr, false);
    if (parsed.is_object())
    {
        if (parsed.contains("collapsed") && parsed["collapsed"].is_array())
            view["collapsed"] = parsed["collapsed"];
        if (parsed.contains("folded") && parsed["folded"].is_boolean())
            view["folded"] = parsed["folded"];
        if (parsed.contains("page") && parsed["page"].is_string()) // a module with pages (ADR 0051)
            view["page"] = parsed["page"];
    }
    return view;
}

Json sectionsFromPanel(ui::DeviceUiContext& context)
{
    Json list = Json::array();
    if (const auto* factory = ui::findPanel(context.info.id))
    {
        const auto panel = factory->create(context);
        // A module with pages (ADR 0051): each section with its page's id and title.
        for (int page = 0; page < std::max(1, panel->pageCount()); ++page)
            for (auto* s : panel->pageSections(page))
            {
                Json section = {{"id", s->id()}, {"title", s->sectionTitle().toStdString()}};
                if (panel->pageCount() > 0)
                {
                    section["page"] = panel->pageId(page);
                    section["pageTitle"] = panel->pageTabs()->tabs()[page].toStdString();
                }
                list.push_back(std::move(section));
            }
    }
    return list;
}
} // namespace

// ---- building tools ----------------------------------------------------------------------------

Tool make(ToolEnv& env, Spec spec, Body body)
{
    Tool t;
    t.name = spec.name;
    t.title = spec.title;
    t.description = spec.description;
    t.inputSchema = std::move(spec.input);
    t.outputSchema = std::move(spec.output);
    t.annotations = spec.annotations;
    const bool group = spec.undoStep && !spec.annotations.readOnly;
    auto undoName = "Assistant: " + spec.name;
    std::replace(undoName.begin(), undoName.end(), '_', ' ');
    t.call = [&env, work = std::move(body), group, undoName](const Json& args, CallContext& context) {
        auto result = ToolResult::error("The DAW is closing; try again once it's running.");
        env.main.run([&] {
            env.session.flushSnapshot(); // edits made just before (by anyone) are visible
            try
            {
                std::optional<engine::Session::EditGroup> edits;
                if (group)
                    edits.emplace(env.session, undoName);
                result = work(args, context);
            }
            catch (const Problem& p)
            {
                result = ToolResult::error(p.message);
            }
            env.session.flushSnapshot();
        });
        return result;
    };
    return t;
}

// ---- schemas -----------------------------------------------------------------------------------

Json object(Json properties, const std::vector<std::string>& required)
{
    Json s{{"type", "object"},
           {"properties", properties.is_null() ? Json::object() : std::move(properties)},
           {"additionalProperties", false}};
    if (!required.empty())
        s["required"] = required;
    return s;
}

Json result(Json properties, const std::vector<std::string>& required)
{
    Json s{{"type", "object"}, {"properties", properties.is_null() ? Json::object() : std::move(properties)}};
    if (!required.empty())
        s["required"] = required;
    return s;
}

Json string(const std::string& description)
{
    return {{"type", "string"}, {"description", description}};
}

Json number(const std::string& description)
{
    return {{"type", "number"}, {"description", description}};
}

Json integer(const std::string& description, std::optional<int> min, std::optional<int> max)
{
    Json s{{"type", "integer"}, {"description", description}};
    if (min)
        s["minimum"] = *min;
    if (max)
        s["maximum"] = *max;
    return s;
}

Json boolean(const std::string& description)
{
    return {{"type", "boolean"}, {"description", description}};
}

Json arrayOf(Json items, const std::string& description, std::optional<int> minItems)
{
    Json s{{"type", "array"}, {"items", std::move(items)}, {"description", description}};
    if (minItems)
        s["minItems"] = *minItems;
    return s;
}

Json id(IdKind kind, const std::string& description)
{
    const char* example = kind == IdKind::Track ? "\"t:3\"" : kind == IdKind::Clip ? "\"c:12\"" : "\"m:7\"";
    return {{"type", {"string", "integer"}}, {"description", description + " (an id such as " + example + ")"}};
}

Json ids(IdKind kind, const std::string& description)
{
    return arrayOf(id(kind, "One of them"), description);
}

Json position(const std::string& description)
{
    return {{"type", {"number", "string"}},
            {"description", description +
                                " Beats (quarter notes) from the song's start, or \"bar.beat.sixteenth\" such as "
                                "\"5.1.1\"."}};
}

Json length(const std::string& description)
{
    return {{"type", {"number", "string"}},
            {"description", description + " Beats, or text such as \"2 bars\", \"3 beats\" or \"1/16\"."}};
}

Json positionResult()
{
    return result({{"beats", number("Beats from the song's start")}, {"bbt", string("bar.beat.sixteenth")}},
                  {"beats", "bbt"});
}

Json noteInput()
{
    return object(
        {{"pitch", {{"type", {"integer", "string"}}, {"description", "MIDI note number, or a name with C3 = 60 (\"D#4\")"}}},
         {"start",
          {{"type", {"number", "string"}},
           {"description", "Beats from the clip's start (0 is its first beat), or text such as \"1/8\" or \"2 beats\""}}},
         {"length", length("How long the note is.")},
         {"velocity", integer("1-127 (default 100)", 1, 127)},
         {"muted", boolean("Kept but not played (default false)")},
         {"chance", number("Percent: how likely it is to play each time it comes round (default 100)")},
         {"velocity_spread", integer("How far its velocity may move up or down each time, 0-127", 0, 127)},
         {"release_velocity", integer("Its note-off velocity, 0-127 (default 0: none)", 0, 127)}},
        {"pitch", "start", "length"});
}

// ---- reading arguments -----------------------------------------------------------------------------

model::TempoMap tempo(ToolEnv& env)
{
    return env.session.snapshot()->tempo;
}

double seconds(ToolEnv& env, double b)
{
    return tempo(env).secondsForBeats(b);
}

double beats(ToolEnv& env, double s)
{
    return tempo(env).beatsForSeconds(s);
}

model::TrackInfo track(ToolEnv& env, const Json& value)
{
    const auto id = parseId(value, IdKind::Track);
    if (!id)
        throw Problem{value.dump() + " isn't a track id: they look like \"t:3\" (read segno://project)."};
    for (const auto& t : env.session.snapshot()->tracks)
        if (t.id == *id)
            return t;
    throw Problem{"Track " + formatId(IdKind::Track, *id) +
                  " doesn't exist; read segno://project (or call get_project) for the current tracks."};
}

model::TrackInfo channel(ToolEnv& env, const Json& value)
{
    const auto id = parseId(value, IdKind::Track);
    if (!id)
        throw Problem{value.dump() + " isn't a track id: they look like \"t:3\" (read segno://project)."};
    if (const auto* t = env.session.snapshot()->channel(*id))
        return *t;
    throw Problem{"Track " + formatId(IdKind::Track, *id) +
                  " doesn't exist; read segno://project (or call get_project) for the tracks, returns and master."};
}

model::ClipInfo clip(ToolEnv& env, const Json& value)
{
    const auto id = parseId(value, IdKind::Clip);
    if (!id)
        throw Problem{value.dump() + " isn't a clip id: they look like \"c:12\" (read segno://project)."};
    if (const auto found = model::findClip(*env.session.snapshot(), *id))
        return *found->first;
    throw Problem{"Clip " + formatId(IdKind::Clip, *id) +
                  " doesn't exist; read segno://project (or call get_project) for the current clips."};
}

std::pair<model::DeviceSlot, model::TrackInfo> module(ToolEnv& env, const Json& value)
{
    const auto id = parseId(value, IdKind::Module);
    if (!id)
        throw Problem{value.dump() + " isn't a module id: they look like \"m:7\" (read segno://project)."};
    const auto snap = env.session.snapshot();
    for (const auto* list : {&snap->tracks, &snap->returns})
        for (const auto& t : *list)
            if (const auto* d = model::findDevice(t.devices, *id)) // in a rack too
                return {*d, t};
    if (const auto* d = model::findDevice(snap->master.devices, *id))
        return {*d, snap->master};
    throw Problem{"Module " + formatId(IdKind::Module, *id) +
                  " doesn't exist; read segno://project (or call get_project) for each track's chain."};
}

double positionArg(ToolEnv& env, const Json& value)
{
    std::string error;
    if (const auto v = parsePosition(value, tempo(env), &error))
        return *v;
    throw Problem{error};
}

double lengthArg(ToolEnv& env, const Json& value)
{
    std::string error;
    if (const auto v = parseLength(value, tempo(env), &error))
        return *v;
    throw Problem{error};
}

juce::File readableFile(const Json& value)
{
    auto path = value.is_string() ? juce::String(value.get<std::string>()).trim() : juce::String();
    if (path.isEmpty())
        throw Problem{"Give the file's path."};
    if (path == "~" || path.startsWith("~/"))
        path = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName() + path.substring(1);
    if (!juce::File::isAbsolutePath(path))
        throw Problem{"\"" + path.toStdString() + "\" isn't an absolute path."};
    const juce::File file(path);
    if (!file.existsAsFile())
        throw Problem{"There's no file at " + file.getFullPathName().toStdString() + "."};
    if (!file.hasReadAccess())
        throw Problem{"The DAW can't read " + file.getFullPathName().toStdString() + "."};
    return file;
}

// ---- describing things ---------------------------------------------------------------------------

double clipStartBeat(ToolEnv& env, const model::ClipInfo& c)
{
    return beats(env, c.start);
}

double noteOffsetBeats(ToolEnv& env, const model::ClipInfo& c)
{
    return c.loop.on() ? beats(env, c.loop.start) : clipStartBeat(env, c) - c.contentStartBeat;
}

Json db(double value)
{
    return std::isfinite(value) ? Json(std::round(value * 10.0) / 10.0) : Json(nullptr);
}

Json noteJson(const model::Note& note, double offsetBeats)
{
    Json j{{"pitch", note.pitch},
           {"name", pitchName(note.pitch)},
           {"start", rounded(note.start - offsetBeats, 1e6)},
           {"length", rounded(note.length, 1e6)},
           {"velocity", velocityToMidi(note.velocity)}};
    if (note.muted) // only when it is (piano-roll.md §9.4): it stays and doesn't play
        j["muted"] = true;
    // Chance, spread and release velocity (§10, §9.6) only when they aren't the plain note's.
    if (note.chance < 1.0f)
        j["chance"] = rounded(note.chance * 100.0, 10.0);
    const auto midi = [](float v) { return static_cast<int>(std::lround(v * 127.0f)); };
    if (midi(note.velocitySpread) > 0)
        j["velocity_spread"] = midi(note.velocitySpread);
    if (midi(note.releaseVelocity) > 0)
        j["release_velocity"] = midi(note.releaseVelocity);
    return j;
}

Json clipJson(ToolEnv& env, const model::ClipInfo& c, bool notes)
{
    const auto t = tempo(env);
    const double start = t.beatsForSeconds(c.start), end = t.beatsForSeconds(c.end());
    Json j{{"id", formatId(IdKind::Clip, c.id)},
           {"kind", c.kind == model::ClipInfo::Kind::Midi ? "midi" : "audio"},
           {"name", c.name},
           {"start", positionJson(start, t)},
           {"end", positionJson(end, t)},
           {"length", rounded(end - start, 1e6)},
           {"muted", c.muted},
           {"colour", c.colour}};
    // Its loop (ADR 0048): where it starts in the content and how long it is, in beats.
    if (c.loop.on())
    {
        j["loop"] = {{"start", rounded(t.beatsForSeconds(c.loop.start), 1e6)}, {"length", rounded(t.beatsForSeconds(c.loop.length), 1e6)}};
        if (c.kind == model::ClipInfo::Kind::Audio)
        {
            j["loop"]["startSeconds"] = roundTime(c.loop.start);
            j["loop"]["lengthSeconds"] = roundTime(c.loop.length);
        }
    }
    else
        j["loop"] = nullptr;
    if (const auto found = model::findClip(*env.session.snapshot(), c.id))
    {
        const auto& track = env.session.snapshot()->tracks[static_cast<size_t>(found->second)];
        j["track"] = formatId(IdKind::Track, track.id);
        // A clip with takes to choose from (ADR 0032): get_takes lists them.
        if (const auto* stack = c.stack != 0 ? track.stack(c.stack) : nullptr; stack != nullptr && stack->takes.size() >= 2)
            j["takes"] = stack->takes.size();
    }
    if (c.kind == model::ClipInfo::Kind::Audio)
    {
        // Fades and the crossfade into the next clip (fades.md §7), in ms.
        auto ms = [](double seconds) { return rounded(seconds * 1000.0, 1e3); };
        j["fadeIn"] = {{"ms", ms(c.fadeIn.length)}, {"bend", rounded(c.fadeIn.bend, 1e3)}};
        j["fadeOut"] = {{"ms", ms(c.fadeOut.length)}, {"bend", rounded(c.fadeOut.bend, 1e3)}};
        if (c.crossfadeOut)
        {
            static constexpr const char* placements[] = {"before", "centred", "after"};
            const auto at = model::placementOf(*c.crossfadeOut);
            j["crossfadeOut"] = {{"ms", ms(c.crossfadeOut->length)},
                                 {"startMs", ms(c.crossfadeOut->from)},
                                 {"placement", at ? Json(placements[static_cast<int>(*at)]) : Json(nullptr)},
                                 {"shape", model::shapeName(c.crossfadeOut->shape)}};
        }
        j["source"] = c.sourcePath;
        j["sourceOffsetSeconds"] = roundTime(c.offset);
        j["sourceSeconds"] = roundTime(c.sourceLength);
        // Its sound (audio-editor.md §6; set_audio, get_gain_line) and its file's format.
        j["sound"] = {
            {"gain", db(c.sound.gainDb <= model::minusInfinityDb ? -INFINITY : c.sound.gainDb)},
            {"reverse", c.sound.reversed},
            {"inverted", c.sound.inverted},
            {"channels", juce::String(model::channelModeName(c.sound.channels)).toLowerCase().toStdString()},
            {"gainLinePoints", c.sound.line.size()}};
        j["file"] = {{"channels", c.fileChannels},
                     {"sampleRate", c.fileSampleRate},
                     {"bitDepth", c.fileBitDepth},
                     {"seconds", roundTime(c.fileLength)}};
        // Its warping and transposition (audio-editor.md §7, §8.1; set_audio, set_warp_markers).
        Json markers = Json::array();
        for (const auto& m : c.warp.markers)
            markers.push_back({{"file", roundTime(m.file)}, {"beat", std::round(m.beat * 1e6) / 1e6}});
        j["warp"] = {{"on", c.warp.on},
                     {"mode", juce::String(model::warpModeName(c.warp.mode)).toLowerCase().toStdString()},
                     {"tails", juce::String(model::tailsName(c.warp.tails)).toLowerCase().toStdString()},
                     {"formants", c.warp.formants},
                     {"tempo", std::round(c.warp.tempo * 1e4) / 1e4},
                     {"sensitivity", std::round(c.warp.sensitivity * 1e3) / 1e3},
                     {"markers", std::move(markers)}};
        j["pitch"] = {{"transpose", c.pitch.transpose}, {"fine", c.pitch.fine}};
        // Its key (§8.2, §8.3; set_audio, detect_key): set, or found in its file (null while it's
        // being found, or with no clear key), and Follow key with the semitones it adds.
        j["key"] = keyJson(c.key);
        j["keyFound"] = c.keyFound;
        j["followKey"] = {{"on", c.followKey}, {"steps", c.followSteps}};
    }
    else
    {
        j["noteCount"] = c.notes.size();
        // Its controller lanes (piano-roll.md §11; get_controllers has their points) and what its
        // notes' chance is decided from (§10).
        Json lanes = Json::array();
        for (const auto& lane : c.controllers)
            lanes.push_back({{"controller", controllerJson(lane.controller)},
                             {"name", model::controllerName(lane.controller)},
                             {"points", lane.points.size()}});
        j["controllers"] = std::move(lanes);
        j["chanceSeed"] = c.chanceSeed;
        j["chanceKept"] = c.chanceKept; // piano-roll.md §26: it keeps the roll it played
        if (notes)
        {
            const double offset = noteOffsetBeats(env, c); // notes count from the clip's start (a loop's)
            Json list = Json::array();
            for (const auto& n : c.notes)
                list.push_back(noteJson(n, offset));
            j["notes"] = std::move(list);
        }
    }
    return j;
}

Json moduleSummary(const model::DeviceSlot& slot)
{
    Json j{{"id", formatId(IdKind::Module, slot.id)},
           {"name", slot.name},
           {"type", slot.kind != model::DeviceSlot::Kind::External ? slot.deviceId : std::string("plugin")},
           {"on", slot.enabled},
           {"instrument", slot.instrument}};
    if (slot.modulator != nullptr) // modulators.md §16: it moves parameters, it makes no sound
        j["modulator"] = true;
    // A rack (racks.md §10): its chains' modules, in order (get_rack has the rest).
    if (slot.rack != nullptr)
    {
        Json chains = Json::array();
        for (const auto& c : slot.rack->chains)
        {
            Json modules = Json::array();
            for (const auto& d : c.devices)
                modules.push_back(moduleSummary(d));
            chains.push_back({{"name", c.name}, {"modules", std::move(modules)}});
        }
        j["chains"] = std::move(chains);
    }
    return j;
}

std::string kindName(const model::TrackInfo& t)
{
    switch (t.role)
    {
    case model::TrackInfo::Role::Return: return "return";
    case model::TrackInfo::Role::Master: return "master";
    case model::TrackInfo::Role::Track: break;
    }
    return "track";
}

std::string panText(float pan)
{
    const int amount = static_cast<int>(std::lround(std::abs(pan) * 50.0f));
    return amount == 0 ? std::string("C") : std::to_string(amount) + (pan < 0.0f ? "L" : "R");
}

Json mixJson(ToolEnv& env, const model::TrackInfo& t)
{
    const auto volume = t.mix.volumeDb <= model::minusInfinityDb ? Json(nullptr) : db(t.mix.volumeDb);
    Json mix{{"volume", volume}, {"pan", rounded(t.mix.pan, 1e3)}, {"panText", panText(t.mix.pan)}};
    if (t.role != model::TrackInfo::Role::Master)
    {
        mix["mute"] = t.mix.mute;
        mix["solo"] = t.mix.solo;
    }
    if (t.role == model::TrackInfo::Role::Track)
    {
        Json sends = Json::array();
        const auto snap = env.session.snapshot();
        for (const auto& s : t.mix.sends)
        {
            const auto* r = snap->channel(s.returnId);
            sends.push_back({{"return", formatId(IdKind::Track, s.returnId)},
                             {"name", r != nullptr ? r->name : std::string()},
                             {"level", s.levelDb <= model::minusInfinityDb ? Json(nullptr) : db(s.levelDb)},
                             {"preFader", s.preFader}});
        }
        mix["sends"] = std::move(sends);
    }
    return mix;
}

Json trackJson(ToolEnv& env, const model::TrackInfo& t, bool notes)
{
    Json clips = Json::array(), chain = Json::array();
    for (const auto& c : t.clips)
        clips.push_back(clipJson(env, c, notes));
    for (const auto& d : t.devices)
        chain.push_back(moduleSummary(d));
    return {{"id", formatId(IdKind::Track, t.id)},
            {"kind", kindName(t)},
            {"name", t.name},
            {"colour", t.colour},
            {"instrument", t.instrument.empty() ? Json(nullptr) : Json(t.instrument)},
            {"clips", std::move(clips)},
            {"chain", std::move(chain)},
            {"mix", mixJson(env, t)},
            {"input", t.holdsClips() ? inputJson(t.input) : Json(nullptr)}};
}

Json inputJson(const model::InputInfo& in)
{
    auto monitor = in.monitor == model::Monitor::In ? "in" : in.monitor == model::Monitor::Off ? "off" : "auto";
    Json records = nullptr;
    if (in.records)
        records = *in.records == model::RecordKind::Audio ? "audio" : "midi";
    return {{"audio", in.audio.empty() ? Json(nullptr) : Json(in.audio)},
            {"midi", in.midi.empty() ? Json(nullptr) : Json(in.midi)},
            {"channel", in.channel == 0 ? Json("all") : Json(in.channel)},
            {"monitor", monitor},
            {"records", records},
            {"armed", in.armed},
            {"selectedForLiveMidi", in.softArmed}};
}

Json inputsJson(ToolEnv& env)
{
    Json audio = Json::array(), midi = Json::array();
    for (const auto& i : env.session.audioInputs())
        audio.push_back(i.id);
    for (const auto& i : env.session.midiInputs())
        midi.push_back({{"id", i.id}, {"name", i.name}});
    return {{"audio", std::move(audio)}, {"midi", std::move(midi)}};
}

Json projectJson(ToolEnv& env)
{
    auto& s = env.session;
    const auto snap = s.snapshot();
    const auto t = snap->tempo;
    const auto file = s.file();
    const auto [loopStart, loopEnd] = s.loopRange();
    Json tracks = Json::array(), returns = Json::array();
    for (const auto& track : snap->tracks)
        tracks.push_back(trackJson(env, track));
    for (const auto& r : snap->returns)
        returns.push_back(trackJson(env, r));
    return {{"name", file ? file->getFileNameWithoutExtension().toStdString() : std::string("Untitled")},
            {"file", file ? Json(file->getFullPathName().toStdString()) : Json(nullptr)},
            {"unsaved", s.hasUnsavedChanges()},
            {"tempo", rounded(t.bpm, 1e3)},
            {"timeSignature", {{"numerator", t.numerator}, {"denominator", t.denominator}}},
            {"key", keyJson(snap->keys.first())},
            {"edgeFades", snap->edgeFades},
            {"reducedLatency", s.reducedLatency()},
            {"midiInputsMapped",
             [&s]
             {
                 std::vector<std::string> inputs; // midi-learn.md §9
                 for (const auto& m : s.midiMappings())
                     if (std::find(inputs.begin(), inputs.end(), m.source.device) == inputs.end())
                         inputs.push_back(m.source.device);
                 return inputs;
             }()},
            {"end", positionJson(t.beatsForSeconds(snap->contentEnd()), t)},
            {"loop",
             {{"on", s.isLooping()},
              {"start", positionJson(t.beatsForSeconds(loopStart), t)},
              {"end", positionJson(t.beatsForSeconds(loopEnd), t)}}},
            {"undo", {{"available", s.canUndo()}, {"next", s.undoDescription()}}},
            {"redo", {{"available", s.canRedo()}, {"next", s.redoDescription()}}},
            {"tracks", std::move(tracks)},
            {"returns", std::move(returns)},
            {"master", trackJson(env, snap->master)},
            {"automationLanes", automationLanesJson(env)},
            {"sections",
             [&snap, &t]
             {
                 Json list = Json::array(); // the song row (ADR 0049): list_sections has them too
                 for (const auto& sec : snap->sections)
                     list.push_back({{"section", formatId(IdKind::Section, sec.id)},
                                     {"name", sec.name},
                                     {"start", positionJson(t.beatsForSeconds(sec.start), t)},
                                     {"end", positionJson(t.beatsForSeconds(sec.end), t)}});
                 return list;
             }()},
            {"inputs", inputsJson(env)}};
}

Json transportJson(ToolEnv& env)
{
    auto& s = env.session;
    const auto t = tempo(env);
    const auto [loopStart, loopEnd] = s.loopRange();
    return {{"playing", s.isPlaying()},
            {"recording", s.isRecording()},
            {"position", positionJson(t.beatsForSeconds(s.position()), t)},
            {"loop",
             {{"on", s.isLooping()},
              {"start", positionJson(t.beatsForSeconds(loopStart), t)},
              {"end", positionJson(t.beatsForSeconds(loopEnd), t)}}},
            {"metronome", s.isMetronomeOn()},
            {"tempo", rounded(t.bpm, 1e3)}};
}

Json modulatorSectionsJson(model::ModulatorKind kind)
{
    Json list = Json::array();
    for (const auto& s : model::modulatorSections(kind))
        list.push_back({{"id", std::string(s.id)}, {"title", std::string(s.title)}});
    return list;
}

Json modulatorJson(ToolEnv& env, const model::DeviceSlot& slot)
{
    Json routes = Json::array();
    if (slot.modulator == nullptr)
        return {{"routes", routes}};
    for (const auto& r : slot.modulator->routes)
    {
        Json route{{"target", paramRefJson(env, {r.channel, r.key})},
                   {"amount", std::round(r.amount * 1000.0) / 10.0}};
        if (r.copy > 0)
            route["copy"] = r.copy + 1; // its own copy with Per Target, counted from 1
        routes.push_back(std::move(route));
    }
    Json j{{"routes", std::move(routes)}};
    if (model::hasKeep(slot.modulator->kind))
        j["kept"] = slot.modulator->kept; // ADR 0062
    // The LFO's drawn shape (§12.1): what Shape "Drawn" plays.
    if (slot.modulator->kind == model::ModulatorKind::Lfo)
        if (auto handle = env.session.openModulator(slot.id))
        {
            const auto c = handle->host().drawnCurve();
            j["curve"] = c.points.empty() ? Json{{"shape", c.shape}} : Json{{"points", c.points}};
        }
    return j;
}

Json sectionsOf(ToolEnv& env, engine::DeviceHandle& handle)
{
    const std::string type(handle.deviceId());
    if (const auto it = env.sections.find(type); it != env.sections.end())
        return it->second;
    auto list = sectionsFromPanel(handle.context());
    env.sections[type] = list;
    return list;
}

Json moduleJson(ToolEnv& env, const model::DeviceSlot& slot, const model::TrackInfo& t)
{
    Json j = moduleSummary(slot);
    j["track"] = formatId(IdKind::Track, t.id);
    for (size_t i = 0; i < t.devices.size(); ++i)
        if (t.devices[i].id == slot.id)
            j["position"] = i;
    const auto view = viewJson(env.session.deviceViewState(slot.id));
    j["folded"] = view["folded"];
    // How late it plays its track (a linear-phase EQ's delay): what a change to it costs live.
    j["latencyMs"] = std::round(env.session.moduleLatencyMs(slot.id) * 10.0) / 10.0;
    j["bypassedWhileMonitoring"] = env.session.bypassedWhileMonitoring(slot.id); // recording.md §22.1
    // A plugin's name with its maker and format, as the inspector shows it (selection.md §8.2).
    if (slot.kind == model::DeviceSlot::Kind::External)
        j["plugin"] = env.session.moduleType(slot.id);
    if (slot.takesSidechain) // ADR 0025: where its sidechain comes from, and what could feed it
    {
        Json choices = Json::array();
        for (auto source : env.session.sidechainSources(slot.id))
            choices.push_back(formatId(IdKind::Track, source));
        j["sidechain"] = {
            {"source", slot.sidechainSource ? Json(formatId(IdKind::Track, *slot.sidechainSource)) : Json(nullptr)},
            {"possibleSources", std::move(choices)}};
    }
    Json parameters = Json::array();
    // In a rack: the macro that sets a parameter (racks.md §4).
    const auto* holder = model::rackHolding(t.devices, slot.id);
    auto handle = env.session.openDevice(slot.id);
    if (handle == nullptr && slot.rack != nullptr)
    {
        handle = env.session.openRack(slot.id); // its macros, as its parameters
        j["rack"] = rackJson(env, slot, t);
    }
    // A modulator (modulators.md §16): its parameters as a module's, then what it moves.
    auto modulator =
        handle == nullptr && slot.modulator != nullptr ? env.session.openModulator(slot.id) : nullptr;
    if (modulator != nullptr)
        j["modulator"] = modulatorJson(env, slot);
    if (handle != nullptr || modulator != nullptr)
    {
        auto& context = handle != nullptr ? handle->context() : modulator->context();
        for (size_t i = 0; i < context.info.params.size(); ++i)
            if (const auto& spec = context.info.params[i]; !spec.hasFlag(ParamFlags::Hidden))
            {
                auto p = parameterJson(spec, context.params.getPlain(i));
                int macro = -1;
                if (holder != nullptr &&
                    model::mappingOf(*holder->rack, slot.id, std::string(spec.id), &macro) != nullptr)
                    p["setByMacro"] = {{"rack", formatId(IdKind::Module, holder->id)},
                                       {"macro", macro + 1},
                                       {"name", holder->rack->macros[static_cast<size_t>(macro)].name}};
                if (slot.rack != nullptr && i < static_cast<size_t>(model::RackInfo::macroCount) &&
                    model::morphSets(*slot.rack, static_cast<int>(i)))
                    p["setByMorph"] = true; // racks.md §18.3
                // Its automation (C4): its lane, points, range and whether it plays.
                if (auto a = parameterAutomationJson(env, {t.id, "m" + std::to_string(slot.id) + "/" + std::string(spec.id)});
                    !a.is_null())
                    p["automation"] = std::move(a);
                parameters.push_back(std::move(p));
            }
        Json sections = Json::array(), pages = Json::array();
        for (auto s :
             handle != nullptr ? sectionsOf(env, *handle) : modulatorSectionsJson(slot.modulator->kind))
        {
            const auto& collapsed = view["collapsed"];
            s["collapsed"] = std::find(collapsed.begin(), collapsed.end(), s["id"]) != collapsed.end();
            // Pages (ADR 0051), in order, with their sections.
            if (s.contains("page"))
            {
                if (pages.empty() || pages.back()["id"] != s["page"])
                    pages.push_back(
                        {{"id", s["page"]}, {"title", s["pageTitle"]}, {"sections", Json::array()}});
                pages.back()["sections"].push_back(s["id"]);
                s.erase("pageTitle");
            }
            sections.push_back(std::move(s));
        }
        j["sections"] = std::move(sections);
        if (!pages.empty())
        {
            j["page"] = view.value("page", pages.front()["id"].get<std::string>());
            j["pages"] = std::move(pages);
        }
        if (context.device != nullptr)
        {
            auto state = Json::object();
            context.device->writeExtraState(state);
            if (!state.empty())
                j["state"] = std::move(state);
        }
    }
    else
    {
        for (const auto& p : env.session.pluginParameters(slot.id))
            parameters.push_back({{"id", p.id}, {"name", p.name}, {"value", rounded(p.value)}, {"text", p.text}});
        j["parameterValues"] = "normalised 0-1 (a third-party plugin)";
    }
    j["parameters"] = std::move(parameters);
    return j;
}

Json moduleTypesJson(ToolEnv& env, const std::string& kind)
{
    Json list = Json::array();
    for (const auto& d : env.session.availableDevices())
    {
        // The modulators come after, with what each is for.
        if (d.modulator || (kind == "instrument" && !d.instrument) || (kind == "effect" && d.instrument) ||
            kind == "modulator")
            continue;
        Json entry{{"type", d.native ? d.key.substr(3) : d.key},
                   {"name", d.name},
                   {"kind", d.instrument ? "instrument" : "effect"},
                   {"native", d.native},
                   {"category", d.category}};
        if (d.native)
            entry["description"] = std::string(purposeOf(d.key.substr(3)));
        list.push_back(std::move(entry));
    }
    // The modulators (modulators.md §16): modules that move other parameters on their track.
    if (kind.empty() || kind == "modulator")
        for (const auto m : model::allModulators)
        {
            const auto& info = model::modulatorInfo(m);
            list.push_back({{"type", std::string(info.id)},
                            {"name", std::string(info.name)},
                            {"kind", "modulator"},
                            {"native", true},
                            {"category", "Modulators"},
                            {"description", std::string(model::modulatorPurpose(m))}});
        }
    return list;
}

Json moduleTypeJson(ToolEnv& env, const std::string& type)
{
    if (const auto m = model::modulatorKind(type))
    {
        const auto& info = model::modulatorInfo(*m);
        Json parameters = Json::array();
        for (const auto& spec : info.params)
            if (!spec.hasFlag(ParamFlags::Hidden))
                parameters.push_back(parameterJson(spec, std::nullopt));
        return {{"type", std::string(info.id)},
                {"name", std::string(info.name)},
                {"kind", "modulator"},
                {"native", true},
                {"category", "Modulators"},
                {"description", std::string(model::modulatorPurpose(*m))},
                {"parameters", std::move(parameters)},
                {"sections", modulatorSectionsJson(*m)}};
    }
    const auto* factory = findDevice(type);
    if (factory == nullptr)
    {
        for (auto entry : moduleTypesJson(env))
            if (entry["type"] == type)
                return entry; // a scanned plugin: no more is known before it's added
        return nullptr;
    }
    const auto& info = factory->info();
    Json parameters = Json::array();
    for (const auto& spec : info.params)
        if (!spec.hasFlag(ParamFlags::Hidden))
            parameters.push_back(parameterJson(spec, std::nullopt));
    Json sections;
    if (const auto it = env.sections.find(type); it != env.sections.end())
        sections = it->second;
    else
    {
        // A throwaway instance, just to read its panel's sections.
        auto device = factory->create();
        ui::LocalParamBinding binding(*device);
        ui::DeviceUiContext context{device->info(), binding, nullptr, device.get()};
        sections = sectionsFromPanel(context);
        env.sections[type] = sections;
    }
    return {{"type", std::string(info.id)},
            {"name", std::string(info.name)},
            {"kind", info.kind == DeviceKind::Instrument ? "instrument" : "effect"},
            {"native", true},
            {"category", std::string(info.category)},
            {"description", std::string(purposeOf(info.id))},
            {"parameters", std::move(parameters)},
            {"sections", std::move(sections)}};
}
// ---- parameters as tools name them -------------------------------------------------------------

namespace
{
std::string lowerCase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

Json paramRefSchema()
{
    return {
        {"module", id(IdKind::Module, "A module's parameter: the module (with parameter)")},
        {"track", id(IdKind::Track, "A channel's mix: the track, return track or master (with parameter)")},
        {"parameter",
         string("The module's parameter (id or name), or for a channel \"volume\", \"pan\" or \"send\"")},
        {"return", id(IdKind::Track, "For a send: the return track it sends to")}};
}

ParamRef paramRef(ToolEnv& env, const Json& args)
{
    if (!args.contains("parameter") || !args["parameter"].is_string())
        throw Problem{
            "Name the parameter: a module's parameter id or name with module, or \"volume\", \"pan\" or "
            "\"send\" with track."};
    const auto parameter = args["parameter"].get<std::string>();
    auto& s = env.session;
    if (args.contains("module"))
    {
        const auto [slot, owner] = module(env, args["module"]);
        const auto prefix = "m" + std::to_string(slot.id) + "/";
        const auto choices = s.automatableParameters(owner.id);
        for (int pass = 0; pass < 2; ++pass) // ids first, then names
            for (const auto& c : choices)
                if (c.key.rfind(prefix, 0) == 0 && (pass == 0 ? c.key.substr(prefix.size()) == parameter
                                                              : lowerCase(c.name) == lowerCase(parameter)))
                    return {{owner.id, c.key}, c.name};
        throw Problem{slot.name + " has no automatable parameter \"" + parameter +
                      "\" (get_module lists them; settings such as lookahead can't be automated)."};
    }
    if (!args.contains("track"))
        throw Problem{"Give module (for a module's parameter) or track (for volume, pan or a send)."};
    const auto t = channel(env, args["track"]);
    const auto p = lowerCase(parameter);
    if (p == "volume" || p == "pan")
        return {{t.id, p}, p == "volume" ? "Volume" : "Pan"};
    if (p == "send")
    {
        if (t.role != model::TrackInfo::Role::Track)
            throw Problem{"Only tracks send; " + t.name + " is a " + kindName(t) + "."};
        if (!args.contains("return"))
            throw Problem{"Name the return track the send goes to (return)."};
        const auto r = channel(env, args["return"]);
        if (r.role != model::TrackInfo::Role::Return)
            throw Problem{r.name + " isn't a return track."};
        return {{t.id, "send/" + std::to_string(r.id)}, "Send " + r.name};
    }
    throw Problem{
        "A channel's parameters are \"volume\", \"pan\" and \"send\"; for a module's parameter give module."};
}

Json paramRefJson(ToolEnv& env, const engine::Session::ParamTarget& t)
{
    Json j{{"track", formatId(IdKind::Track, t.channel)}, {"name", env.session.paramName(t).first}};
    if (t.key.rfind("send/", 0) == 0)
    {
        j["parameter"] = "send";
        j["return"] = formatId(IdKind::Track, std::stoull(t.key.substr(5)));
    }
    else if (t.key.rfind('m', 0) == 0 && t.key.find('/') != std::string::npos)
    {
        const auto slash = t.key.find('/');
        j["module"] = formatId(IdKind::Module, std::stoull(t.key.substr(1, slash - 1)));
        j["parameter"] = t.key.substr(slash + 1);
    }
    else
        j["parameter"] = t.key;
    return j;
}

} // namespace fd::mcp::tools
