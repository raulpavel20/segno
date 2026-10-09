// Automation (M2 step C4, ADR 0028, docs/design/automation.md §11): reading and writing lanes.

#include "mcp/tools/Tools.h"

#include <algorithm>
#include <cctype>

namespace fd::mcp
{
using namespace tools;

namespace
{
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// What a lane's arguments name, resolved.
struct Target
{
    engine::Session::LaneTarget lane;
    model::TrackInfo channel;
    std::optional<model::DeviceSlot> module;
    std::string parameter; // the parameter's id ("cutoff", "volume", "send")
    std::string name;      // as people read it
};

Json targetSchema()
{
    return {{"module", id(IdKind::Module, "A module's parameter: the module (with parameter)")},
            {"track", id(IdKind::Track, "A channel's mix: the track, return track or master (with parameter)")},
            {"parameter", string("The module's parameter (id or name), or for a channel \"volume\", \"pan\" or \"send\"")},
            {"return", id(IdKind::Track, "For a send: the return track it sends to")}};
}

Target target(ToolEnv& env, const Json& args)
{
    if (!args.contains("parameter") || !args["parameter"].is_string())
        throw Problem{"Name the parameter: a module's parameter id or name with module, or \"volume\", \"pan\" or "
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
                if (c.lane.rfind(prefix, 0) == 0 &&
                    (pass == 0 ? c.lane.substr(prefix.size()) == parameter : lower(c.name) == lower(parameter)))
                    return {{owner.id, c.lane}, owner, slot, c.lane.substr(prefix.size()), c.name};
        throw Problem{slot.name + " has no automatable parameter \"" + parameter +
                      "\" (get_module lists them; settings such as lookahead can't be automated)."};
    }
    if (!args.contains("track"))
        throw Problem{"Give module (for a module's parameter) or track (for volume, pan or a send)."};
    const auto t = channel(env, args["track"]);
    const auto p = lower(parameter);
    if (p == "volume" || p == "pan")
        return {{t.id, p}, t, std::nullopt, p, p == "volume" ? "Volume" : "Pan"};
    if (p == "send")
    {
        if (t.role != model::TrackInfo::Role::Track)
            throw Problem{"Only tracks send; " + t.name + " is a " + kindName(t) + "."};
        if (!args.contains("return"))
            throw Problem{"Name the return track the send goes to (return)."};
        const auto r = channel(env, args["return"]);
        if (r.role != model::TrackInfo::Role::Return)
            throw Problem{r.name + " isn't a return track."};
        return {{t.id, "send/" + std::to_string(r.id)}, t, std::nullopt, "send", "Send " + r.name};
    }
    throw Problem{"A channel's lanes are \"volume\", \"pan\" and \"send\"; for a module's parameter give module."};
}

Json targetJson(const Target& t)
{
    Json j{{"track", formatId(IdKind::Track, t.channel.id)}, {"parameter", t.parameter}, {"name", t.name}};
    if (t.module)
        j["module"] = formatId(IdKind::Module, t.module->id);
    if (t.parameter == "send")
        j["return"] = formatId(IdKind::Track, std::stoull(t.lane.lane.substr(5)));
    return j;
}

/// A value from a plain number, true/false, or text the parameter understands.
float valueArg(ToolEnv& env, const Target& t, const Json& value)
{
    auto& s = env.session;
    std::optional<float> v;
    if (value.is_boolean())
        v = s.laneFromPlain(t.lane, value.get<bool>() ? 1.0 : 0.0);
    else if (value.is_number())
        v = s.laneFromPlain(t.lane, value.get<double>());
    else if (value.is_string())
        v = s.laneFromText(t.lane, value.get<std::string>());
    if (!v)
        throw Problem{value.dump() + " isn't a value of " + t.name + ": give it in the parameter's unit (" +
                      s.laneText(t.lane, 0.5f) + ") or as text."};
    return *v;
}

std::optional<std::pair<double, double>> rangeArg(ToolEnv& env, const Json& args)
{
    if (!args.contains("from") && !args.contains("to"))
        return std::nullopt;
    const double from = args.contains("from") ? seconds(env, positionArg(env, args["from"])) : 0.0;
    const double to = args.contains("to") ? seconds(env, positionArg(env, args["to"])) : 1e9;
    if (to < from)
        throw Problem{"to comes before from."};
    return std::pair{from, to};
}

Json laneJson(ToolEnv& env, const Target& t, const std::optional<std::pair<double, double>>& range = {})
{
    auto& s = env.session;
    s.flushSnapshot();
    const auto* channel = s.snapshot()->channel(t.channel.id);
    const auto* lane = channel != nullptr ? channel->lane(t.lane.lane) : nullptr;
    const auto tm = tempo(env);
    Json points = Json::array();
    if (lane != nullptr)
        for (const auto& p : lane->points)
        {
            if (range && (p.time < range->first || p.time > range->second))
                continue;
            const auto plain = s.lanePlain(t.lane, p.value);
            points.push_back({{"position", positionJson(tm.beatsForSeconds(p.time), tm)},
                              {"value", plain ? Json(std::round(*plain * 1e4) / 1e4) : Json(nullptr)},
                              {"text", s.laneText(t.lane, p.value)},
                              {"bend", lane->steps >= 2 ? 0.0 : std::round(model::bendOf(p) * 1e3) / 1e3}});
        }
    Json j = targetJson(t);
    j["on"] = lane == nullptr || lane->on;
    j["stepped"] = lane != nullptr && lane->steps >= 2;
    j["range"] = {{"min", s.laneText(t.lane, 0.0f)}, {"max", s.laneText(t.lane, 1.0f)}};
    j["points"] = std::move(points);
    return j;
}

Json laneResultSchema()
{
    return result({{"track", string("")}, {"parameter", string("")}, {"on", boolean("The lane plays")},
                   {"points", arrayOf(result({{"position", positionResult()}, {"text", string("")}}), "", 0)}},
                  {"track", "parameter", "points"});
}
} // namespace

void addAutomationTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations removes;
    removes.destructive = true;

    registry.tools.push_back(make(
        env,
        {"get_automation", "Read a parameter's automation",
         "Reads the automation lane of a module's parameter or of a channel's volume, pan or send: its points (where, "
         "the value in the parameter's unit and as text, and the bend of the segment after each, -1 to 1), whether "
         "the lane plays, and the lane's range. Give from and to for part of it.",
         object([&] {
             auto p = targetSchema();
             p["from"] = position("Only points from here");
             p["to"] = position("Only points up to here");
             return p;
         }(), {"parameter"}),
         laneResultSchema(), reads, false},
        [&env](const Json& args, CallContext&) {
            const auto t = target(env, args);
            const auto j = laneJson(env, t, rangeArg(env, args));
            return ToolResult::ok(j, t.name + ": " + std::to_string(j["points"].size()) + " points" +
                                         (j["on"].get<bool>() ? "." : " (off: the parameter keeps its own value)."));
        }));

    registry.tools.push_back(make(
        env,
        {"set_automation", "Write a parameter's automation",
         "Writes a lane of points for a module's parameter or a channel's volume, pan or send: each point a position "
         "(beats or \"5.1.1\"), a value in the parameter's unit or as text (\"1.2 kHz\", \"-6 dB\", \"25L\"), and "
         "optionally the bend of the segment after it (-1 to 1; 0 is straight). A straight segment moves the control "
         "evenly. The points replace the lane's (or, with from and to, only those in that range). Stepped "
         "parameters hold each value until the next point. The lane shows in the arrangement. One undo step.",
         object([&] {
             auto p = targetSchema();
             p["points"] = arrayOf(object({{"position", position("Where")},
                                           {"value", {{"type", {"number", "string", "boolean"}},
                                                      {"description", "The parameter's value, or its text"}}},
                                           {"bend", number("The segment after it: -1 .. 1 (0 straight)")}},
                                          {"position", "value"}),
                                   "The points", 0);
             p["from"] = position("Replace only from here");
             p["to"] = position("Replace only up to here");
             return p;
         }(), {"parameter", "points"}),
         laneResultSchema(), changes},
        [&env](const Json& args, CallContext&) {
            const auto t = target(env, args);
            model::AutomationPoints points;
            for (const auto& p : args["points"])
            {
                const double at = seconds(env, positionArg(env, p["position"]));
                points.push_back({at, valueArg(env, t, p["value"]),
                                  model::curveForBend(p.contains("bend") ? p["bend"].get<float>() : 0.0f)});
            }
            std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
            if (!env.session.setAutomation(t.lane, points, rangeArg(env, args)))
                throw Problem{t.name + " can't be automated."};
            const auto j = laneJson(env, t);
            return ToolResult::ok(j, t.name + ": wrote " + std::to_string(points.size()) + " points (" +
                                         std::to_string(j["points"].size()) + " on the lane).");
        }));

    registry.tools.push_back(make(
        env,
        {"clear_automation", "Clear a parameter's automation",
         "Removes the points of a lane (all of them, or those between from and to). Without points the parameter "
         "keeps its own value again. One undo step.",
         object([&] {
             auto p = targetSchema();
             p["from"] = position("Clear from here");
             p["to"] = position("Clear up to here");
             return p;
         }(), {"parameter"}),
         laneResultSchema(), removes},
        [&env](const Json& args, CallContext&) {
            const auto t = target(env, args);
            if (!env.session.setAutomation(t.lane, {}, rangeArg(env, args), "Clear automation"))
                throw Problem{t.name + " can't be automated."};
            return ToolResult::ok(laneJson(env, t), "Cleared " + t.name + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"set_automation_on", "Turn automation back on",
         "Turns a lane back on (or off), or with all every lane back on. A lane turns off when its control is moved "
         "by hand (or by set_parameters, set_mix, set_send) while not recording: the parameter keeps that value until "
         "the lane is turned back on. Not an undo step.",
         object([&] {
             auto p = targetSchema();
             p["on"] = boolean("On (default) or off");
             p["all"] = boolean("Every lane back on");
             return p;
         }()),
         result({{"on", boolean("")}}), changes, false},
        [&env](const Json& args, CallContext&) {
            if (args.value("all", false))
            {
                env.session.setAllLanesOn();
                env.session.flushSnapshot();
                return ToolResult::ok({{"on", true}, {"all", true}}, "Every lane plays again.");
            }
            const auto t = target(env, args);
            const bool on = args.value("on", true);
            env.session.setLaneOn(t.lane, on);
            return ToolResult::ok(laneJson(env, t), t.name + (on ? " plays its lane." : " keeps its own value."));
        }));
}
} // namespace fd::mcp
