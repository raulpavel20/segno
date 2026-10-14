#include "model/Controllers.h"

#include <algorithm>
#include <cmath>

namespace fd::model
{
std::string controllerName(int controller)
{
    switch (controller)
    {
    case controllers::pitchBend:
        return "Pitch bend";
    case controllers::aftertouch:
        return "Aftertouch";
    case controllers::modWheel:
        return "Mod wheel";
    case controllers::breath:
        return "Breath";
    case 4:
        return "Foot pedal";
    case 7:
        return "Volume";
    case 10:
        return "Pan";
    case controllers::expression:
        return "Expression";
    case controllers::sustain:
        return "Sustain";
    case 65:
        return "Portamento";
    case 66:
        return "Sostenuto";
    case 67:
        return "Soft pedal";
    default:
        return "CC " + std::to_string(controller);
    }
}

bool isController(int controller) noexcept
{
    return controller >= 0 && controller <= controllers::aftertouch;
}

float restingValue(int controller) noexcept
{
    return controller == controllers::pitchBend ? 0.5f : 0.0f;
}

LaneRules controllerRules(int controller) noexcept
{
    return {.steps = controller >= 64 && controller <= 69 ? 2 : 0};
}

std::vector<int> commonControllers()
{
    return {controllers::pitchBend,  controllers::modWheel,   controllers::sustain,
            controllers::expression, controllers::aftertouch, controllers::breath};
}

int controllerValue(int controller, float value) noexcept
{
    const float v = std::clamp(value, 0.0f, 1.0f);
    // Pitch bend in 16 384ths, so the wheel at rest (8 192) is exactly half; the top is 16 383.
    if (controller == controllers::pitchBend)
        return std::min(16383, static_cast<int>(std::lround(v * 16384.0f)));
    return static_cast<int>(std::lround(v * 127.0f));
}

float controllerLevel(int controller, int value) noexcept
{
    if (controller == controllers::pitchBend) // the top (16 383) is all the way up
        return value >= 16383 ? 1.0f : static_cast<float>(std::max(value, 0)) / 16384.0f;
    return static_cast<float>(std::clamp(value, 0, 127)) / 127.0f;
}

std::vector<ControllerEvent> controllerEvents(const ControllerLane& lane, double step)
{
    std::vector<ControllerEvent> out;
    const auto& points = lane.points;
    const int c = lane.controller;
    step = std::max(step, 1e-6);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        out.push_back({.beat = p.time,
                       .value = controllerValue(c, p.value),
                       .kind = ControllerEvent::Kind::Point,
                       .curve = p.curve});
        if (i + 1 >= points.size() || p.curve > 0.5f)
            continue; // the last point, or a step: it holds
        const auto& next = points[i + 1];
        int last = out.back().value;
        for (double at = p.time + step; at < next.time - 1e-9; at += step)
        {
            const int v =
                controllerValue(c, segmentValueAt(p.time, p.value, next.time, next.value, p.curve, at));
            if (v == last)
                continue;
            out.push_back({.beat = at, .value = v, .kind = ControllerEvent::Kind::Ramp});
            last = v;
        }
    }
    return out;
}

AutomationPoints controllerPoints(int controller, const std::vector<ControllerEvent>& events)
{
    AutomationPoints out;
    for (const auto& e : events)
    {
        if (e.kind == ControllerEvent::Kind::Ramp)
            continue;
        out.push_back({.time = e.beat,
                       .value = controllerLevel(controller, e.value),
                       .curve = e.kind == ControllerEvent::Kind::Played ? kStepCurve : e.curve});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return out;
}
} // namespace fd::model
