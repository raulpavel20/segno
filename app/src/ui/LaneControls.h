#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
/// The parameter of an automation lane as a bar (docs/design/automation.md §3): the lane header's
/// control, the same as moving the knob or fader it stands for (an automated lane turns off).
/// Works in lane units: the control's travel.
class LaneBar final : public ui::Knob
{
public:
    LaneBar(engine::Session& session, engine::Session::LaneTarget target);
    /// Follows the parameter's value now (played or set), unless being dragged.
    void refresh();
    void setStepped(int steps);

private:
    engine::Session& session_;
    engine::Session::LaneTarget target_;
    std::optional<engine::Session::LaneGesture> gesture_;
};

/// One lane's controls in its header: the on switch and the bar.
struct LaneControls
{
    LaneControls(engine::Session& session, const engine::Session::LaneTarget& target);
    void update(const model::LaneInfo& lane);

    engine::Session& session;
    engine::Session::LaneTarget target;
    ui::Toggle on; // a round indicator, like a module's power
    LaneBar bar;
};
} // namespace fd::app
