// Module chains (mcp.md §5): ours, through the host binding like their panels (ADR 0021), and
// third-party plugins as far as their parameters go.

#include "Sampler.h"
#include "mcp/tools/Tools.h"
#include "model/Modulators.h"
#include "model/Racks.h"

#include <cmath>
#include <fd/audiofile/AudioFile.h>
#include <fd/device/Library.h>
#include <fd/device/Registry.h>

namespace fd::mcp
{
using namespace tools;

namespace
{
std::string lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// The catalogue key ("fd:eq", "ext:...") for a type given as our id, a key, or a name.
std::string typeKey(ToolEnv& env, const std::string& type)
{
    const auto devices = env.session.availableDevices();
    for (const auto& d : devices)
        if (d.key == type || (d.native && d.key == "fd:" + type))
            return d.key;
    for (const auto& d : devices)
        if (lower(d.name) == lower(type))
            return d.key;
    throw Problem{"There's no module type \"" + type + "\"; list_module_types (or segno://modules) lists them."};
}

/// The modulator a type names: its id ("mod-lfo") or its name ("LFO").
std::optional<model::ModulatorKind> modulatorType(const std::string& type)
{
    if (const auto kind = model::modulatorKind(type))
        return kind;
    for (const auto kind : model::allModulators)
        if (lower(std::string(model::modulatorInfo(kind).name)) == lower(type))
            return kind;
    return std::nullopt;
}

std::size_t parameterIndex(const DeviceInfo& info, const std::string& key)
{
    for (size_t i = 0; i < info.params.size(); ++i)
        if (info.params[i].id == key && !info.params[i].hasFlag(ParamFlags::Hidden))
            return i;
    for (size_t i = 0; i < info.params.size(); ++i)
        if (lower(std::string(info.params[i].name)) == lower(key) && !info.params[i].hasFlag(ParamFlags::Hidden))
            return i;
    return info.params.size();
}
} // namespace

void addModuleTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations adds;
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations removes;
    removes.destructive = true;
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"add_module", "Add a module",
         "Adds a module (an instrument or effect) to a chain: a track's, a return track's or the master's; "
         "one of "
         "ours or a scanned plugin. One of ours starts from your default for it, if you set one, or from a "
         "preset "
         "you name (list_presets). Into a rack's chain with rack and chain; type \"rack\" adds an empty rack "
         "(racks.md). A modulator (\"mod-lfo\", \"mod-random\", \"mod-envelope\", \"mod-follower\", "
         "\"mod-steps\") goes in a track's or return track's own chain; route_modulator gives it what it "
         "moves. "
         "Returns the new module with its parameters.",
         object(
             {{"track", id(IdKind::Track, "The track, return track or master")},
              {"type",
               string("A type from list_module_types (\"eq\", \"saturator\"), or its name; \"rack\" for "
                      "an empty rack")},
              {"position", integer("Where in the chain, from 0 (default: the end)", 0)},
              {"rack", id(IdKind::Module, "Into this rack's chain instead of the track's")},
              {"path", string("With type \"rack\": a .segnorack file to load (search finds them as racks)")},
              {"chain", integer("The rack's chain, from 1 (default: 1)", 1)},
              {"preset", string("One of ours: a preset to start from, by its id or name (list_presets)")}},
             {"track", "type"}),
         moduleResultSchema(), adds},
        [&env](const Json& args, CallContext&)
        {
            const auto t = channel(env, args["track"]); // modules go on tracks, returns and the master
            // A modulator (modulators.md §16): where it's asked for along a track's chain.
            if (const auto kind = modulatorType(args["type"].get<std::string>()))
            {
                if (args.contains("rack"))
                    throw Problem{"Modulators go in a track's own chain for now, not into a rack's."};
                if (args.contains("preset"))
                    throw Problem{"Modulators have no presets yet; set_parameters sets them up."};
                if (t.role == model::TrackInfo::Role::Master)
                    throw Problem{
                        "The master can't have modulators yet: put one on a track or a return track."};
                const auto id = env.session.addModulator(t.id, *kind, args.value("position", -1));
                if (id == 0)
                    throw Problem{"The modulator couldn't be added."};
                env.session.flushSnapshot();
                const auto [slot, owner] = module(env, formatId(IdKind::Module, id));
                return ToolResult::ok(moduleJson(env, slot, owner));
            }
            if (lower(args["type"].get<std::string>()) == "rack")
            {
                std::uint64_t rack = 0;
                Json missing = Json::array();
                if (args.contains("path"))
                {
                    const auto loaded = env.session.loadRack(
                        readableFile(args["path"]), {.channel = t.id, .index = args.value("position", -1)});
                    if (loaded.rack == 0)
                        throw Problem{loaded.error.empty() ? std::string("It isn't a rack file.")
                                                           : loaded.error};
                    rack = loaded.rack;
                    for (const auto& m : loaded.missing)
                        missing.push_back(m);
                }
                else
                    rack = env.session.addRack(t.id, args.value("position", -1));
                if (rack == 0)
                    throw Problem{"The rack couldn't be added."};
                env.session.flushSnapshot();
                const auto [slot, owner] = module(env, formatId(IdKind::Module, rack));
                auto json = moduleJson(env, slot, owner);
                if (!missing.empty())
                    json["missing"] = std::move(missing); // plugins not installed, left out
                return ToolResult::ok(json);
            }
            const auto key = typeKey(env, args["type"].get<std::string>());
            // A preset is found before the module is made: a wrong name adds nothing.
            const Preset* preset = nullptr;
            const DeviceInfo* info = nullptr;
            if (args.contains("preset"))
            {
                const auto* factory = key.starts_with("fd:") ? findDevice(key.substr(3)) : nullptr;
                if (factory == nullptr)
                    throw Problem{"Presets are for Segno's own modules."};
                info = &factory->info();
                auto& library = env.session.presetLibrary();
                library.rescan();
                const auto wanted = args["preset"].get<std::string>();
                for (const auto* p : library.presetsFor(*info))
                    if ((p->key() == wanted || p->name == wanted) && (preset == nullptr || preset->isYours()))
                        preset = p;
                if (preset == nullptr || presetTooNew(*info, *preset))
                    throw Problem{"There's no preset \"" + wanted + "\" for the " + std::string(info->name) +
                                  " that this Segno can load; list_presets lists them."};
            }
            std::uint64_t id = 0;
            if (args.contains("rack"))
            {
                const auto [rack, owner] = module(env, args["rack"]);
                if (rack.rack == nullptr || owner.id != t.id)
                    throw Problem{formatId(IdKind::Module, rack.id) + " isn't a rack on that track."};
                const int chain = args.value("chain", 1) - 1;
                if (chain < 0 || chain >= static_cast<int>(rack.rack->chains.size()))
                    throw Problem{"The rack has " + std::to_string(rack.rack->chains.size()) +
                                  " chains, counted from 1."};
                id = env.session.addDevice(engine::Session::ChainPlace{.channel = t.id,
                                                                       .rack = rack.id,
                                                                       .chain = chain,
                                                                       .index = args.value("position", -1)},
                                           key);
            }
            else
                id = env.session.addDevice(t.id, key, args.value("position", -1));
            if (id == 0)
                throw Problem{"The module couldn't be created (" + key + ")."};
            if (preset != nullptr)
                if (auto host = env.session.presetHost(id))
                    PresetController(*info, *host).load(*preset);
            env.session.flushSnapshot();
            const auto [slot, owner] = module(env, formatId(IdKind::Module, id));
            auto json = moduleJson(env, slot, owner);
            if (preset != nullptr)
                json["preset"] = preset->name;
            return ToolResult::ok(json);
        }));

    registry.tools.push_back(make(env,
                                  {"remove_module", "Remove a module", "Removes a module from its chain (undoable).",
                                   object({{"module", id(IdKind::Module, "The module")}}, {"module"}),
                                   result({{"removed", string("Its id")}}, {"removed"}), removes},
                                  [&env](const Json& args, CallContext&) {
                                      const auto [slot, owner] = module(env, args["module"]);
                                      env.session.removeDevice(slot.id);
                                      return ToolResult::ok({{"removed", formatId(IdKind::Module, slot.id)}});
                                  }));

    registry.tools.push_back(make(
        env,
        {"move_module", "Move a module",
         "Moves a module within its track: along its chain, into a rack's chain (rack and chain), or out of "
         "a rack "
         "into the track's chain (rack: null). A rack never goes into a rack.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"position", integer("Its new place in the chain it goes to, from 0", 0)},
                 {"rack",
                  {{"type", {"string", "null"}},
                   {"description", "A rack on its track to move it into; null: "
                                   "the track's own chain"}}},
                 {"chain", integer("The rack's chain, from 1 (default: 1)", 1)}},
                {"module", "position"}),
         result({{"chain", arrayOf(result({{"id", string("")}}), "The chain now, in order", std::nullopt)}},
                {"chain"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = module(env, args["module"]);
            const int position = args["position"].get<int>();
            if (!args.contains("rack"))
                env.session.moveDevice(slot.id, position); // along its own chain
            else
            {
                engine::Session::ChainPlace place{.channel = owner.id, .index = position};
                if (!args["rack"].is_null())
                {
                    const auto [rack, rackOwner] = module(env, args["rack"]);
                    if (rack.rack == nullptr || rackOwner.id != owner.id)
                        throw Problem{formatId(IdKind::Module, rack.id) +
                                      " isn't a rack on the module's track."};
                    place.rack = rack.id;
                    place.chain = args.value("chain", 1) - 1;
                }
                // To where it ends up, along its own chain as elsewhere.
                if (const auto from = env.session.placeOf(slot.id);
                    from && from->rack == place.rack && from->chain == place.chain && position >= from->index)
                    place.index = position + 1;
                if (!env.session.moveDevice(slot.id, place))
                    throw Problem{"It can't go there (a rack never goes into a rack)."};
            }
            env.session.flushSnapshot();
            return ToolResult::ok({{"chain", trackJson(env, track(env, formatId(IdKind::Track, owner.id)))["chain"]}});
        }));

    registry.tools.push_back(make(env,
                                  {"set_module_on", "Turn a module on or off", "Turns a module on, or off (bypassed).",
                                   object({{"module", id(IdKind::Module, "The module")}, {"on", boolean("On")}},
                                          {"module", "on"}),
                                   result({{"on", boolean("")}}, {"on"}), changes},
                                  [&env](const Json& args, CallContext&) {
                                      const auto [slot, owner] = module(env, args["module"]);
                                      env.session.setDeviceEnabled(slot.id, args["on"].get<bool>());
                                      return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                                             {"on", args["on"].get<bool>()}});
                                  }));

    registry.tools.push_back(make(
        env,
        {"set_parameters", "Set parameters",
         "Sets any number of a module's parameters at once (one undo step). Keys are parameter ids (or names) from "
         "get_module; values are plain values in the parameter's unit (dB, Hz, ms, %), true/false for switches, or "
         "text the parameter understands (\"-6 dB\", \"1.2k\", \"LP 24\", \"C3\"). Out-of-range values are clamped. "
         "Third-party plugins take normalised values from 0 to 1.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"values", {{"type", "object"},
                             {"description", "{parameter id: value or text}"},
                             {"additionalProperties", {{"type", {"number", "string", "boolean"}}}}}}},
                {"module", "values"}),
         result({{"set", arrayOf(result({{"id", string("")}, {"value", number("")}, {"text", string("")}}), "", std::nullopt)},
                 {"problems", arrayOf(result({{"id", string("")}, {"problem", string("")}}), "Values not set, and why", std::nullopt)}},
                {"set", "problems"}),
         changes},
        [&env](const Json& args, CallContext&) {
            const auto [slot, owner] = module(env, args["module"]);
            Json set = Json::array(), problems = Json::array();
            auto handle = env.session.openDevice(slot.id);
            if (handle == nullptr && slot.rack != nullptr)
                handle = env.session.openRack(slot.id); // a rack's macros
            auto modulator =
                handle == nullptr && slot.modulator != nullptr ? env.session.openModulator(slot.id) : nullptr;
            const auto* holder = model::rackHolding(owner.devices, slot.id);
            if (handle != nullptr || modulator != nullptr)
            {
                auto& context = handle != nullptr ? handle->context() : modulator->context();
                for (const auto& [key, value] : args["values"].items())
                {
                    const auto i = parameterIndex(context.info, key);
                    if (i >= context.info.params.size())
                    {
                        problems.push_back({{"id", key}, {"problem", "no such parameter"}});
                        continue;
                    }
                    const auto& spec = context.info.params[i];
                    // Set by a rack's macro: it follows the macro (racks.md §4).
                    int macro = -1;
                    if (holder != nullptr &&
                        model::mappingOf(*holder->rack, slot.id, std::string(spec.id), &macro) != nullptr)
                    {
                        problems.push_back(
                            {{"id", key},
                             {"problem", "set by macro " + std::to_string(macro + 1) + " (" +
                                             holder->rack->macros[static_cast<size_t>(macro)].name +
                                             "): set_macro moves it, unmap_macro frees it"}});
                        continue;
                    }
                    // A rack's macro Morph sets (racks.md §18.3): it follows Morph.
                    if (slot.rack != nullptr && i < static_cast<size_t>(model::RackInfo::macroCount) &&
                        model::morphSets(*slot.rack, static_cast<int>(i)))
                    {
                        problems.push_back(
                            {{"id", key},
                             {"problem",
                              "set by Morph: set_morph moves it; set_morph with on false, or set_macro "
                              "with locked true, frees it"}});
                        continue;
                    }
                    std::optional<float> plain;
                    if (value.is_boolean())
                        plain = value.get<bool>() ? spec.max : spec.min;
                    else if (value.is_number())
                        plain = value.get<float>();
                    else
                        plain = spec.fromText(value.get<std::string>());
                    if (!plain || !std::isfinite(*plain))
                    {
                        problems.push_back({{"id", key}, {"problem", "\"" + value.get<std::string>() + "\" isn't a value it understands"}});
                        continue;
                    }
                    const float applied = spec.sanitize(*plain);
                    context.params.beginGesture(i);
                    context.params.setPlain(i, applied);
                    context.params.endGesture(i);
                    const float now = context.params.getPlain(i);
                    Json entry{{"id", std::string(spec.id)}, {"value", now}, {"text", spec.toText(now)}};
                    if (std::abs(applied - *plain) > 1e-4f * std::max(1.0f, std::abs(*plain)))
                        entry["clamped"] = true;
                    set.push_back(std::move(entry));
                }
            }
            else
            {
                const auto parameters = env.session.pluginParameters(slot.id);
                for (const auto& [key, value] : args["values"].items())
                {
                    const auto it = std::find_if(parameters.begin(), parameters.end(), [&](const auto& p) {
                        return p.id == key || lower(p.name) == lower(key);
                    });
                    if (it == parameters.end())
                    {
                        problems.push_back({{"id", key}, {"problem", "no such parameter"}});
                        continue;
                    }
                    if (!value.is_number() && !value.is_boolean())
                    {
                        problems.push_back({{"id", key}, {"problem", "third-party plugins take values from 0 to 1"}});
                        continue;
                    }
                    const float v = value.is_boolean() ? (value.get<bool>() ? 1.0f : 0.0f) : value.get<float>();
                    env.session.setPluginParameter(slot.id, it->id, v);
                    Json entry{{"id", it->id}, {"value", std::clamp(v, 0.0f, 1.0f)}};
                    for (const auto& p : env.session.pluginParameters(slot.id))
                        if (p.id == it->id)
                            entry["text"] = p.text;
                    if (v < 0.0f || v > 1.0f)
                        entry["clamped"] = true;
                    set.push_back(std::move(entry));
                }
            }
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)}, {"set", set}, {"problems", problems}});
        }));

    registry.tools.push_back(make(
        env,
        {"load_sample", "Load a sample",
         "Loads a sample (an audio file) or a multisample (an SFZ file of zones by key and velocity) into a "
         "Sampler module, with what the file sets: a sample's root key; an SFZ's mode, loop, velocity to "
         "volume "
         "and amp envelope. Says how many zones loaded, which samples are missing and what in the file isn't "
         "played. One undo step.",
         object({{"module", id(IdKind::Module, "The Sampler")}, {"path", string("The file's absolute path")}},
                {"module", "path"}),
         result({{"seconds", number("The sample's length; a multisample's longest")},
                 {"rootKey", {{"type", {"integer", "null"}}}},
                 {"zones", integer("How many zones")},
                 {"missing", integer("Zones whose sample isn't there")},
                 {"ignored",
                  arrayOf(string(""), "What the SFZ file says that the Sampler doesn't play", std::nullopt)}},
                {"seconds", "zones"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = module(env, args["module"]);
            const auto file = readableFile(args["path"]);
            std::string error;
            const auto loaded = env.session.loadSample(slot.id, file, &error);
            if (!loaded)
                throw Problem{error == "isn't a Sampler"
                                  ? formatId(IdKind::Module, slot.id) + " isn't a Sampler."
                                  : file.getFullPathName().toStdString() +
                                        " isn't a sample or SFZ file the Sampler can load (" + error + ")."};
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                   {"seconds", roundTime(loaded->seconds)},
                                   {"rootKey", loaded->rootKey ? Json(*loaded->rootKey) : Json(nullptr)},
                                   {"zones", loaded->zones},
                                   {"missing", loaded->missing},
                                   {"ignored", loaded->ignored}});
        }));

    registry.tools.push_back(make(
        env,
        {"get_zones", "Get a Sampler's zones",
         "Lists what a Sampler module plays: one sample across the keyboard, or a multisample's zones, each "
         "with "
         "its sample, keys, velocities, root, tuning and gain, and its turn in a round robin or its share of "
         "a "
         "random choice. Keys are note names (C3 = 60).",
         object({{"module", id(IdKind::Module, "The Sampler")}}, {"module"}),
         result({{"path", string("The sample or SFZ file")},
                 {"zones", arrayOf(result({}), "Its zones", std::nullopt)}},
                {"zones"}),
         reading},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = module(env, args["module"]);
            auto handle = env.session.openDevice(slot.id);
            const auto* sampler =
                handle != nullptr ? dynamic_cast<const devices::Sampler*>(handle->context().device) : nullptr;
            if (sampler == nullptr)
                throw Problem{formatId(IdKind::Module, slot.id) + " isn't a Sampler."};
            Json zones = Json::array();
            const auto map = sampler->map();
            if (map == nullptr)
                return ToolResult::ok(
                    {{"module", formatId(IdKind::Module, slot.id)}, {"path", nullptr}, {"zones", zones}});
            const auto& root = handle->context().info.params[devices::Sampler::Root];
            const auto note = [&root](int key) { return root.toText(static_cast<float>(key)); };
            for (const auto& z : map->zones)
            {
                Json j{{"sample", z.file},
                       {"keys", {{"low", note(z.loKey)}, {"high", note(z.hiKey)}}},
                       {"velocities", {{"low", z.loVel}, {"high", z.hiVel}}},
                       {"root", map->single
                                    ? root.toText(handle->context().params.getPlain(devices::Sampler::Root))
                                    : note(z.root)},
                       {"tuneCents", z.tune},
                       {"gainDb", std::round(200.0 * std::log10(std::max(1.0e-6f, z.gain))) / 10.0}};
                if (z.sample == nullptr)
                    j["missing"] = true;
                if (z.seqLength > 1)
                    j["roundRobin"] = std::to_string(z.seqPosition) + " of " + std::to_string(z.seqLength);
                if (z.loRand > 0.0f || z.hiRand < 1.0f)
                    j["random"] = {z.loRand, z.hiRand};
                zones.push_back(std::move(j));
            }
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                   {"path", map->path},
                                   {"zones", std::move(zones)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_module_sidechain", "Set a module's sidechain",
         "Sets what a module with a sidechain input (Compressor, Gate) listens to instead of its own input: another "
         "track, heard after its modules and fader (even when muted), or none. get_module lists the tracks that "
         "can feed it (never one that would make a loop). One undo step.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"source", {{"type", {"string", "null"}}, {"description", "A track, or null for none"}}}},
                {"module", "source"}),
         result({{"source", {{"type", {"string", "null"}}, {"description", "The track it listens to, or null"}}}},
                {"source"}),
         changes},
        [&env](const Json& args, CallContext&) {
            const auto [slot, owner] = module(env, args["module"]);
            if (!slot.takesSidechain)
                throw Problem{"This module has no sidechain input (the Compressor and the Gate have one)."};
            std::optional<std::uint64_t> source;
            std::string name = "its own input";
            if (!args["source"].is_null())
            {
                const auto t = track(env, args["source"]);
                source = t.id;
                name = t.name;
            }
            if (!env.session.setSidechainSource(slot.id, source))
                throw Problem{"That track can't feed this module: it's the module's own track, or its sound already "
                              "depends on that track through a sidechain (a loop)."};
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                   {"source", source ? Json(formatId(IdKind::Track, *source)) : Json(nullptr)}},
                                  slot.name + " now listens to " + name + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"set_module_view", "Collapse sections, fold a module or turn its page",
         "Sets how a module's panel shows: which sections are collapsed (the rest are expanded), whether the "
         "module is folded to a strip, and, for a module with pages, the page shown. Section and page ids "
         "come "
         "from get_module. View state only: never an undo step.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"collapsed", arrayOf(string("A section id"), "The sections to collapse", 0)},
                 {"folded", boolean("Folded to a strip")},
                 {"page", string("A page id (a module with pages)")}},
                {"module"}),
         result({{"collapsed", arrayOf(string(""), "", std::nullopt)},
                 {"folded", boolean("")},
                 {"page", string("The page shown, for a module with pages")}},
                {"collapsed", "folded"}),
         changes, false},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = module(env, args["module"]);
            auto view = Json::parse(env.session.deviceViewState(slot.id), nullptr, false);
            if (!view.is_object())
                view = Json::object();
            if (args.contains("collapsed"))
            {
                auto handle = env.session.openDevice(slot.id);
                if (handle == nullptr && slot.modulator == nullptr)
                    throw Problem{"Third-party plugins have no sections."};
                std::vector<std::string> known, collapsed;
                for (const auto& s : handle != nullptr ? sectionsOf(env, *handle)
                                                       : modulatorSectionsJson(slot.modulator->kind))
                    known.push_back(s["id"].get<std::string>());
                for (const auto& s : args["collapsed"])
                {
                    const auto id = s.get<std::string>();
                    if (std::find(known.begin(), known.end(), id) == known.end())
                    {
                        std::string list;
                        for (const auto& k : known)
                            list += (list.empty() ? "" : ", ") + k;
                        throw Problem{"There's no section \"" + id + "\"; this module has " + list + "."};
                    }
                    collapsed.push_back(id);
                }
                std::sort(collapsed.begin(), collapsed.end());
                if (collapsed.empty())
                    view.erase("collapsed");
                else
                    view["collapsed"] = collapsed;
            }
            if (args.contains("folded"))
            {
                if (args["folded"].get<bool>())
                    view["folded"] = true;
                else
                    view.erase("folded");
            }
            // The page (ADR 0051): stored by id, the first page left out.
            std::vector<std::string> pages;
            if (auto handle = env.session.openDevice(slot.id))
                for (const auto& s : sectionsOf(env, *handle))
                    if (s.contains("page") && (pages.empty() || pages.back() != s["page"].get<std::string>()))
                        pages.push_back(s["page"].get<std::string>());
            if (args.contains("page"))
            {
                const auto page = args["page"].get<std::string>();
                if (pages.empty())
                    throw Problem{slot.name + " has no pages: all of its sections show side by side."};
                if (std::find(pages.begin(), pages.end(), page) == pages.end())
                {
                    std::string list;
                    for (const auto& k : pages)
                        list += (list.empty() ? "" : ", ") + k;
                    throw Problem{"There's no page \"" + page + "\"; this module has " + list + "."};
                }
                if (page == pages.front())
                    view.erase("page");
                else
                    view["page"] = page;
            }
            env.session.setDeviceViewState(slot.id, view.empty() ? std::string() : view.dump());
            Json out = {{"module", formatId(IdKind::Module, slot.id)},
                        {"collapsed", view.value("collapsed", Json::array())},
                        {"folded", view.value("folded", false)}};
            if (!pages.empty())
                out["page"] = view.value("page", pages.front());
            return ToolResult::ok(std::move(out));
        }));
}
} // namespace fd::mcp
