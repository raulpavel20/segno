#pragma once

#include "engine/Session.h"
#include "model/BraceDrag.h"
#include "model/Snapshot.h"
#include "model/Timeline.h"

#include <fd_ui/fd_ui.h>

#include <functional>
#include <optional>

namespace fd::app
{
/// The clip views' loop brace (M3 step C10, clip-loops.md §6): the piano roll's and the audio clip
/// view's, with the transport brace's gestures (model::BraceDrag). Times are the view's: its
/// content's zero at `origin` (model::clipViewOrigin). While Loop is off the brace shows what the
/// clip would loop (itself), and a drag inside it draws a loop: there's none to move.
class ClipLoopBrace
{
public:
    /// The brace: the clip's loop, or the clip.
    [[nodiscard]] static model::BraceRange rangeOf(const model::ClipInfo& clip, double origin);
    /// What a press `x` pixels from the view's time zero would grab (cursors).
    [[nodiscard]] static model::BraceZone zoneAt(const model::ClipInfo& clip, double origin, const model::Timeline& timeline, float x);

    void press(const model::ClipInfo& clip, double origin, const model::Timeline& timeline, float x);
    /// A drag from the press to `x`; `snap` maps a time to the view's grid.
    void drag(const model::ClipInfo& clip, const model::Timeline& timeline, float from, float x,
              const std::function<double(double)>& snap);
    /// Letting go: the clip's loop set (Loop on), or a click on the brace switching Loop; one undo
    /// step each.
    void release(engine::Session& session, const model::ClipInfo& clip, double origin, bool moved);
    /// The brace while dragging.
    [[nodiscard]] const std::optional<model::BraceRange>& preview() const noexcept { return preview_; }

private:
    model::BraceZone grabbed_ = model::BraceZone::None;
    model::BraceRange atPress_;
    std::optional<model::BraceRange> preview_;
};
} // namespace fd::app
