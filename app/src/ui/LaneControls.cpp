#include "ui/LaneControls.h"

namespace fd::app
{
LaneBar::LaneBar(engine::Session& session) : session_(session)
{
    setStyle(Style::Bar);
    setName({});
    textFromValue = [this](float v) { return target_ ? juce::String(session_.paramText(*target_, v)) : juce::String(); };
    valueFromText = [this](const juce::String& text) -> std::optional<float> {
        return target_ ? session_.paramFromText(*target_, text.toStdString()) : std::nullopt;
    };
    onGestureStart = [this] {
        if (target_)
            gesture_.emplace(session_, *target_, "Automation lane");
    };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) {
        if (target_)
            session_.setParamValue(*target_, v);
    };
}

void LaneBar::setTarget(const std::optional<engine::Session::ParamTarget>& target, int steps)
{
    setNumSteps(steps >= 2 ? steps : 0);
    if (target == target_)
        return;
    gesture_.reset();
    target_ = target;
    setArcOrigin(target_ && target_->key == "pan" ? 0.5f : 0.0f);
    refresh();
}

void LaneBar::refresh()
{
    if (gesture_ || !target_)
        return;
    if (const auto v = session_.paramValue(*target_); v && std::abs(*v - getValue()) > 1e-6f)
        setValue(*v, juce::dontSendNotification);
}

LaneControls::LaneControls(engine::Session& s, std::uint64_t id) : session(s), lane(id), bar(s)
{
    on.setClickingTogglesState(false); // the snapshot says what it is
    on.setWantsKeyboardFocus(false);
    on.setTooltip("Lane on: its parameters follow its points. Off: they keep the values set by hand");
    // Any off: every one back on; else all off (automation.md §3).
    on.onClick = [this] { session.setLaneOn(lane, !allOn); };
}

void LaneControls::update(const model::AutomationLaneInfo& info)
{
    allOn = info.allOn();
    on.setToggleState(allOn, juce::dontSendNotification);
    if (info.assignments.size() == 1)
    {
        const auto& a = info.assignments.front();
        bar.setTarget(engine::Session::ParamTarget{a.channel, a.key}, a.steps);
        bar.setTooltip(juce::String::fromUTF8(a.name.c_str()) + " (" + juce::String::fromUTF8(a.channelName.c_str()) +
                       juce::String::fromUTF8(" \xC2\xB7 ") + juce::String::fromUTF8(a.module.c_str()) + ")");
    }
    else
        bar.setTarget(std::nullopt, 0);
}
} // namespace fd::app
