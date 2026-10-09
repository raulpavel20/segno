#include "ui/LaneControls.h"

namespace fd::app
{
LaneBar::LaneBar(engine::Session& session, engine::Session::LaneTarget target)
    : session_(session), target_(std::move(target))
{
    setStyle(Style::Bar);
    setName({});
    if (target_.lane == "pan")
        setArcOrigin(0.5f);
    textFromValue = [this](float v) { return juce::String(session_.laneText(target_, v)); };
    valueFromText = [this](const juce::String& text) { return session_.laneFromText(target_, text.toStdString()); };
    onGestureStart = [this] { gesture_.emplace(session_, target_, "Automation lane"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) { session_.setLaneValue(target_, v); };
    refresh();
}

void LaneBar::refresh()
{
    if (gesture_)
        return;
    if (const auto v = session_.laneValue(target_); v && std::abs(*v - getValue()) > 1e-6f)
        setValue(*v, juce::dontSendNotification);
}

void LaneBar::setStepped(int steps)
{
    setNumSteps(steps >= 2 ? steps : 0);
}

LaneControls::LaneControls(engine::Session& s, const engine::Session::LaneTarget& t)
    : session(s), target(t), bar(s, t)
{
    on.setClickingTogglesState(false); // the snapshot says what it is
    on.setWantsKeyboardFocus(false);
    on.setTooltip("Lane on: the parameter follows its points. Off: it keeps the value set by hand");
    on.onClick = [this] { session.setLaneOn(target, !on.getToggleState()); };
}

void LaneControls::update(const model::LaneInfo& lane)
{
    on.setToggleState(lane.on, juce::dontSendNotification);
    bar.setStepped(lane.steps);
    bar.setTooltip(juce::String::fromUTF8(lane.name.c_str()) + " (" + juce::String::fromUTF8(lane.module.c_str()) + ")");
}
} // namespace fd::app
