#include "ui/ClipLoopBrace.h"

#include "model/Editing.h"

namespace fd::app
{
model::BraceRange ClipLoopBrace::rangeOf(const model::ClipInfo& clip, double origin)
{
    if (!clip.loop.on())
        return {clip.start, clip.end()};
    return {origin + clip.loop.start, origin + clip.loop.end()};
}

model::BraceZone ClipLoopBrace::zoneAt(const model::ClipInfo& clip, double origin, const model::Timeline& timeline, float x)
{
    const auto [a, b] = rangeOf(clip, origin);
    return model::braceZoneAt(x, timeline.xForTime(a), timeline.xForTime(b), ui::Theme::current().input.edgeGrab);
}

void ClipLoopBrace::press(const model::ClipInfo& clip, double origin, const model::Timeline& timeline, float x)
{
    atPress_ = rangeOf(clip, origin);
    grabbed_ = zoneAt(clip, origin, timeline, x);
    preview_.reset();
}

void ClipLoopBrace::drag(const model::ClipInfo& clip, const model::Timeline& timeline, float from, float x,
                         const std::function<double(double)>& snap)
{
    const auto grab = !clip.loop.on() && grabbed_ == model::BraceZone::Body ? model::BraceZone::None : grabbed_;
    preview_ = model::dragBrace(grab, atPress_, timeline.timeForX(from), timeline.timeForX(x), snap, model::minClipLength);
}

void ClipLoopBrace::release(engine::Session& session, const model::ClipInfo& clip, double origin, bool moved)
{
    const auto release = model::releaseBrace(grabbed_, moved, preview_);
    if (release.range)
        session.setClipLoop(clip.id, {release.range->first - origin, release.range->second - release.range->first});
    if (release.toggle)
        session.setClipsLooping({clip.id}, !clip.loop.on());
    preview_.reset();
}
} // namespace fd::app
