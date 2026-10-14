// Controllers in MIDI clips (M3 P4b, docs/design/piano-roll.md §11, §14): a clip's controller lanes
// (pitch bend, the mod wheel, the sustain pedal, any CC, aftertouch) as points, read and written as
// the clip view's lanes are; each write one undo step.

#include "mcp/tools/Tools.h"
#include "model/Controllers.h"

#include <cmath>

namespace fd::mcp
{
using namespace tools;

namespace tools
{
Json controllerJson(int controller)
{
    if (controller == model::controllers::pitchBend)
        return "pitch_bend";
    if (controller == model::controllers::aftertouch)
        return "aftertouch";
    return controller;
}

int controllerArg(const Json& value)
{
    if (value.is_number_integer() && model::isController(value.get<int>()) && value.get<int>() < 128)
        return value.get<int>();
    if (value.is_string())
    {
        static const std::pair<const char*, int> words[] = {
            {"pitch_bend", model::controllers::pitchBend},  {"aftertouch", model::controllers::aftertouch},
            {"mod_wheel", model::controllers::modWheel},    {"breath", model::controllers::breath},
            {"expression", model::controllers::expression}, {"sustain", model::controllers::sustain}};
        for (const auto& [word, controller] : words)
            if (value.get<std::string>() == word)
                return controller;
    }
    throw Problem{
        "controller: " + value.dump() +
        " isn't a CC number (0-127) or one of pitch_bend, aftertouch, mod_wheel, breath, expression, "
        "sustain."};
}
} // namespace tools

namespace
{
model::ClipInfo midiClipArg(ToolEnv& env, const Json& value)
{
    auto c = clip(env, value);
    if (c.kind != model::ClipInfo::Kind::Midi)
        throw Problem{formatId(IdKind::Clip, c.id) +
                      " is an audio clip; controllers go in MIDI clips (create_midi_clip makes one)."};
    return c;
}

/// A point's value as tools write it: 0-127, pitch bend −8192 … 8191 (0 at rest).
Json valueJson(int controller, float level)
{
    const int midi = model::controllerValue(controller, level);
    return controller == model::controllers::pitchBend ? midi - 8192 : midi;
}

float levelArg(int controller, const Json& value, const std::string& where)
{
    if (!value.is_number())
        throw Problem{where + ": a number."};
    const double v = value.get<double>();
    if (controller == model::controllers::pitchBend)
    {
        if (!(v >= -8192.0 && v <= 8191.0))
            throw Problem{where + ": pitch bend is -8192 to 8191 (0 at rest)."};
        return model::controllerLevel(controller, static_cast<int>(std::lround(v)) + 8192);
    }
    if (!(v >= 0.0 && v <= 127.0))
        throw Problem{where + ": 0-127."};
    return model::controllerLevel(controller, static_cast<int>(std::lround(v)));
}

Json laneJson(const model::ControllerLane& lane, double offset)
{
    Json points = Json::array();
    for (const auto& p : lane.points)
    {
        Json j{{"beat", std::round((p.time - offset) * 1e6) / 1e6},
               {"value", valueJson(lane.controller, p.value)}};
        if (p.curve > 0.5f)
            j["step"] = true;
        else if (const float bend = model::bendOf(p); bend != 0.0f)
            j["bend"] = std::round(bend * 1000.0f) / 1000.0f;
        points.push_back(std::move(j));
    }
    return {{"controller", controllerJson(lane.controller)},
            {"name", model::controllerName(lane.controller)},
            {"points", std::move(points)}};
}

Json lanesResult()
{
    return result({{"clip", clipResultSchema()},
                   {"lanes", {{"type", "array"}, {"description", "The lanes: controller, name and points"}}}},
                  {"clip", "lanes"});
}

Json controllerSchema()
{
    return {{"type", {"integer", "string"}},
            {"description", "A CC number (0-127: 1 the mod wheel, 64 sustain), or pitch_bend, aftertouch, "
                            "mod_wheel, breath, expression, sustain"}};
}
} // namespace

void addControllerTools(Registry& registry, ToolEnv& env)
{
    ToolAnnotations reads;
    reads.readOnly = true;
    ToolAnnotations replaces;
    replaces.destructive = true;
    replaces.idempotent = true;

    registry.tools.push_back(make(
        env,
        {"get_controllers", "Get controllers",
         "A MIDI clip's controller lanes (piano-roll.md §11): each controller's points, beats from "
         "the clip's start (as notes count). Values are 0-127; pitch bend -8192 to 8191, 0 at rest. A "
         "point holds until the next with step, else moves to it in a line, or bent (-1 to 1). Recorded "
         "controllers are points that hold.",
         object({{"clip", id(IdKind::Clip, "The MIDI clip")}, {"controller", controllerSchema()}}, {"clip"}),
         lanesResult(), reads},
        [&env](const Json& args, CallContext&)
        {
            const auto c = midiClipArg(env, args["clip"]);
            const auto only =
                args.contains("controller") ? std::optional(controllerArg(args["controller"])) : std::nullopt;
            const double offset = noteOffsetBeats(env, c);
            Json lanes = Json::array();
            for (const auto& lane : c.controllers)
                if (!only || lane.controller == *only)
                    lanes.push_back(laneJson(lane, offset));
            return ToolResult::ok({{"clip", clipJson(env, c, false)}, {"lanes", std::move(lanes)}});
        }));

    registry.tools.push_back(make(
        env,
        {"set_controllers", "Set controllers",
         "Replaces a MIDI clip's lanes for the controllers given (others stay), as the clip view draws "
         "them: points at beats from the clip's start, played as MIDI with lines between them. An empty "
         "list of points removes the controller's lane. The sustain pedal and the other pedals (CC "
         "64-69) are on (127) or off (0).",
         object(
             {{"clip", id(IdKind::Clip, "The MIDI clip")},
              {"lanes",
               arrayOf(
                   object({{"controller", controllerSchema()},
                           {"points",
                            arrayOf(object({{"beat", number("Beats from the clip's start")},
                                            {"value", number("0-127; pitch bend -8192 to 8191, 0 at rest")},
                                            {"step", boolean("Hold until the next point (default: a line)")},
                                            {"bend", number("The line to the next point bent, -1 to 1")}},
                                           {"beat", "value"}),
                                    "The points, in any order", 0)}},
                          {"controller", "points"}),
                   "The lanes to write")}},
             {"clip", "lanes"}),
         lanesResult(), replaces},
        [&env](const Json& args, CallContext&)
        {
            const auto c = midiClipArg(env, args["clip"]);
            const double offset = noteOffsetBeats(env, c);
            std::vector<model::ControllerLane> lanes;
            for (std::size_t k = 0; k < args["lanes"].size(); ++k)
            {
                const auto& l = args["lanes"][k];
                const auto where = "lanes[" + std::to_string(k) + "]";
                model::ControllerLane lane{.controller = controllerArg(l["controller"])};
                const auto rules = model::controllerRules(lane.controller);
                for (std::size_t i = 0; i < l["points"].size(); ++i)
                {
                    const auto& p = l["points"][i];
                    const auto at = where + ".points[" + std::to_string(i) + "]";
                    const double beat = p["beat"].get<double>();
                    if (!(beat >= 0.0))
                        throw Problem{at + ".beat: 0 or later."};
                    float level = levelArg(lane.controller, p["value"], at + ".value");
                    if (rules.stepped())
                        level = model::snapToSteps(level, rules.steps);
                    const bool step = rules.stepped() || p.value("step", false);
                    const float curve =
                        step ? model::kStepCurve
                             : model::curveForBend(std::clamp(p.value("bend", 0.0f), -1.0f, 1.0f));
                    lane.points.push_back({.time = beat + offset, .value = level, .curve = curve});
                }
                std::stable_sort(lane.points.begin(), lane.points.end(),
                                 [](const auto& a, const auto& b) { return a.time < b.time; });
                lanes.push_back(std::move(lane));
            }
            env.session.setControllers(c.id, lanes);
            env.session.flushSnapshot();
            const auto after = midiClipArg(env, args["clip"]);
            Json out = Json::array();
            for (const auto& lane : after.controllers)
                out.push_back(laneJson(lane, offset));
            return ToolResult::ok({{"clip", clipJson(env, after, false)}, {"lanes", std::move(out)}});
        }));
}
} // namespace fd::mcp
