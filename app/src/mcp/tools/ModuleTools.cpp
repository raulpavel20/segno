// Module chains (mcp.md §5): ours, through the host binding like their panels (ADR 0021), and
// third-party plugins as far as their parameters go.

#include "mcp/tools/Tools.h"

#include <fd/audiofile/AudioFile.h>
#include <fd/device/Registry.h>

#include <cmath>

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

    registry.tools.push_back(make(
        env,
        {"add_module", "Add a module",
         "Adds a module (an instrument or effect) to a track's chain: one of ours or a scanned plugin. Returns the "
         "new module with its parameters.",
         object({{"track", id(IdKind::Track, "The track")},
                 {"type", string("A type from list_module_types (\"eq\", \"saturator\"), or its name")},
                 {"position", integer("Where in the chain, from 0 (default: the end)", 0)}},
                {"track", "type"}),
         moduleResultSchema(), adds},
        [&env](const Json& args, CallContext&) {
            const auto t = track(env, args["track"]);
            const auto key = typeKey(env, args["type"].get<std::string>());
            const auto id = env.session.addDevice(t.id, key, args.value("position", -1));
            if (id == 0)
                throw Problem{"The module couldn't be created (" + key + ")."};
            env.session.flushSnapshot();
            const auto [slot, owner] = module(env, formatId(IdKind::Module, id));
            return ToolResult::ok(moduleJson(env, slot, owner));
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
        {"move_module", "Move a module", "Moves a module within its chain.",
         object({{"module", id(IdKind::Module, "The module")}, {"position", integer("Its new place, from 0", 0)}},
                {"module", "position"}),
         result({{"chain", arrayOf(result({{"id", string("")}}), "The chain now, in order", std::nullopt)}}, {"chain"}),
         changes},
        [&env](const Json& args, CallContext&) {
            const auto [slot, owner] = module(env, args["module"]);
            env.session.moveDevice(slot.id, args["position"].get<int>());
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
            if (auto handle = env.session.openDevice(slot.id))
            {
                auto& context = handle->context();
                for (const auto& [key, value] : args["values"].items())
                {
                    const auto i = parameterIndex(context.info, key);
                    if (i >= context.info.params.size())
                    {
                        problems.push_back({{"id", key}, {"problem", "no such parameter"}});
                        continue;
                    }
                    const auto& spec = context.info.params[i];
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
         "Loads an audio file into a Sampler module (and sets its root key when the file names one).",
         object({{"module", id(IdKind::Module, "The Sampler")}, {"path", string("The file's absolute path")}},
                {"module", "path"}),
         result({{"seconds", number("The sample's length")}, {"rootKey", {{"type", {"integer", "null"}}}}}, {"seconds"}),
         changes},
        [&env](const Json& args, CallContext&) {
            const auto [slot, owner] = module(env, args["module"]);
            auto handle = env.session.openDevice(slot.id);
            if (handle == nullptr || handle->deviceId() != "sampler")
                throw Problem{formatId(IdKind::Module, slot.id) + " isn't a Sampler."};
            const auto file = readableFile(args["path"]);
            std::string error;
            const auto audio = audiofile::decode(file.getFullPathName().toStdString(), &error);
            if (!audio)
                throw Problem{file.getFullPathName().toStdString() + " isn't audio the Sampler can load (" + error + ")."};
            auto& context = handle->context();
            context.params.applyExtraState({{"sample.path", file.getFullPathName().toStdString()}});
            if (audio->rootKey)
                if (const auto root = parameterIndex(context.info, "root"); root < context.info.params.size())
                {
                    context.params.beginGesture(root);
                    context.params.setPlain(root, static_cast<float>(*audio->rootKey));
                    context.params.endGesture(root);
                }
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                   {"seconds", roundTime(audio->numFrames() / audio->sampleRate)},
                                   {"rootKey", audio->rootKey ? Json(*audio->rootKey) : Json(nullptr)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_module_view", "Collapse sections or fold a module",
         "Sets how a module's panel shows: which sections are collapsed (the rest are expanded), and whether the "
         "module is folded to a strip. Section ids come from get_module. View state only: never an undo step.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"collapsed", arrayOf(string("A section id"), "The sections to collapse", 0)},
                 {"folded", boolean("Folded to a strip")}},
                {"module"}),
         result({{"collapsed", arrayOf(string(""), "", std::nullopt)}, {"folded", boolean("")}}, {"collapsed", "folded"}),
         changes, false},
        [&env](const Json& args, CallContext&) {
            const auto [slot, owner] = module(env, args["module"]);
            auto view = Json::parse(env.session.deviceViewState(slot.id), nullptr, false);
            if (!view.is_object())
                view = Json::object();
            if (args.contains("collapsed"))
            {
                auto handle = env.session.openDevice(slot.id);
                if (handle == nullptr)
                    throw Problem{"Third-party plugins have no sections."};
                std::vector<std::string> known, collapsed;
                for (const auto& s : sectionsOf(env, *handle))
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
            env.session.setDeviceViewState(slot.id, view.empty() ? std::string() : view.dump());
            return ToolResult::ok({{"module", formatId(IdKind::Module, slot.id)},
                                   {"collapsed", view.value("collapsed", Json::array())},
                                   {"folded", view.value("folded", false)}});
        }));
}
} // namespace fd::mcp
