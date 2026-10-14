// Racks (M3 P2, docs/design/racks.md §10, ADR 0056): modules grouped into racks of chains side by
// side, each chain's mixer row, and eight macros mapped to parameters in the rack. Through
// engine::Session, like the chain view's edits (ADR 0021).

#include "mcp/tools/Tools.h"
#include "model/Racks.h"

#include <cmath>

namespace fd::mcp
{
using namespace tools;

namespace
{
/// The rack an argument names, with the track that holds it.
std::pair<model::DeviceSlot, model::TrackInfo> rackArg(ToolEnv& env, const Json& value)
{
    auto [slot, owner] = module(env, value);
    if (slot.rack == nullptr)
        throw Problem{formatId(IdKind::Module, slot.id) + " isn't a rack; group_modules makes one."};
    return {slot, owner};
}

int chainArg(const model::RackInfo& rack, const Json& value)
{
    const int chain = value.get<int>() - 1;
    if (chain < 0 || chain >= static_cast<int>(rack.chains.size()))
        throw Problem{"The rack has " + std::to_string(rack.chains.size()) + " chains, counted from 1."};
    return chain;
}

/// A value 0 .. 1 as a percentage, to a tenth.
double percent(float v)
{
    return std::round(v * 1000.0) / 10.0;
}

int snapshotArg(const model::RackInfo& rack, const Json& value)
{
    const int snapshot = value.get<int>() - 1;
    if (snapshot < 0 || snapshot >= static_cast<int>(rack.snapshots.size()))
        throw Problem{rack.snapshots.empty()
                          ? std::string("The rack has no snapshots yet; store_snapshot stores one.")
                          : "The rack has " + std::to_string(rack.snapshots.size()) +
                                " snapshots, counted from 1."};
    return snapshot;
}

int macroArg(const Json& value)
{
    const int macro = value.get<int>() - 1;
    if (macro < 0 || macro >= model::RackInfo::macroCount)
        throw Problem{"A rack's macros are 1 to 8."};
    return macro;
}

/// A parameter's plain value (its unit's) as its control's travel (0 .. 1), through the session.
float travelOf(ToolEnv& env, std::uint64_t channel, std::uint64_t module, const std::string& param,
               const Json& value)
{
    const engine::Session::ParamTarget target{channel, "m" + std::to_string(module) + "/" + param};
    std::optional<float> travel;
    if (value.is_number())
        travel = env.session.paramFromPlain(target, value.get<double>());
    else if (value.is_string())
        travel = env.session.paramFromText(target, value.get<std::string>());
    if (!travel)
        throw Problem{"\"" + value.dump() + "\" isn't a value " + param + " understands."};
    return *travel;
}

Json plainOf(ToolEnv& env, std::uint64_t channel, std::uint64_t module, const std::string& param,
             float travel)
{
    const engine::Session::ParamTarget target{channel, "m" + std::to_string(module) + "/" + param};
    Json j{{"text", env.session.paramText(target, travel)}};
    if (const auto plain = env.session.paramPlain(target, travel))
        j["value"] = std::round(*plain * 1000.0) / 1000.0;
    return j;
}
} // namespace

namespace tools
{
Json rackJson(ToolEnv& env, const model::DeviceSlot& slot, const model::TrackInfo& owner)
{
    const auto& rack = *slot.rack;
    Json chains = Json::array();
    for (size_t i = 0; i < rack.chains.size(); ++i)
    {
        const auto& c = rack.chains[i];
        Json modules = Json::array();
        for (const auto& d : c.devices)
            modules.push_back(moduleSummary(d));
        chains.push_back({{"chain", i + 1},
                          {"name", c.name},
                          {"on", c.on},
                          {"solo", c.solo},
                          {"volumeDb", db(c.volumeDb)},
                          {"pan", panText(c.pan)},
                          {"modules", modules}});
    }
    Json macros = Json::array();
    for (size_t i = 0; i < rack.macros.size(); ++i)
    {
        const auto& m = rack.macros[i];
        Json mappings = Json::array();
        for (const auto& map : m.mappings)
        {
            Json j{{"module", formatId(IdKind::Module, map.module)},
                   {"parameter", map.param},
                   {"name", map.paramName},
                   {"from", plainOf(env, owner.id, map.module, map.param, map.from)},
                   {"to", plainOf(env, owner.id, map.module, map.param, map.to)},
                   {"curve", std::round(map.curve * 100.0)}};
            mappings.push_back(std::move(j));
        }
        Json macro{{"macro", i + 1},
                   {"name", m.name},
                   {"value", percent(m.value)},
                   {"mappings", mappings},
                   {"locked", m.locked}};
        if (m.automated)
            macro["followsLane"] = true;
        if (model::morphSets(rack, static_cast<int>(i)))
            macro["setByMorph"] = true;
        macros.push_back(std::move(macro));
    }
    // Its snapshots and Morph (racks.md §18), counted from 1 as chains and macros are.
    Json snapshots = Json::array();
    for (size_t i = 0; i < rack.snapshots.size(); ++i)
    {
        Json positions = Json::array();
        for (const float v : rack.snapshots[i].macros)
            positions.push_back(percent(v));
        snapshots.push_back({{"snapshot", i + 1}, {"name", rack.snapshots[i].name}, {"macros", positions}});
    }
    const auto oneBased = [](int i) { return i >= 0 ? Json(i + 1) : Json(nullptr); };
    return {{"rack", formatId(IdKind::Module, slot.id)},
            {"name", slot.name},
            {"track", formatId(IdKind::Track, owner.id)},
            {"on", slot.enabled},
            {"instrument", slot.instrument},
            {"shownChain", rack.shownChain + 1},
            {"chains", chains},
            {"macros", macros},
            {"snapshots", snapshots},
            {"currentSnapshot", oneBased(rack.currentSnapshot)},
            {"snapshotChanged", model::snapshotChanged(rack)},
            {"morph",
             {{"on", rack.morph.on},
              {"a", oneBased(rack.morph.a)},
              {"b", oneBased(rack.morph.b)},
              {"position", percent(rack.morph.position)}}}};
}
} // namespace tools

void addRackTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations adds;
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations removes;
    removes.destructive = true;
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;
    const auto rackResult = result(
        {{"rack", string("The rack's id")},
         {"name", string("")},
         {"chains", arrayOf(result({}), "Its chains, each with its mixer row and modules", std::nullopt)},
         {"macros", arrayOf(result({}), "Its eight macros: name, value (%), mappings", std::nullopt)}},
        {"rack", "chains", "macros"});
    const auto now = [&env](std::uint64_t id)
    {
        env.session.flushSnapshot();
        const auto [slot, owner] = module(env, formatId(IdKind::Module, id));
        return ToolResult::ok(rackJson(env, slot, owner));
    };

    registry.tools.push_back(make(
        env,
        {"group_modules", "Group modules into a rack",
         "Wraps modules of one chain into a new rack (racks.md): one chain holding them in their order, "
         "where the "
         "first of them was, with eight macros. It sounds as they did; their automation and settings stay "
         "with "
         "them. Returns the rack.",
         object({{"modules", ids(IdKind::Module, "Modules of one track's chain (not racks)")}}, {"modules"}),
         rackResult, adds},
        [&env, now](const Json& args, CallContext&)
        {
            std::vector<std::uint64_t> modules;
            for (const auto& m : args["modules"])
                modules.push_back(module(env, m).first.id);
            const auto rack = env.session.groupIntoRack(modules);
            if (rack == 0)
                throw Problem{
                    "They can't be grouped: they must be modules of one track's own chain, none a rack."};
            return now(rack);
        }));

    registry.tools.push_back(
        make(env,
             {"ungroup_rack", "Ungroup a rack",
              "Puts the modules of a rack's shown chain back in its track's chain where the rack was; its "
              "other chains "
              "and its macros' mappings go with it.",
              object({{"rack", id(IdKind::Module, "The rack")}}, {"rack"}),
              result({{"modules", arrayOf(string(""), "The modules put back, in order", std::nullopt)}},
                     {"modules"}),
              removes},
             [&env](const Json& args, CallContext&)
             {
                 const auto [slot, owner] = rackArg(env, args["rack"]);
                 Json placed = Json::array();
                 for (auto id : env.session.ungroupRack(slot.id))
                     placed.push_back(formatId(IdKind::Module, id));
                 return ToolResult::ok({{"modules", placed}});
             }));

    registry.tools.push_back(
        make(env,
             {"get_rack", "Read a rack",
              "A rack's chains (each with its name, on, solo, volume, pan and modules) and its eight macros "
              "(name, "
              "value in %, and what each is mapped to: module, parameter, From and To in the parameter's "
              "unit, Curve "
              "from -100 to 100 %).",
              object({{"rack", id(IdKind::Module, "The rack")}}, {"rack"}), rackResult, reading, false},
             [&env](const Json& args, CallContext&)
             {
                 const auto [slot, owner] = rackArg(env, args["rack"]);
                 return ToolResult::ok(rackJson(env, slot, owner));
             }));

    registry.tools.push_back(
        make(env,
             {"add_rack_chain", "Add a chain to a rack",
              "Adds a chain beside a rack's others (at most 4): empty, it passes the rack's input through, "
              "so a dry "
              "chain sits beside an effected one. add_module with rack and chain puts modules in it.",
              object({{"rack", id(IdKind::Module, "The rack")},
                      {"name", string("Its name (default: \"Chain N\")")}},
                     {"rack"}),
              rackResult, adds},
             [&env, now](const Json& args, CallContext&)
             {
                 const auto [slot, owner] = rackArg(env, args["rack"]);
                 const int chain = env.session.addRackChain(slot.id);
                 if (chain < 0)
                     throw Problem{"A rack has at most " + std::to_string(engine::Session::maxRackChains) +
                                   " chains."};
                 if (args.contains("name"))
                     env.session.setRackChain(slot.id, chain, {.name = args["name"].get<std::string>()});
                 return now(slot.id);
             }));

    registry.tools.push_back(
        make(env,
             {"remove_rack_chain", "Remove a chain from a rack",
              "Removes a rack's chain and its modules; a rack keeps one.",
              object({{"rack", id(IdKind::Module, "The rack")}, {"chain", integer("The chain, from 1", 1)}},
                     {"rack", "chain"}),
              rackResult, removes},
             [&env, now](const Json& args, CallContext&)
             {
                 const auto [slot, owner] = rackArg(env, args["rack"]);
                 if (!env.session.removeRackChain(slot.id, chainArg(*slot.rack, args["chain"])))
                     throw Problem{"A rack keeps one chain."};
                 return now(slot.id);
             }));

    registry.tools.push_back(make(
        env,
        {"set_rack_chain", "Set a rack's chain",
         "Sets a chain's mixer row, each part given: its name, on, solo (heard alone; never kept in a rack's "
         "file), volume in dB (-inf to +6) and pan (-1 left to 1 right, or \"25L\"). Shown: the chain the "
         "rack's "
         "frame shows.",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"chain", integer("The chain, from 1", 1)},
                 {"name", string("Its name")},
                 {"on", boolean("Heard")},
                 {"solo", boolean("Heard alone")},
                 {"volume_db", number("Its volume, dB")},
                 {"pan",
                  {{"type", {"number", "string"}}, {"description", "-1 .. 1, or \"C\", \"25L\", \"50R\""}}},
                 {"shown", boolean("The rack's frame shows it")}},
                {"rack", "chain"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [slot, owner] = rackArg(env, args["rack"]);
            const int chain = chainArg(*slot.rack, args["chain"]);
            engine::Session::RackChainEdit e;
            if (args.contains("name"))
                e.name = args["name"].get<std::string>();
            if (args.contains("on"))
                e.on = args["on"].get<bool>();
            if (args.contains("solo"))
                e.solo = args["solo"].get<bool>();
            if (args.contains("volume_db"))
                e.volumeDb = args["volume_db"].get<float>();
            if (args.contains("pan"))
            {
                const auto& pan = args["pan"];
                if (pan.is_number())
                    e.pan = pan.get<float>();
                else
                {
                    const auto text = juce::String(pan.get<std::string>()).trim().toUpperCase();
                    const float amount =
                        juce::jlimit(0.0f, 50.0f, text.dropLastCharacters(1).getFloatValue()) / 50.0f;
                    e.pan = text == "C" ? 0.0f : text.endsWithChar('L') ? -amount : amount;
                }
            }
            env.session.setRackChain(slot.id, chain, e);
            if (args.value("shown", false))
                env.session.showRackChain(slot.id, chain);
            return now(slot.id);
        }));

    registry.tools.push_back(make(
        env,
        {"map_macro", "Map a macro to a parameter",
         "Maps one of a rack's macros (1 to 8) to a parameter of a module in the rack: where the macro's 0 % "
         "puts it (from) and its 100 % (to), in the parameter's unit as set_parameters takes it (third-party "
         "plugins: 0 to 1), and the curve (-100 to 100 %: up, it comes in late; down, early; 0 straight). "
         "From "
         "defaults to where it is, To to its top. A parameter another macro set is taken from it. The "
         "parameter "
         "then follows its macro; set the macro (set_macro) or automate it.",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"macro", integer("The macro, 1 to 8", 1, 8)},
                 {"module", id(IdKind::Module, "A module in the rack")},
                 {"parameter", string("Its parameter's id (get_module)")},
                 {"from", {{"type", {"number", "string"}}, {"description", "Where the macro's 0 % puts it"}}},
                 {"to", {{"type", {"number", "string"}}, {"description", "Where its 100 % puts it"}}},
                 {"curve", number("-100 .. 100 %")}},
                {"rack", "macro", "module", "parameter"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            const int macro = macroArg(args["macro"]);
            const auto [slot, holder] = module(env, args["module"]);
            const auto param = args["parameter"].get<std::string>();
            if (model::findDevice({*model::findDevice(owner.devices, rack.id)}, slot.id) == nullptr ||
                slot.id == rack.id)
                throw Problem{formatId(IdKind::Module, slot.id) + " isn't in " +
                              formatId(IdKind::Module, rack.id) + "."};
            const engine::Session::ParamTarget target{owner.id, "m" + std::to_string(slot.id) + "/" + param};
            float from = 0.0f;
            if (args.contains("from"))
                from = travelOf(env, owner.id, slot.id, param, args["from"]);
            else if (const auto value = env.session.paramValue(target))
                from = *value;
            const float to = args.contains("to") ? travelOf(env, owner.id, slot.id, param, args["to"])
                                                 : (from < 0.999f ? 1.0f : 0.0f);
            const float curve = std::clamp(args.value("curve", 0.0f) / 100.0f, -1.0f, 1.0f);
            if (!env.session.mapMacro(rack.id, macro, slot.id, param, from, to, curve))
                throw Problem{"\"" + param +
                              "\" can't be mapped: get_module lists the module's parameters, and only "
                              "those an automation lane can drive can be."};
            return now(rack.id);
        }));

    registry.tools.push_back(
        make(env,
             {"unmap_macro", "Unmap a parameter from its macro",
              "The parameter is set by hand again, where its macro has it now.",
              object({{"module", id(IdKind::Module, "A module in a rack")},
                      {"parameter", string("Its parameter's id")}},
                     {"module", "parameter"}),
              result({{"unmapped", boolean("")}}, {"unmapped"}), changes},
             [&env](const Json& args, CallContext&)
             {
                 const auto [slot, holder] = module(env, args["module"]);
                 if (!env.session.unmapMacro(slot.id, args["parameter"].get<std::string>()))
                     throw Problem{"No macro sets it."};
                 return ToolResult::ok({{"unmapped", true}});
             }));

    registry.tools.push_back(make(
        env,
        {"save_rack", "Save a rack as a file",
         "Writes a rack as a .segnorack file (racks.md §7): its chains, their mixer rows and modules' "
         "settings, its "
         "macros and mappings; not automation, routing or solo. By default into your racks (the User "
         "library's "
         "Racks folder) named as the rack, where the browser finds it. add_module with type \"rack\" and its "
         "path loads it on a track.",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"path", string("Where to write it (default: your racks, named as the rack)")}},
                {"rack"}),
         result({{"path", string("The file written")}}, {"path"}), adds, false},
        [&env](const Json& args, CallContext&)
        {
            const auto [slot, owner] = rackArg(env, args["rack"]);
            const auto file =
                args.contains("path")
                    ? juce::File(juce::String::fromUTF8(args["path"].get<std::string>().c_str()))
                    : env.session.racksFolder().getChildFile(
                          juce::File::createLegalFileName(juce::String::fromUTF8(slot.name.c_str())) +
                          engine::Session::rackFileExtension);
            if (!juce::File::isAbsolutePath(file.getFullPathName()))
                throw Problem{"The path must be absolute."};
            std::string error;
            if (!env.session.saveRack(slot.id, file, &error))
                throw Problem{error};
            return ToolResult::ok({{"path", file.getFullPathName().toStdString()}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_macro", "Set a macro",
         "Sets one of a rack's macros: its value (0 to 100 %; every parameter mapped to it follows), its "
         "name, "
         "and its lock (locked, snapshots' recall, Morph and Vary leave it alone; racks.md §18.4).",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"macro", integer("The macro, 1 to 8", 1, 8)},
                 {"value", number("0 .. 100 %")},
                 {"name", string("Its name")},
                 {"locked", boolean("Locked")}},
                {"rack", "macro"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            const int macro = macroArg(args["macro"]);
            if (args.contains("locked"))
                env.session.lockMacro(rack.id, macro, args["locked"].get<bool>());
            std::optional<float> value;
            if (args.contains("value"))
            {
                env.session.flushSnapshot();
                const auto [fresh, holder] = rackArg(env, args["rack"]);
                if (model::morphSets(*fresh.rack, macro))
                    throw Problem{"Morph sets macro " + std::to_string(macro + 1) +
                                  ": set_morph moves it; set_morph with on false, or locked true, frees it."};
                value = std::clamp(args["value"].get<float>() / 100.0f, 0.0f, 1.0f);
            }
            std::optional<std::string> name;
            if (args.contains("name"))
                name = args["name"].get<std::string>();
            if (value || name)
                env.session.setMacro(rack.id, macro, value, name);
            return now(rack.id);
        }));

    // ---- snapshots, Morph and Vary (racks.md §18) ------------------------------------------------

    registry.tools.push_back(make(
        env,
        {"store_snapshot", "Store a rack's snapshot",
         "Stores where a rack's eight macros are as a snapshot (racks.md §18.2), \"Snapshot N\", which "
         "becomes "
         "the current one; with snapshot, over that one instead (Update). A snapshot holds the macros' "
         "positions "
         "only: a rack that differs in anything else is another rack (save_rack). At most 8; the first two "
         "become Morph's A and B.",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"snapshot", integer("Store over this one, counted from 1 (default: a new one)", 1, 8)}},
                {"rack"}),
         rackResult, adds},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            std::optional<int> over;
            if (args.contains("snapshot"))
                over = snapshotArg(*rack.rack, args["snapshot"]);
            if (env.session.storeSnapshot(rack.id, over) < 0)
                throw Problem{"The rack has 8 snapshots already: store over one, or remove one."};
            return now(rack.id);
        }));

    registry.tools.push_back(make(
        env,
        {"recall_snapshot", "Recall a rack's snapshot",
         "Puts a rack's macros where a snapshot has them, all but locked ones and those following an "
         "automation lane; Morph turns off first. It becomes the current one.",
         object({{"rack", id(IdKind::Module, "The rack")}, {"snapshot", integer("Counted from 1", 1, 8)}},
                {"rack", "snapshot"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            env.session.recallSnapshot(rack.id, snapshotArg(*rack.rack, args["snapshot"]));
            return now(rack.id);
        }));

    registry.tools.push_back(make(
        env,
        {"remove_snapshot", "Remove a rack's snapshot",
         "Removes a snapshot; those after it move up one. Removing Morph's A or B turns Morph off.",
         object({{"rack", id(IdKind::Module, "The rack")}, {"snapshot", integer("Counted from 1", 1, 8)}},
                {"rack", "snapshot"}),
         rackResult, removes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            env.session.removeSnapshot(rack.id, snapshotArg(*rack.rack, args["snapshot"]));
            return now(rack.id);
        }));

    registry.tools.push_back(make(
        env,
        {"set_snapshot", "Rename or set a rack's snapshot",
         "A snapshot's name, or where it has the macros (macros: up to eight values, 0 to 100 %, macro 1 "
         "first), set without recalling it.",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"snapshot", integer("Counted from 1", 1, 8)},
                 {"name", string("Its name")},
                 {"macros", arrayOf(number("0 .. 100 %"), "Its positions, macro 1 first", std::nullopt)}},
                {"rack", "snapshot"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            const int snapshot = snapshotArg(*rack.rack, args["snapshot"]);
            std::optional<std::string> name;
            if (args.contains("name"))
                name = args["name"].get<std::string>();
            std::optional<std::array<float, 8>> macros;
            if (args.contains("macros"))
            {
                const auto& given = args["macros"];
                if (!given.is_array() || given.size() > 8)
                    throw Problem{"macros is up to eight values, 0 to 100 %, macro 1 first."};
                auto positions = rack.rack->snapshots[static_cast<size_t>(snapshot)].macros;
                for (size_t i = 0; i < given.size(); ++i)
                    positions[i] = std::clamp(given[i].get<float>() / 100.0f, 0.0f, 1.0f);
                macros = positions;
            }
            env.session.setSnapshot(rack.id, snapshot, name, macros);
            return now(rack.id);
        }));

    registry.tools.push_back(make(
        env,
        {"set_morph", "Set a rack's Morph",
         "Morph (racks.md §18.3): while on, the rack's macros (all but locked ones and those following a "
         "lane) go from where snapshot A has them to where B has them as its position goes from 0 to 100 %. "
         "On needs A and B. Its position is also the rack's parameter \"morph\", automated as any.",
         object({{"rack", id(IdKind::Module, "The rack")},
                 {"on", boolean("On")},
                 {"a", integer("Snapshot A, counted from 1", 1, 8)},
                 {"b", integer("Snapshot B, counted from 1", 1, 8)},
                 {"position", number("0 .. 100 %")}},
                {"rack"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["rack"]);
            engine::Session::MorphEdit e;
            if (args.contains("a"))
                e.a = snapshotArg(*rack.rack, args["a"]);
            if (args.contains("b"))
                e.b = snapshotArg(*rack.rack, args["b"]);
            if (args.contains("on"))
                e.on = args["on"].get<bool>();
            if (args.contains("position"))
                e.position = std::clamp(args["position"].get<float>() / 100.0f, 0.0f, 1.0f);
            if (!env.session.setMorph(rack.id, e))
                throw Problem{"Morph needs two snapshots to go between: store_snapshot stores them, a and b "
                              "choose them."};
            return now(rack.id);
        }));

    registry.tools.push_back(make(
        env,
        {"vary_module", "Vary a module",
         "Vary (racks.md §18.5): on a rack, each macro that maps something and isn't locked or following a "
         "lane moves from where it is by a random amount, up to 15 % of its travel (more: 40 %). One undo "
         "step; again for another. Not while Morph is on.",
         object({{"module", id(IdKind::Module, "The module (a rack)")}, {"more", boolean("Vary more")}},
                {"module"}),
         rackResult, changes},
        [&env, now](const Json& args, CallContext&)
        {
            const auto [rack, owner] = rackArg(env, args["module"]);
            if (!env.session.varyRack(rack.id, args.value("more", false) ? engine::Session::varyMoreAmount
                                                                         : engine::Session::varyAmount))
                throw Problem{model::morphing(*rack.rack)
                                  ? std::string("Morph sets the macros: set_morph with on false first.")
                                  : std::string("Nothing to vary: no macro that maps something is free.")};
            return now(rack.id);
        }));
}
} // namespace fd::mcp
