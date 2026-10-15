// Modulators (M3 P5, ADR 0061, docs/design/modulators.md §16): what each moves and how far, routes
// made, set and removed, and their rolls kept. A modulator is a module: add_module, set_parameters,
// move_module, set_module_on and remove_module take it as any.

#include "mcp/tools/Tools.h"
#include "model/Chance.h"
#include "model/Modulators.h"
#include "model/Racks.h"

#include <cmath>
#include <fd/dsp/Modulators.h>

namespace fd::mcp
{
using namespace tools;

namespace
{
using ParamTarget = engine::Session::ParamTarget;

/// The modulator an argument names, and its track.
std::pair<model::DeviceSlot, model::TrackInfo> modulatorArg(ToolEnv& env, const Json& value)
{
    auto found = module(env, value);
    if (found.first.modulator == nullptr)
        throw Problem{formatId(IdKind::Module, found.first.id) + " (" + found.first.name +
                      ") isn't a modulator; get_modulators lists a track's."};
    return found;
}

/// A route's amount from %: -200 ... +200 (both ways: half each way at 100; below 0 the other way).
float amountArg(const Json& args, float fallback)
{
    if (!args.contains("amount"))
        return fallback;
    const double percent = args["amount"].get<double>();
    if (!std::isfinite(percent))
        throw Problem{"amount is a percentage from -200 to 200."};
    return static_cast<float>(std::clamp(percent, -200.0, 200.0) / 100.0);
}

double percent(float amount)
{
    return std::round(static_cast<double>(amount) * 1000.0) / 10.0;
}

/// Refuses a route, saying why a modulator can't move the parameter (modulators.md §8).
[[noreturn]] void refuse(const model::DeviceSlot& m, const model::TrackInfo& owner, const ParamRef& p)
{
    if (p.target.channel != owner.id)
        throw Problem{m.name + " moves what's on its own track (" + owner.name +
                      "), not another's: put a modulator on that track."};
    if (p.target.key.rfind("m" + std::to_string(m.id) + "/", 0) == 0)
        throw Problem{m.name + " can't move its own " + p.name + "."};
    if (const auto* rack = model::rackHolding(owner.devices, m.id))
        throw Problem{
            m.name + " is in " + rack->name +
            ": a rack's modulator moves only what's in the rack "
            "(its modules, macros and Morph). Move it out of the rack (move_module) to reach the rest."};
    throw Problem{"That would make a loop: " + p.name + " moves " + m.name +
                  ", through the modulators between them. Route another parameter, or remove a route first."};
}

Json targetSchema()
{
    return object(paramRefSchema(), {"parameter"});
}

Json routeResult()
{
    return result({{"modulator", string("Its id")},
                   {"target", result({{"parameter", string("")}, {"name", string("")}})},
                   {"amount", number("% of the parameter's range, -200 to 200")}},
                  {"modulator", "target"});
}

/// The route of `modulator` to `target` as the snapshot has it (null: none).
const model::ModulatorRouteInfo* routeOf(const model::DeviceSlot& m, const ParamTarget& target)
{
    for (const auto& r : m.modulator->routes)
        if (r.channel == target.channel && r.key == target.key)
            return &r;
    return nullptr;
}
} // namespace

void addModulatorTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations changes;
    changes.idempotent = true;
    ToolAnnotations removes;
    removes.destructive = true;
    ToolAnnotations reading;
    reading.readOnly = true;
    reading.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"get_modulators", "Get a track's modulators",
         "A track's modulators (modules that move other parameters on their track), in chain order: each "
         "one's "
         "type, place, settings, whether it keeps its roll (Random, Envelope, Steps), and its routes: each "
         "parameter it moves and how far (% of that parameter's range, -200 to 200; both ways, half of it "
         "each way at 100; below 0 the other way).",
         object({{"track", id(IdKind::Track, "The track or return track")}}, {"track"}),
         result(
             {{"track", string("")}, {"modulators", arrayOf(result({{"id", string("")}}), "", std::nullopt)}},
             {"track", "modulators"}),
         reading},
        [&env](const Json& args, CallContext&)
        {
            const auto t = channel(env, args["track"]);
            Json list = Json::array();
            for (size_t i = 0; i < t.devices.size(); ++i)
            {
                const auto& d = t.devices[i];
                if (d.modulator == nullptr)
                    continue;
                Json settings = Json::object();
                if (auto handle = env.session.openModulator(d.id))
                {
                    auto& context = handle->context();
                    for (size_t p = 0; p < context.info.params.size(); ++p)
                        if (const auto& spec = context.info.params[p]; !spec.hasFlag(ParamFlags::Hidden))
                            settings[std::string(spec.id)] = spec.toText(context.params.getPlain(p));
                }
                auto j = modulatorJson(env, d);
                j["id"] = formatId(IdKind::Module, d.id);
                j["name"] = d.name;
                j["type"] = d.deviceId;
                j["on"] = d.enabled;
                j["position"] = i;
                j["settings"] = std::move(settings);
                list.push_back(std::move(j));
            }
            return ToolResult::ok(
                {{"track", formatId(IdKind::Track, t.id)}, {"modulators", std::move(list)}});
        }));

    registry.tools.push_back(make(
        env,
        {"route_modulator", "Route a modulator to a parameter",
         "Makes a modulator move a parameter on its own track: a module's (a rack's macro or Morph too, "
         "\"macro1\"... \"morph\"), or the track's volume, pan or a send. The parameter keeps its own value; "
         "the modulator moves it around that. amount is % of its range (default 50; -200 to 200; below 0 "
         "the other way); a route already there takes the amount. Refused, saying why, for another track's "
         "parameter, the modulator's own, or a loop. One undo step.",
         object({{"modulator", id(IdKind::Module, "The modulator")},
                 {"target", targetSchema()},
                 {"amount", number("% of the parameter's range (default 50)")}},
                {"modulator", "target"}),
         routeResult(), changes},
        [&env](const Json& args, CallContext&)
        {
            const auto [m, owner] = modulatorArg(env, args["modulator"]);
            const auto p = paramRef(env, args["target"]);
            const float amount = amountArg(args, 0.5f);
            if (!env.session.routeModulator(m.id, p.target, amount))
                refuse(m, owner, p);
            return ToolResult::ok({{"modulator", formatId(IdKind::Module, m.id)},
                                   {"target", paramRefJson(env, p.target)},
                                   {"amount", percent(amount)}},
                                  m.name + " moves " + p.name + ".");
        }));

    registry.tools.push_back(
        make(env,
             {"set_route", "Set a route's amount",
              "How far a modulator moves a parameter it's routed to: % of the parameter's range, -200 to 200 "
              "(below 0 the other way). One undo step.",
              object({{"modulator", id(IdKind::Module, "The modulator")},
                      {"target", targetSchema()},
                      {"amount", number("% of the parameter's range")}},
                     {"modulator", "target", "amount"}),
              routeResult(), changes},
             [&env](const Json& args, CallContext&)
             {
                 const auto [m, owner] = modulatorArg(env, args["modulator"]);
                 const auto p = paramRef(env, args["target"]);
                 if (routeOf(m, p.target) == nullptr)
                     throw Problem{m.name + " doesn't move " + p.name + "; route_modulator routes it."};
                 const float amount = amountArg(args, 0.5f);
                 env.session.setRouteAmount(m.id, p.target, amount);
                 return ToolResult::ok({{"modulator", formatId(IdKind::Module, m.id)},
                                        {"target", paramRefJson(env, p.target)},
                                        {"amount", percent(amount)}});
             }));

    registry.tools.push_back(
        make(env,
             {"remove_route", "Remove a route",
              "A modulator no longer moves a parameter (it keeps its own value). One undo step.",
              object({{"modulator", id(IdKind::Module, "The modulator")}, {"target", targetSchema()}},
                     {"modulator", "target"}),
              routeResult(), removes},
             [&env](const Json& args, CallContext&)
             {
                 const auto [m, owner] = modulatorArg(env, args["modulator"]);
                 const auto p = paramRef(env, args["target"]);
                 if (!env.session.removeRoute(m.id, p.target))
                     throw Problem{m.name + " doesn't move " + p.name + "."};
                 return ToolResult::ok({{"modulator", formatId(IdKind::Module, m.id)},
                                        {"target", paramRefJson(env, p.target)}});
             }));

    registry.tools.push_back(make(
        env,
        {"set_lfo_curve", "Draw an LFO's shape",
         "The shape an LFO modulator plays while its Shape is Drawn (§12.1): a named shape to start from "
         "(sine, triangle, saw-up, saw-down, square, steps), or points [x, y, bend] from x 0 to 1 (y 0 to 1, "
         "bottom to top; bend -1 to 1 bows each segment). Sets Shape to Drawn. One undo step.",
         object({{"modulator", id(IdKind::Module, "The LFO")},
                 {"shape", string("A named shape")},
                 {"points",
                  arrayOf({{"type", "array"}, {"items", number("")}}, "[x, y, bend] points, x rising", 2)}},
                {"modulator"}),
         result({{"modulator", string("")}, {"curve", result({})}}, {"modulator", "curve"}), changes},
        [&env](const Json& args, CallContext&)
        {
            const auto [m, owner] = modulatorArg(env, args["modulator"]);
            if (m.modulator->kind != model::ModulatorKind::Lfo)
                throw Problem{m.name + " isn't an LFO: only an LFO plays a drawn shape."};
            dsp::DrawnCurve curve;
            if (args.contains("points"))
            {
                float x = -1.0f;
                for (const auto& p : args["points"])
                {
                    if (!p.is_array() || p.size() != 3)
                        throw Problem{"Each point is [x, y, bend]."};
                    const std::array<float, 3> point{std::clamp(p[0].get<float>(), 0.0f, 1.0f),
                                                     std::clamp(p[1].get<float>(), 0.0f, 1.0f),
                                                     std::clamp(p[2].get<float>(), -1.0f, 1.0f)};
                    if (point[0] < x)
                        throw Problem{"The points go left to right: each x at least the one before."};
                    x = point[0];
                    curve.points.push_back(point);
                }
                curve.points.front()[0] = 0.0f; // from one end to the other
                curve.points.back()[0] = 1.0f;
            }
            else
            {
                const auto shape = args.value("shape", std::string("sine"));
                const auto named = dsp::namedCurveShapes();
                if (std::find(named.begin(), named.end(), shape) == named.end())
                    throw Problem{"There's no shape \"" + shape +
                                  "\": sine, triangle, saw-up, saw-down, square or steps."};
                curve.shape = shape;
            }
            auto handle = env.session.openModulator(m.id);
            if (handle == nullptr)
                throw Problem{"The LFO couldn't be opened."};
            handle->host().setDrawnCurve(curve);
            auto& params = handle->context().params;
            params.setPlain(model::modulator::Lfo::Shape, static_cast<float>(dsp::mod::Shape::Drawn));
            env.session.flushSnapshot();
            const auto now = handle->host().drawnCurve();
            return ToolResult::ok(
                {{"modulator", formatId(IdKind::Module, m.id)},
                 {"curve", now.points.empty() ? Json{{"shape", now.shape}} : Json{{"points", now.points}}}});
        }));

    registry.tools.push_back(make(
        env,
        {"keep_roll", "Keep what was left to chance, or let it roll",
         "A Random, a Steps with chances, an Envelope with Chance, and a MIDI clip's notes left to chance "
         "roll their dice again each time the song plays and each time the loop comes round. keep true keeps "
         "the roll heard last, so it plays the same every time and exports as heard; keep false lets it roll "
         "again. For one modulator or clip, or with song true every one in the song. One undo step.",
         object({{"modulator", id(IdKind::Module, "A modulator that rolls dice")},
                 {"clip", id(IdKind::Clip, "A MIDI clip with notes left to chance")},
                 {"song", boolean("Every modulator and clip in the song")},
                 {"keep", boolean("Keep its roll (true) or let it roll (false)")}},
                {"keep"}),
         result(
             {{"modulators", arrayOf(result({{"id", string("")}, {"kept", boolean("")}}), "", std::nullopt)},
              {"clips", arrayOf(result({{"id", string("")}, {"kept", boolean("")}}), "", std::nullopt)}},
             {"modulators", "clips"}),
         changes},
        [&env](const Json& args, CallContext&)
        {
            const bool keep = args["keep"].get<bool>();
            const bool song = args.value("song", false);
            if (static_cast<int>(song) + static_cast<int>(args.contains("modulator")) +
                    static_cast<int>(args.contains("clip")) !=
                1)
                throw Problem{"Give modulator or clip, or song: true for every one in the song."};
            if (song)
                env.session.keepAllRolls(keep);
            else if (args.contains("clip"))
            {
                const auto c = clip(env, args["clip"]);
                if (c.kind != model::ClipInfo::Kind::Midi || !model::usesChance(c.notes))
                    throw Problem{"No note in " + c.name +
                                  " is left to chance: edit_notes sets a note's chance or velocity_spread."};
                env.session.keepChance(c.id, keep);
            }
            else
            {
                const auto [m, owner] = modulatorArg(env, args["modulator"]);
                if (!model::hasKeep(m.modulator->kind))
                    throw Problem{m.name + " rolls no dice: only a Random, a Steps and an Envelope do."};
                env.session.keepRoll(m.id, keep);
            }
            env.session.flushSnapshot();
            Json list = Json::array();
            const auto snapshot = env.session.snapshot();
            for (const auto* channels : {&snapshot->tracks, &snapshot->returns})
                for (const auto& t : *channels)
                    for (const auto& d : t.devices)
                        if (d.modulator != nullptr && model::hasKeep(d.modulator->kind))
                            list.push_back(
                                {{"id", formatId(IdKind::Module, d.id)}, {"kept", d.modulator->kept}});
            Json clips = Json::array();
            for (const auto& t : snapshot->tracks)
                for (const auto& c : t.clips)
                    if (c.kind == model::ClipInfo::Kind::Midi && model::usesChance(c.notes))
                        clips.push_back({{"id", formatId(IdKind::Clip, c.id)}, {"kept", c.chanceKept}});
            return ToolResult::ok({{"modulators", std::move(list)}, {"clips", std::move(clips)}});
        }));
}
} // namespace fd::mcp
