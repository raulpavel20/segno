#include "mcp/tools/Tools.h"

#include "ModulePurposes.h"
#include "model/Editing.h"

#include <fd/device/Registry.h>

#include <cmath>

namespace fd::mcp
{
void registerAll(Registry& registry, ToolEnv& env)
{
    addReadTools(registry, env);
    addTransportTools(registry, env);
    addClipTools(registry, env);
    addNoteTools(registry, env);
    addModuleTools(registry, env);
    addRenderTools(registry, env);
    addMixTools(registry, env);
    addProjectTools(registry, env);
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
    }
    return view;
}

Json sectionsFromPanel(ui::DeviceUiContext& context)
{
    Json list = Json::array();
    if (const auto* factory = ui::findPanel(context.info.id))
    {
        const auto panel = factory->create(context);
        for (auto* s : panel->sections())
            list.push_back({{"id", s->id()}, {"title", s->sectionTitle().toStdString()}});
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
         {"velocity", integer("1-127 (default 100)", 1, 127)}},
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
            for (const auto& d : t.devices)
                if (d.id == *id)
                    return {d, t};
    for (const auto& d : snap->master.devices)
        if (d.id == *id)
            return {d, snap->master};
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

Json db(double value)
{
    return std::isfinite(value) ? Json(std::round(value * 10.0) / 10.0) : Json(nullptr);
}

Json noteJson(const model::Note& note, double offsetBeats)
{
    return {{"pitch", note.pitch},
            {"name", pitchName(note.pitch)},
            {"start", rounded(note.start - offsetBeats, 1e6)},
            {"length", rounded(note.length, 1e6)},
            {"velocity", velocityToMidi(note.velocity)}};
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
           {"length", rounded(end - start, 1e6)}};
    if (const auto found = model::findClip(*env.session.snapshot(), c.id))
        j["track"] = formatId(IdKind::Track, env.session.snapshot()->tracks[static_cast<size_t>(found->second)].id);
    if (c.kind == model::ClipInfo::Kind::Audio)
    {
        j["source"] = c.sourcePath;
        j["sourceOffsetSeconds"] = roundTime(c.offset);
        j["sourceSeconds"] = roundTime(c.sourceLength);
    }
    else
    {
        j["noteCount"] = c.notes.size();
        if (notes)
        {
            const double offset = start - c.contentStartBeat; // notes count from the content start
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
    return {{"id", formatId(IdKind::Module, slot.id)},
            {"name", slot.name},
            {"type", slot.kind == model::DeviceSlot::Kind::Native ? slot.deviceId : std::string("plugin")},
            {"on", slot.enabled},
            {"instrument", slot.instrument}};
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
            {"mix", mixJson(env, t)}};
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
            {"end", positionJson(t.beatsForSeconds(snap->contentEnd()), t)},
            {"loop",
             {{"on", s.isLooping()},
              {"start", positionJson(t.beatsForSeconds(loopStart), t)},
              {"end", positionJson(t.beatsForSeconds(loopEnd), t)}}},
            {"undo", {{"available", s.canUndo()}, {"next", s.undoDescription()}}},
            {"redo", {{"available", s.canRedo()}, {"next", s.redoDescription()}}},
            {"tracks", std::move(tracks)},
            {"returns", std::move(returns)},
            {"master", trackJson(env, snap->master)}};
}

Json transportJson(ToolEnv& env)
{
    auto& s = env.session;
    const auto t = tempo(env);
    const auto [loopStart, loopEnd] = s.loopRange();
    return {{"playing", s.isPlaying()},
            {"position", positionJson(t.beatsForSeconds(s.position()), t)},
            {"loop",
             {{"on", s.isLooping()},
              {"start", positionJson(t.beatsForSeconds(loopStart), t)},
              {"end", positionJson(t.beatsForSeconds(loopEnd), t)}}},
            {"metronome", s.isMetronomeOn()},
            {"tempo", rounded(t.bpm, 1e3)}};
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
    if (auto handle = env.session.openDevice(slot.id))
    {
        auto& context = handle->context();
        for (size_t i = 0; i < context.info.params.size(); ++i)
            if (!context.info.params[i].hasFlag(ParamFlags::Hidden))
                parameters.push_back(parameterJson(context.info.params[i], context.params.getPlain(i)));
        Json sections = Json::array();
        for (auto s : sectionsOf(env, *handle))
        {
            const auto& collapsed = view["collapsed"];
            s["collapsed"] = std::find(collapsed.begin(), collapsed.end(), s["id"]) != collapsed.end();
            sections.push_back(std::move(s));
        }
        j["sections"] = std::move(sections);
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
        if ((kind == "instrument" && !d.instrument) || (kind == "effect" && d.instrument))
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
    return list;
}

Json moduleTypeJson(ToolEnv& env, const std::string& type)
{
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
} // namespace fd::mcp::tools
