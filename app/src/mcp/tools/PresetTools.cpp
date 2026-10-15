// Presets (M3 A1, ADR 0044, docs/design/presets.md §14): listing, loading and saving a module's
// presets, and A/B, as its header does. Assistants never delete presets: deleting files is yours.

#include "mcp/tools/Tools.h"

#include <fd/device/Registry.h>

namespace fd::mcp
{
using namespace tools;

namespace
{
const char* sourceName(Preset::Source source)
{
    switch (source)
    {
    case Preset::Source::Builtin:
        return "defaults";
    case Preset::Source::Factory:
        return "factory";
    case Preset::Source::User:
        return "yours";
    case Preset::Source::Default:
        return "your default";
    }
    return "factory";
}

Json presetJson(const PresetLibrary& library, const DeviceInfo& info, const Preset& p)
{
    Json json{{"id", p.key()},
              {"name", p.name},
              {"source", sourceName(p.source)},
              {"tags", p.tags},
              {"favourite", library.isFavourite(info.id, p.key())}};
    if (!p.description.empty())
        json["description"] = p.description;
    if (!p.author.empty())
        json["author"] = p.author;
    if (p.isYours() && !p.folder.empty())
        json["folder"] = p.folder;
    if (presetTooNew(info, p))
        json["loadable"] = false; // saved by a newer Segno
    return json;
}

/// One of our modules, with its presets.
struct ModulePresets
{
    model::DeviceSlot slot;
    model::TrackInfo owner;
    std::unique_ptr<PresetHost> host;
    const DeviceInfo* info = nullptr;
};

ModulePresets modulePresets(ToolEnv& env, const Json& value)
{
    auto [slot, owner] = module(env, value);
    auto host = env.session.presetHost(slot.id);
    const auto* info = engine::Session::nativeInfo("fd:" + slot.deviceId); // a modulator's too
    if (host == nullptr || info == nullptr)
        throw Problem{"Presets are for Segno's own modules; " + slot.name + " is a third-party plugin."};
    return {std::move(slot), std::move(owner), std::move(host), info};
}

/// A preset by its id from list_presets, or its name (a factory preset first, unless `source`
/// says "yours").
const Preset& findPreset(PresetLibrary& library, const DeviceInfo& info, const std::string& wanted,
                         const std::string& source)
{
    library.rescan();
    const auto all = library.presetsFor(info);
    for (const auto* p : all)
        if (p->key() == wanted)
            return *p;
    const Preset* found = nullptr;
    for (const auto* p : all)
        if (p->name == wanted && (source.empty() || sourceName(p->source) == source))
            if (found == nullptr || (found->isYours() && !p->isYours() && source.empty()))
                found = p;
    if (found == nullptr)
        throw Problem{"There's no preset \"" + wanted + "\" for the " + std::string(info.name) +
                      "; list_presets lists them."};
    if (presetTooNew(info, *found))
        throw Problem{"\"" + found->name + "\" was saved by a newer Segno and can't be loaded here."};
    return *found;
}
} // namespace

void addPresetTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations adds;

    registry.tools.push_back(make(
        env,
        {"list_presets", "List a module's presets",
         "Lists the presets of one of Segno's modules (or of a module type): its own Default settings, the "
         "factory "
         "presets, then yours, with tags, descriptions and favourites. For a module, also which preset it "
         "came from, "
         "whether it has changed since, and which of its A/B settings shows.",
         object({{"module", id(IdKind::Module, "A module (its presets, and what it loaded)")},
                 {"type", string("Or a module type from list_module_types (\"compressor\")")}}),
         result({{"presets",
                  arrayOf(result({{"id", string("Its id: give it to load_preset")}, {"name", string("")}}),
                          "", std::nullopt)}},
                {"presets"}),
         reads, false},
        [&env](const Json& args, CallContext&)
        {
            auto& library = env.session.presetLibrary();
            Json out;
            const DeviceInfo* info = nullptr;
            std::unique_ptr<PresetHost> host;
            if (args.contains("module"))
            {
                auto m = modulePresets(env, args["module"]);
                info = m.info;
                host = std::move(m.host);
                out["module"] = formatId(IdKind::Module, m.slot.id);
            }
            else if (args.contains("type"))
            {
                const auto type = args["type"].get<std::string>();
                info = engine::Session::nativeInfo(type.starts_with("fd:") ? type : "fd:" + type);
                if (info == nullptr)
                    throw Problem{"There's no module type \"" + type +
                                  "\" of Segno's own; list_module_types lists them."};
            }
            else
                throw Problem{"Give a module, or a module type."};
            library.rescan();
            Json list = Json::array();
            for (const auto* p : library.presetsFor(*info))
                list.push_back(presetJson(library, *info, *p));
            out["presets"] = std::move(list);
            if (host != nullptr)
            {
                PresetController c(*info, *host);
                out["loaded"] = c.loadedName();
                out["modified"] = c.modified();
                out["showing"] = c.onB() ? "B" : "A";
            }
            out["hasDefault"] = library.defaultFor(*info) != nullptr;
            return ToolResult::ok(out);
        }));

    registry.tools.push_back(make(
        env,
        {"load_preset", "Load a preset",
         "Loads a preset into one of Segno's modules (into the A or B setting that shows), by its id from "
         "list_presets or its name. Its parameters change at once; parameters with automation keep following "
         "their "
         "lanes. One undo step.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"preset", string("The preset's id from list_presets, or its name")},
                 {"source",
                  {{"type", "string"},
                   {"enum", {"factory", "yours"}},
                   {"description", "Which, when a factory preset and one of yours share the name"}}}},
                {"module", "preset"}),
         moduleResultSchema(), changes},
        [&env](const Json& args, CallContext&)
        {
            auto m = modulePresets(env, args["module"]);
            auto& library = env.session.presetLibrary();
            const auto& p = findPreset(library, *m.info, args["preset"].get<std::string>(),
                                       args.value("source", std::string()));
            PresetController(*m.info, *m.host).load(p);
            env.session.flushSnapshot();
            auto json = moduleJson(env, m.slot, m.owner);
            json["preset"] = p.name;
            return ToolResult::ok(json, "Loaded " + p.name + " into " + m.slot.name + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"save_preset", "Save a preset",
         "Saves a module's settings as a preset of yours (in Documents/Segno/Presets, shared with Segno's "
         "plugins in "
         "other DAWs). One of yours with the same name is refused unless replace is true. The module then "
         "shows it as "
         "its preset.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"name", string("The preset's name (it's the file's name)")},
                 {"tags", arrayOf(string("A tag"), "Tags to find it by (\"Drums\", \"Subtle\")", 0)},
                 {"description", string("What it's for, in a line")},
                 {"author", string("Who made it")},
                 {"replace", boolean("Write over one of yours with this name")}},
                {"module", "name"}),
         result({{"preset", result({{"id", string("")}, {"name", string("")}}, {"id", "name"})}}, {"preset"}),
         adds, false},
        [&env](const Json& args, CallContext&)
        {
            auto m = modulePresets(env, args["module"]);
            std::vector<std::string> tags;
            if (args.contains("tags"))
                for (const auto& t : args["tags"])
                    tags.push_back(t.get<std::string>());
            PresetController c(*m.info, *m.host);
            const auto saved = c.saveAs(args["name"].get<std::string>(), std::move(tags),
                                        args.value("description", std::string()),
                                        args.value("author", std::string()), args.value("replace", false));
            if (saved.preset == nullptr)
                throw Problem{saved.problem + (saved.exists ? " Pass replace: true to write over it." : "")};
            return ToolResult::ok(
                {{"module", formatId(IdKind::Module, m.slot.id)},
                 {"preset", presetJson(env.session.presetLibrary(), *m.info, *saved.preset)}});
        }));

    registry.tools.push_back(make(
        env,
        {"compare", "Compare A/B",
         "Every one of Segno's modules holds two settings, A and B, to compare. \"switch\" shows the other "
         "one (one "
         "undo step); \"copy\" makes the one not shown the same as the one shown.",
         object({{"module", id(IdKind::Module, "The module")},
                 {"action",
                  {{"type", "string"}, {"enum", {"switch", "copy"}}, {"description", "Switch, or copy"}}}},
                {"module", "action"}),
         result({{"showing", string("A or B")}}, {"showing"}), changes},
        [&env](const Json& args, CallContext&)
        {
            auto m = modulePresets(env, args["module"]);
            PresetController c(*m.info, *m.host);
            if (args["action"].get<std::string>() == "switch")
                c.switchCompare();
            else
                c.copyCompare();
            return ToolResult::ok({{"module", formatId(IdKind::Module, m.slot.id)},
                                   {"showing", c.onB() ? "B" : "A"},
                                   {"preset", c.loadedName()}});
        }));
}
} // namespace fd::mcp
