// Automation (M2 step C4, ADRs 0028 and 0029, docs/design/automation.md §11): automation lanes of
// their own, the parameters they drive, and their points.

#include "mcp/tools/Tools.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace fd::mcp
{
using namespace tools;

namespace
{
using ParamTarget = engine::Session::ParamTarget;

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

double rounded4(double v)
{
    return std::round(v * 1e4) / 1e4;
}

/// What a parameter's arguments name, resolved.
struct Param
{
    ParamTarget target;
    std::string name; // as people read it
};

Json paramSchema()
{
    return {{"module", id(IdKind::Module, "A module's parameter: the module (with parameter)")},
            {"track", id(IdKind::Track, "A channel's mix: the track, return track or master (with parameter)")},
            {"parameter", string("The module's parameter (id or name), or for a channel \"volume\", \"pan\" or \"send\"")},
            {"return", id(IdKind::Track, "For a send: the return track it sends to")}};
}

Param param(ToolEnv& env, const Json& args)
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
                if (c.key.rfind(prefix, 0) == 0 &&
                    (pass == 0 ? c.key.substr(prefix.size()) == parameter : lower(c.name) == lower(parameter)))
                    return {{owner.id, c.key}, c.name};
        throw Problem{slot.name + " has no automatable parameter \"" + parameter +
                      "\" (get_module lists them; settings such as lookahead can't be automated)."};
    }
    if (!args.contains("track"))
        throw Problem{"Give module (for a module's parameter) or track (for volume, pan or a send)."};
    const auto t = channel(env, args["track"]);
    const auto p = lower(parameter);
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
    throw Problem{"A channel's parameters are \"volume\", \"pan\" and \"send\"; for a module's parameter give module."};
}

/// A parameter as assistants name it: its track, module and id (or volume, pan, send and return).
Json paramJson(ToolEnv& env, const ParamTarget& t)
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

/// A parameter's value from a plain number, true/false, or text it understands: its travel (0..1).
float valueArg(ToolEnv& env, const Param& p, const Json& value)
{
    auto& s = env.session;
    std::optional<float> v;
    if (value.is_boolean())
        v = s.paramFromPlain(p.target, value.get<bool>() ? 1.0 : 0.0);
    else if (value.is_number())
        v = s.paramFromPlain(p.target, value.get<double>());
    else if (value.is_string())
        v = s.paramFromText(p.target, value.get<std::string>());
    if (!v)
        throw Problem{value.dump() + " isn't a value of " + p.name + ": give it in the parameter's unit (" +
                      s.paramText(p.target, 0.5f) + ") or as text."};
    return *v;
}

/// An assignment's range from `from` and `to` (the parameter's units or text; default: all of it).
model::AssignmentRange rangeOf(ToolEnv& env, const Param& p, const Json& args)
{
    return {args.contains("from") ? valueArg(env, p, args["from"]) : 0.0f,
            args.contains("to") ? valueArg(env, p, args["to"]) : 1.0f};
}

/// A time range from `from` and `to` (positions).
std::optional<std::pair<double, double>> timeRange(ToolEnv& env, const Json& args)
{
    if (!args.contains("from") && !args.contains("to"))
        return std::nullopt;
    const double from = args.contains("from") ? seconds(env, positionArg(env, args["from"])) : 0.0;
    const double to = args.contains("to") ? seconds(env, positionArg(env, args["to"])) : 1e9;
    if (to < from)
        throw Problem{"to comes before from."};
    return std::pair{from, to};
}

const model::AutomationLaneInfo& laneInfo(ToolEnv& env, std::uint64_t id)
{
    env.session.flushSnapshot();
    const auto* lane = env.session.snapshot()->automationLane(id);
    if (lane == nullptr)
        throw Problem{formatId(IdKind::Lane, id) + " isn't an automation lane (get_project lists them)."};
    return *lane;
}

std::uint64_t laneArg(ToolEnv& env, const Json& value)
{
    const auto id = parseId(value, IdKind::Lane);
    if (!id)
        throw Problem{value.dump() + " isn't a lane id (\"a:12\"; get_project lists the automation lanes)."};
    (void)laneInfo(env, *id);
    return *id;
}

Json assignmentJson(ToolEnv& env, const model::AssignmentInfo& a)
{
    auto& s = env.session;
    const ParamTarget t{a.channel, a.key};
    auto j = paramJson(env, t);
    j["module"] = a.device != 0 ? Json(formatId(IdKind::Module, a.device)) : Json(nullptr);
    const auto from = s.paramPlain(t, a.range.from), to = s.paramPlain(t, a.range.to);
    j["from"] = {{"value", from ? Json(rounded4(*from)) : Json(nullptr)}, {"text", s.paramText(t, a.range.from)}};
    j["to"] = {{"value", to ? Json(rounded4(*to)) : Json(nullptr)}, {"text", s.paramText(t, a.range.to)}};
    j["on"] = a.on;
    return j;
}

/// A lane with its assignments, and its points (in `time` only) read through `unit`: a given
/// parameter on it, or its one assignment.
Json laneJson(ToolEnv& env, std::uint64_t id, const std::optional<ParamTarget>& unit = std::nullopt,
              const std::optional<std::pair<double, double>>& time = std::nullopt, bool withPoints = true)
{
    auto& s = env.session;
    const auto lane = laneInfo(env, id); // a copy: tools may publish snapshots
    const model::AssignmentInfo* by = nullptr;
    for (const auto& a : lane.assignments)
        if (unit ? (a.channel == unit->channel && a.key == unit->key) : lane.assignments.size() == 1)
            by = &a;
    const auto tm = tempo(env);
    Json assignments = Json::array();
    for (const auto& a : lane.assignments)
        assignments.push_back(assignmentJson(env, a));
    Json j{{"lane", formatId(IdKind::Lane, lane.id)},
           {"name", lane.displayName()},
           {"after", lane.after != 0 ? Json(formatId(IdKind::Track, lane.after)) : Json(nullptr)},
           {"on", lane.allOn()},
           {"stepped", lane.rules().stepped()},
           {"assignments", std::move(assignments)}};
    if (!withPoints)
    {
        j["points"] = lane.points.size();
        return j;
    }
    Json points = Json::array();
    for (const auto& p : lane.points)
    {
        if (time && (p.time < time->first || p.time > time->second))
            continue;
        Json point{{"position", positionJson(tm.beatsForSeconds(p.time), tm)},
                   {"lane", rounded4(p.value)},
                   {"bend", lane.rules().stepped() ? 0.0 : std::round(model::bendOf(p) * 1e3) / 1e3}};
        if (by != nullptr)
        {
            const ParamTarget t{by->channel, by->key};
            const auto plain = s.paramPlain(t, by->range.toTravel(p.value));
            point["value"] = plain ? Json(rounded4(*plain)) : Json(nullptr);
            point["text"] = s.paramText(t, by->range.toTravel(p.value));
        }
        else
            point["text"] = s.laneText(lane.id, p.value);
        points.push_back(std::move(point));
    }
    j["points"] = std::move(points);
    if (by != nullptr)
        j["parameter"] = assignmentJson(env, *by);
    return j;
}

Json laneResultSchema()
{
    return result({{"lane", {{"type", {"string", "null"}}, {"description", "The lane's id (\"a:12\"); null for none"}}},
                   {"name", string("")},
                   {"on", boolean("Every assignment plays")},
                   {"assignments", arrayOf(result({{"track", string("")}, {"parameter", string("")}}), "", 0)},
                   {"points", arrayOf(result({{"position", positionResult()}, {"text", string("")}}), "", 0)}},
                  {"lane", "assignments", "points"});
}

/// A lane without its points (their number).
Json laneSummarySchema()
{
    return result({{"lane", string("The lane's id (\"a:12\")")},
                   {"name", string("")},
                   {"on", boolean("Every assignment plays")},
                   {"assignments", arrayOf(result({{"track", string("")}, {"parameter", string("")}}), "", 0)},
                   {"points", integer("How many points it has")}},
                  {"lane", "name", "assignments", "points"});
}

/// The lane `args` names: by `lane`, or a parameter's (nullopt: the parameter has none).
std::pair<std::optional<std::uint64_t>, std::optional<Param>> laneOrParam(ToolEnv& env, const Json& args)
{
    if (args.contains("lane"))
        return {laneArg(env, args["lane"]), std::nullopt};
    auto p = param(env, args);
    return {env.session.laneOf(p.target), p};
}

/// A point's value as a lane value: through the parameter (its units, within its range), or for a
/// lane by itself through its one assignment, else 0..1 (or a percentage).
float laneValueArg(ToolEnv& env, const model::AutomationLaneInfo& lane, const std::optional<Param>& by, const Json& value)
{
    std::optional<Param> unit = by;
    model::AssignmentRange range;
    if (unit)
    {
        if (const auto* a = lane.assignment(unit->target.channel, unit->target.key))
            range = a->range;
    }
    else if (lane.assignments.size() == 1)
    {
        const auto& a = lane.assignments.front();
        unit = Param{{a.channel, a.key}, a.name};
        range = a.range;
    }
    if (!unit)
    {
        if (value.is_number() && value.get<double>() >= 0.0 && value.get<double>() <= 1.0)
            return value.get<float>();
        if (value.is_string())
            if (const auto v = env.session.laneFromText(lane.id, value.get<std::string>()))
                return *v;
        throw Problem{value.dump() + " isn't a value of this lane: it drives " +
                      std::to_string(lane.assignments.size()) + " parameters, so give 0 to 1 (or \"50 %\"), or name "
                                                                "one of them (with its unit)."};
    }
    const float travel = valueArg(env, *unit, value);
    if (std::abs(range.to - range.from) < 1e-6f)
        throw Problem{unit->name + "'s range on this lane is empty: set it with assign_automation."};
    const float v = (travel - range.from) / (range.to - range.from);
    if (v < -1e-4f || v > 1.0f + 1e-4f)
        throw Problem{value.dump() + " is outside " + unit->name + "'s range on this lane (" +
                      env.session.paramText(unit->target, range.from) + " to " +
                      env.session.paramText(unit->target, range.to) + ")."};
    return std::clamp(v, 0.0f, 1.0f);
}
} // namespace

void addAutomationTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations adds;
    ToolAnnotations removes;
    removes.destructive = true;

    registry.tools.push_back(make(
        env,
        {"add_automation_lane", "Add an automation lane",
         "Adds an automation lane: a row of its own among the tracks holding a curve that drives the parameters "
         "assigned to it, each through its own range (from and to, in the parameter's units; to below from inverts "
         "it). Like a new track it goes at the end of the tracks, or after a track or a lane (after). A parameter "
         "on another lane moves here, bringing its points. One undo step.",
         object({{"after", string("A track (\"t:3\") or a lane (\"a:12\") it goes after")},
                 {"name", string("Its name (by default named after its parameters)")},
                 {"parameters", arrayOf(object([&] {
                                            auto p = paramSchema();
                                            p["from"] = {{"type", {"number", "string"}},
                                                         {"description", "The parameter's value at the lane's bottom"}};
                                            p["to"] = {{"type", {"number", "string"}},
                                                       {"description", "The parameter's value at the lane's top"}};
                                            return p;
                                        }(), {"parameter"}),
                                        "What it drives", 0)}}),
         laneResultSchema(), adds},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            std::vector<std::pair<Param, model::AssignmentRange>> params;
            if (args.contains("parameters"))
                for (const auto& p : args["parameters"])
                {
                    auto resolved = param(env, p);
                    params.emplace_back(resolved, rangeOf(env, resolved, p));
                }
            const auto first = params.empty() ? std::nullopt : std::optional(params.front().first.target);
            std::uint64_t id = 0;
            if (args.contains("after"))
            {
                const auto& a = args["after"];
                id = s.addAutomationLaneAfter(
                    a.is_string() && a.get<std::string>().rfind("a:", 0) == 0 ? laneArg(env, a) : channel(env, a).id, first);
            }
            else
                id = s.addAutomationLane(first);
            if (id == 0)
                throw Problem{params.front().first.name + " can't be automated."};
            for (size_t i = 0; i < params.size(); ++i)
                if (!s.assign(id, params[i].first.target, params[i].second))
                    throw Problem{params[i].first.name + " can't be automated."};
            if (args.contains("name"))
                s.renameAutomationLane(id, args["name"].get<std::string>());
            const auto j = laneJson(env, id);
            return ToolResult::ok(j, "Added " + j["name"].get<std::string>() + " (" + formatId(IdKind::Lane, id) + ").");
        }));

    registry.tools.push_back(make(
        env,
        {"set_automation_lane", "Rename an automation lane",
         "Names an automation lane (an empty name: named after its parameters again). One undo step.",
         object({{"lane", string("The lane (\"a:12\")")}, {"name", string("Its name")}}, {"lane", "name"}),
         laneSummarySchema(), changes},
        [&env](const Json& args, CallContext&) {
            const auto id = laneArg(env, args["lane"]);
            env.session.renameAutomationLane(id, args["name"].get<std::string>());
            const auto j = laneJson(env, id, std::nullopt, std::nullopt, false);
            return ToolResult::ok(j, "Named it " + j["name"].get<std::string>() + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"remove_automation_lane", "Remove an automation lane",
         "Removes an automation lane and its points; the parameters it drove keep their values now. One undo step.",
         object({{"lane", string("The lane (\"a:12\")")}}, {"lane"}), result({{"removed", string("")}}), removes},
        [&env](const Json& args, CallContext&) {
            const auto id = laneArg(env, args["lane"]);
            const auto name = laneInfo(env, id).displayName();
            env.session.removeAutomationLane(id);
            env.session.flushSnapshot();
            return ToolResult::ok({{"removed", formatId(IdKind::Lane, id)}}, "Removed " + name + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"assign_automation", "Assign a parameter to a lane",
         "Makes an automation lane drive a parameter (a module's, or a channel's volume, pan or send) through a "
         "range: from (at the lane's bottom) and to (at its top), in the parameter's units or as text; the whole "
         "range by default; to below from inverts it. A parameter is on one lane at most: one on another lane "
         "moves (movedFrom). Assigning it again sets its range. One undo step.",
         object([&] {
             auto p = paramSchema();
             p["lane"] = string("The lane (\"a:12\")");
             p["from"] = {{"type", {"number", "string"}}, {"description", "The parameter's value at the lane's bottom"}};
             p["to"] = {{"type", {"number", "string"}}, {"description", "The parameter's value at the lane's top"}};
             return p;
         }(), {"lane", "parameter"}),
         laneSummarySchema(), changes},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            const auto id = laneArg(env, args["lane"]);
            const auto p = param(env, args);
            const auto before = s.laneOf(p.target);
            if (!s.assign(id, p.target, rangeOf(env, p, args)))
                throw Problem{p.name + " can't be automated."};
            auto j = laneJson(env, id, p.target, std::nullopt, false);
            if (before && *before != id)
                j["movedFrom"] = formatId(IdKind::Lane, *before);
            return ToolResult::ok(j, p.name + " follows " + j["name"].get<std::string>() + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"unassign_automation", "Take a parameter off its lane",
         "Takes a parameter off its automation lane: it keeps its value now. One undo step.",
         object(paramSchema(), {"parameter"}), result({{"lane", string("The lane it was on")}}), removes},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            const auto p = param(env, args);
            const auto lane = s.laneOf(p.target);
            if (!lane)
                throw Problem{p.name + " isn't on an automation lane."};
            s.unassign(p.target);
            s.flushSnapshot();
            return ToolResult::ok({{"lane", formatId(IdKind::Lane, *lane)}, {"parameter", paramJson(env, p.target)}},
                                  p.name + " keeps its own value now.");
        }));

    registry.tools.push_back(make(
        env,
        {"get_automation", "Read automation",
         "Reads an automation lane (lane), or a parameter's (a module's parameter, or a channel's volume, pan or "
         "send): its points (where; the lane value 0 to 1; the value in the parameter's unit and as text, for the "
         "given parameter or the lane's one parameter; the bend of the segment after each, -1 to 1), what it drives "
         "with each range, and whether it plays. Give from and to for part of it.",
         object([&] {
             auto p = paramSchema();
             p["lane"] = string("The lane (\"a:12\"), instead of a parameter");
             p["from"] = position("Only points from here");
             p["to"] = position("Only points up to here");
             return p;
         }()),
         laneResultSchema(), reads, false},
        [&env](const Json& args, CallContext&) {
            const auto [lane, by] = laneOrParam(env, args);
            if (!lane)
            {
                Json j{{"lane", nullptr}, {"assignments", Json::array()}, {"points", Json::array()}, {"on", true},
                       {"parameter", paramJson(env, by->target)}};
                return ToolResult::ok(j, by->name + " isn't automated.");
            }
            const auto j = laneJson(env, *lane, by ? std::optional(by->target) : std::nullopt, timeRange(env, args));
            return ToolResult::ok(j, j["name"].get<std::string>() + ": " + std::to_string(j["points"].size()) + " points" +
                                         (j["on"].get<bool>() ? "." : " (some parameters keep their own value)."));
        }));

    registry.tools.push_back(make(
        env,
        {"set_automation", "Write automation",
         "Writes points into an automation lane (lane), or a parameter's lane (made for it if it has none): each a "
         "position (beats or \"5.1.1\"), a value and optionally the bend of the segment after it (-1 to 1; 0 is "
         "straight). Values are the parameter's (its unit or text: \"1.2 kHz\", \"-6 dB\", \"25L\"), within its "
         "range on the lane; for a lane driving several parameters, without one named, 0 to 1. A straight segment "
         "moves the control evenly. The points replace the lane's (or, with from and to, only those in that range). "
         "Every parameter on the lane follows. One undo step.",
         object([&] {
             auto p = paramSchema();
             p["lane"] = string("The lane (\"a:12\"), instead of a parameter");
             p["points"] = arrayOf(object({{"position", position("Where")},
                                           {"value", {{"type", {"number", "string", "boolean"}},
                                                      {"description", "The parameter's value, or its text"}}},
                                           {"bend", number("The segment after it: -1 .. 1 (0 straight)")}},
                                          {"position", "value"}),
                                   "The points", 0);
             p["from"] = position("Replace only from here");
             p["to"] = position("Replace only up to here");
             return p;
         }(), {"points"}),
         laneResultSchema(), changes},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            auto [lane, by] = laneOrParam(env, args);
            if (!lane)
            {
                const auto made = s.automateInNewLane(by->target);
                if (made == 0)
                    throw Problem{by->name + " can't be automated."};
                lane = made;
            }
            const auto info = laneInfo(env, *lane);
            model::AutomationPoints points;
            for (const auto& p : args["points"])
            {
                const double at = seconds(env, positionArg(env, p["position"]));
                points.push_back({at, laneValueArg(env, info, by, p["value"]),
                                  model::curveForBend(p.contains("bend") ? p["bend"].get<float>() : 0.0f)});
            }
            std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
            s.setLanePoints(*lane, points, timeRange(env, args));
            auto j = laneJson(env, *lane, by ? std::optional(by->target) : std::nullopt);
            std::string also;
            for (const auto& a : j["assignments"])
                if (!by || a["name"] != by->name)
                    also += (also.empty() ? "" : ", ") + a["name"].get<std::string>();
            return ToolResult::ok(j, j["name"].get<std::string>() + ": wrote " + std::to_string(points.size()) + " points (" +
                                         std::to_string(j["points"].size()) + " on the lane)" +
                                         (by && !also.empty() ? "; it also moves " + also + "." : "."));
        }));

    registry.tools.push_back(make(
        env,
        {"clear_automation", "Clear automation",
         "Removes the points of an automation lane (lane), or of a parameter's lane (all of them, or those between "
         "from and to). Without points its parameters keep their own values. By parameter it refuses when the lane "
         "drives others too: clear the lane, or take the parameter off it (unassign_automation). One undo step.",
         object([&] {
             auto p = paramSchema();
             p["lane"] = string("The lane (\"a:12\"), instead of a parameter");
             p["from"] = position("Clear from here");
             p["to"] = position("Clear up to here");
             return p;
         }()),
         laneResultSchema(), removes},
        [&env](const Json& args, CallContext&) {
            const auto [lane, by] = laneOrParam(env, args);
            if (!lane)
                return ToolResult::ok({{"lane", nullptr}, {"assignments", Json::array()}, {"points", Json::array()}},
                                      by->name + " isn't automated.");
            if (by && laneInfo(env, *lane).assignments.size() > 1)
                throw Problem{by->name + " shares its lane (" + formatId(IdKind::Lane, *lane) +
                              ") with other parameters: clear the lane (lane), or take it off (unassign_automation)."};
            env.session.setLanePoints(*lane, {}, timeRange(env, args), "Clear automation");
            const auto j = laneJson(env, *lane, by ? std::optional(by->target) : std::nullopt);
            return ToolResult::ok(j, "Cleared " + j["name"].get<std::string>() + ".");
        }));

    registry.tools.push_back(make(
        env,
        {"set_automation_on", "Turn automation back on",
         "Turns a parameter's automation back on (or off), every parameter of a lane (lane), or with all every one "
         "back on. A parameter's automation turns off when its control is moved by hand (or by set_parameters, "
         "set_mix, set_send) while not recording: it keeps that value until turned back on. Not an undo step.",
         object([&] {
             auto p = paramSchema();
             p["lane"] = string("Every parameter of this lane (\"a:12\")");
             p["on"] = boolean("On (default) or off");
             p["all"] = boolean("Every parameter back on");
             return p;
         }()),
         result({{"on", boolean("")}}), changes, false},
        [&env](const Json& args, CallContext&) {
            auto& s = env.session;
            if (args.value("all", false))
            {
                s.setAllLanesOn();
                s.flushSnapshot();
                return ToolResult::ok({{"on", true}, {"all", true}}, "All automation plays again.");
            }
            const bool on = args.value("on", true);
            if (args.contains("lane"))
            {
                const auto id = laneArg(env, args["lane"]);
                s.setLaneOn(id, on);
                const auto j = laneJson(env, id, std::nullopt, std::nullopt, false);
                return ToolResult::ok(j, j["name"].get<std::string>() + (on ? " plays." : " is off."));
            }
            const auto p = param(env, args);
            const auto lane = s.laneOf(p.target);
            if (!lane)
                throw Problem{p.name + " isn't on an automation lane."};
            s.setAssignmentOn(p.target, on);
            const auto j = laneJson(env, *lane, p.target, std::nullopt, false);
            return ToolResult::ok(j, p.name + (on ? " follows its lane." : " keeps its own value."));
        }));
}

namespace tools
{
Json automationLanesJson(ToolEnv& env)
{
    Json lanes = Json::array();
    for (const auto& l : env.session.snapshot()->automationLanes)
        lanes.push_back(laneJson(env, l.id, std::nullopt, std::nullopt, false));
    return lanes;
}

Json parameterAutomationJson(ToolEnv& env, const engine::Session::ParamTarget& target)
{
    const auto snapshot = env.session.snapshot();
    const auto [lane, a] = snapshot->assignmentOf(target.channel, target.key);
    if (lane == nullptr)
        return nullptr;
    return {{"lane", formatId(IdKind::Lane, lane->id)},
            {"points", lane->points.size()},
            {"from", env.session.paramText(target, a->range.from)},
            {"to", env.session.paramText(target, a->range.to)},
            {"on", a->on}};
}
} // namespace tools
} // namespace fd::mcp
