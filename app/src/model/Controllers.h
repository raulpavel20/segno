#pragma once

#include "model/Automation.h"

#include <string>
#include <vector>

namespace fd::model
{
/// A clip's controllers (docs/design/piano-roll.md §11): what a player's hand does besides the
/// keys (the pitch wheel, the mod wheel, the sustain pedal), as lanes of points edited as
/// automation lanes are, and played as the MIDI events the lane makes.

/// Which controller: 0..127 a control change (CC), or one of these.
namespace controllers
{
inline constexpr int modWheel = 1;
inline constexpr int breath = 2;
inline constexpr int expression = 11;
inline constexpr int sustain = 64;
inline constexpr int pitchBend = 128;
inline constexpr int aftertouch = 129; // the channel's pressure
} // namespace controllers

struct ControllerLane
{
    int controller = controllers::modWheel;
    /// Time: beats from the clip's content start. Value: 0..1 of the controller's range (pitch
    /// bend: 0.5 is the wheel at rest).
    AutomationPoints points;

    friend bool operator==(const ControllerLane&, const ControllerLane&) = default;
};

/// "Mod wheel", "Sustain", "Pitch bend", "Aftertouch", "CC 74".
[[nodiscard]] std::string controllerName(int controller);
/// A control change, pitch bend or aftertouch.
[[nodiscard]] bool isController(int controller) noexcept;
/// Its value at rest: pitch bend centred, the rest at 0.
[[nodiscard]] float restingValue(int controller) noexcept;
/// The pedals (CC 64–69: sustain, portamento, sostenuto, soft, legato, hold 2) are on or off, so
/// their lanes are steps of two values.
[[nodiscard]] LaneRules controllerRules(int controller) noexcept;
/// What "Add controller ▸" offers first, in its order.
[[nodiscard]] std::vector<int> commonControllers();

/// A lane's value as MIDI (0..127; pitch bend 0..16 383, 8 192 at rest), and back.
[[nodiscard]] int controllerValue(int controller, float value) noexcept;
[[nodiscard]] float controllerLevel(int controller, int value) noexcept;

/// One MIDI event of a lane.
struct ControllerEvent
{
    enum class Kind
    {
        Point, // one of the lane's points
        Ramp,  // between two points, made from the curve
        Played // recorded, or written elsewhere: a point that holds until the next
    };

    double beat = 0.0;
    int value = 0; // as MIDI (controllerValue)
    Kind kind = Kind::Point;
    float curve = 0.0f; // a point's, as AutomationPoint::curve

    friend bool operator==(const ControllerEvent&, const ControllerEvent&) = default;
};

/// The events a lane plays: each point's own and, between two points that aren't a step, the ramp
/// the curve makes, an event wherever the MIDI value changes, at most every `step` beats.
[[nodiscard]] std::vector<ControllerEvent> controllerEvents(const ControllerLane& lane,
                                                            double step = 1.0 / 64.0);

/// A lane's points from its events, in time order: each point as written, each played event a
/// point that holds; the ramps (made from the points) are left out.
[[nodiscard]] AutomationPoints controllerPoints(int controller, const std::vector<ControllerEvent>& events);
} // namespace fd::model
