#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
/// A lane's one assigned parameter as a bar (docs/design/automation.md §3): the lane header's
/// control, the same as moving the knob or fader it stands for (its assignment turns off). Works
/// in the control's travel.
class LaneBar final : public ui::Knob
{
public:
    explicit LaneBar(engine::Session& session);
    /// The parameter it moves (none: nothing to move).
    void setTarget(const std::optional<engine::Session::ParamTarget>& target, int steps);
    [[nodiscard]] const std::optional<engine::Session::ParamTarget>& target() const noexcept { return target_; }
    /// Follows the parameter's value now (played or set), unless being dragged.
    void refresh();

private:
    engine::Session& session_;
    std::optional<engine::Session::ParamTarget> target_;
    std::optional<engine::Session::ParamGesture> gesture_;
};

/// One lane's controls in its header: the on switch and its one assignment's bar.
struct LaneControls
{
    LaneControls(engine::Session& session, std::uint64_t lane);
    void update(const model::AutomationLaneInfo& lane);

    engine::Session& session;
    std::uint64_t lane;
    bool allOn = true;
    ui::Toggle on; // a round indicator, like a module's power
    LaneBar bar;
};
} // namespace fd::app
