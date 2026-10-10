// Fades and crossfades on clips in the arrangement (M2 step C6, ADR 0036, docs/design/fades.md
// §3, §4): drawing them with the functions the audio plays, their handles, gestures and menus.

#include "ui/ArrangementView.h"

#include "model/FadeEditing.h"

namespace fd::app
{
namespace
{
/// Points along a ramp across [x0, x1] in `body`: its gain from `gainAt(alpha)`, as heights.
juce::Path rampPath(float x0, float x1, juce::Rectangle<float> body, const std::function<float(double)>& gainAt)
{
    juce::Path p;
    const int steps = std::clamp(static_cast<int>((x1 - x0) * 0.5f), 2, 256);
    for (int i = 0; i <= steps; ++i)
    {
        const double alpha = static_cast<double>(i) / steps;
        const float x = x0 + (x1 - x0) * static_cast<float>(alpha);
        const float y = body.getBottom() - body.getHeight() * gainAt(alpha);
        if (i == 0)
            p.startNewSubPath(x, y);
        else
            p.lineTo(x, y);
    }
    return p;
}
} // namespace

const model::TrackInfo& ArrangementView::shownTrack(const model::TrackInfo& track) const
{
    return fadePreview_ && fadePreview_->id == track.id ? *fadePreview_ : track;
}

juce::Rectangle<float> ArrangementView::bodyOf(const model::ClipInfo& clip, float y, float h) const
{
    const auto box = clipBox(clip.start, clip.end(), y, h);
    return box.withTrimmedTop(titleOf(box).getHeight());
}

void ArrangementView::paintFades(juce::Graphics& g, const model::ClipInfo& clip, juce::Rectangle<float> body, juce::Colour colour,
                                 bool selected)
{
    if (clip.kind != model::ClipInfo::Kind::Audio || body.getHeight() < 2.0f)
        return;
    const auto& t = ui::Theme::current();
    const auto* peaks = waveforms_.find(clip.sourcePath);
    auto draw = [&](double from, double to, const model::Ramp& ramp, bool fadingIn) {
        const float x0 = laneX(from), x1 = laneX(to);
        if (ramp.kind != model::Ramp::Kind::Fade || x1 - x0 < 1.0f)
            return;
        const auto region = juce::Rectangle<float>(x0, body.getY(), x1 - x0, body.getHeight()).getIntersection(body);
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(region.getSmallestIntegerContainer());
        // The waveform as the fade plays it (fades.md §3).
        if (peaks != nullptr)
        {
            g.setColour(t.clipBody(colour, selected));
            g.fillRect(region);
            WaveformTiles::drawDirect(g, *peaks, clip, body, region.getSmallestIntegerContainer(), timeline_, laneArea().getX(),
                                      t.clipContent(colour),
                                      [&](double seconds) { return model::rampGain(ramp, (seconds - from) / (to - from), fadingIn); });
        }
        // Narrower than a handle, the curve would read as a glitch: an edge fade shows once zoomed in.
        if (x1 - x0 < static_cast<float>(t.shell.fadeHandleSize))
            return;
        const auto curve = rampPath(x0, x1, body, [&](double a) { return model::rampGain(ramp, a, fadingIn); });
        // What the fade takes away is shaded: the area above its curve.
        juce::Path shade(curve);
        shade.lineTo(x1, body.getY());
        shade.lineTo(x0, body.getY());
        shade.closeSubPath();
        g.setColour(t.faded(t.surface0));
        g.fillPath(shade);
        g.setColour(t.textPrimary);
        g.strokePath(curve, juce::PathStrokeType(1.0f));
    };
    const auto& p = clip.playback;
    draw(clip.start, clip.start + p.in.length, p.in, true);
    draw(clip.end() - p.out.length, clip.end(), p.out, false);
}

void ArrangementView::paintCrossfades(juce::Graphics& g, const model::TrackInfo& track, float y, float h)
{
    const auto& t = ui::Theme::current();
    for (size_t i = 0; i + 1 < track.clips.size(); ++i)
    {
        const auto& c = track.clips[i];
        const auto& next = track.clips[i + 1];
        if (!c.crossfadeOut)
            continue;
        const auto& x = *c.crossfadeOut;
        const double join = c.end(), from = join + x.from;
        if (from + x.length < timeline_.start() || from > timeline_.end())
            continue;
        const auto body = bodyOf(c, y, h);
        const float x0 = laneX(from), xj = laneX(join), x1 = laneX(from + x.length);
        if (x1 - x0 < 1.0f)
            continue;
        const auto span = juce::Rectangle<float>(x0, body.getY(), x1 - x0, body.getHeight());
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(laneArea());
        // The waveform the crossfade plays, built from both clips' audio: on each side of the join
        // in that clip's colour (fades.md §3).
        const CrossfadeSums::Key key{.out = c.sourcePath,
                                     .in = next.sourcePath,
                                     .outFrom = c.offset + (from - c.start),
                                     .inFrom = next.offset + (from - next.start),
                                     .length = x.length,
                                     .shape = x.shape};
        if (const auto* sum = sums_.get(key))
        {
            const model::ClipInfo sumClip{.kind = model::ClipInfo::Kind::Audio, .start = from, .length = x.length};
            for (const auto* side : {&c, &next})
            {
                const auto part = side == &c ? span.withRight(xj) : span.withLeft(xj);
                if (part.getWidth() <= 0.0f)
                    continue;
                juce::Graphics::ScopedSaveState sideState(g);
                g.reduceClipRegion(part.getSmallestIntegerContainer());
                const auto colour = t.clipColour(side->colour);
                g.setColour(t.clipBody(colour, side->stack == 0 && selection_.clips.contains(side->id)));
                g.fillRect(part);
                WaveformTiles::drawDirect(g, *sum, sumClip, span, part.getSmallestIntegerContainer(), timeline_, laneArea().getX(),
                                          t.clipContent(colour));
            }
        }
        if (x1 - x0 < static_cast<float>(t.shell.fadeHandleSize)) // as a fade: its grips show it
            continue;
        g.setColour(t.textPrimary);
        g.strokePath(rampPath(x0, x1, span, [&](double a) { return model::crossfadeGains(a, x.shape).out; }), juce::PathStrokeType(1.0f));
        g.strokePath(rampPath(x0, x1, span, [&](double a) { return model::crossfadeGains(a, x.shape).in; }), juce::PathStrokeType(1.0f));
    }
}

bool ArrangementView::showsFadeHandles(const model::TrackInfo& track, const model::FadeHandle& handle) const
{
    auto shown = [&](std::uint64_t clip) { return hoveredClip_ == clip || selection_.clips.contains(clip); };
    if (shown(handle.clip))
        return true;
    // A crossfade's grips show for the clip after the join too.
    using K = model::FadeHandle::Kind;
    if (handle.kind != K::CrossStart && handle.kind != K::CrossEnd)
        return false;
    for (size_t i = 0; i + 1 < track.clips.size(); ++i)
        if (track.clips[i].id == handle.clip)
            return shown(track.clips[i + 1].id);
    return false;
}

std::optional<juce::Point<float>> ArrangementView::handlePoint(const model::TrackInfo& track, const model::FadeHandle& handle,
                                                                float y, float h) const
{
    const auto it = std::find_if(track.clips.begin(), track.clips.end(), [&](const auto& c) { return c.id == handle.clip; });
    if (it == track.clips.end())
        return std::nullopt;
    const auto body = bodyOf(*it, y, h);
    return juce::Point<float>(laneX(handle.time), body.getY() + body.getHeight() * handle.height);
}

void ArrangementView::paintFadeHandles(juce::Graphics& g, const model::TrackInfo& track, float y, float h)
{
    using K = model::FadeHandle::Kind;
    const auto& t = ui::Theme::current();
    const float size = static_cast<float>(t.shell.fadeHandleSize);
    for (const auto& handle : model::fadeHandles(track))
    {
        if (!showsFadeHandles(track, handle) && !(fadeDrag_ && fadeDrag_->handle.kind == handle.kind && fadeDrag_->handle.clip == handle.clip))
            continue;
        const auto at = handlePoint(track, handle, y, h);
        if (!at)
            continue;
        const bool dot = handle.kind == K::BendIn || handle.kind == K::BendOut;
        auto r = juce::Rectangle<float>(size, size).withCentre(*at);
        if (!dot) // a square hangs from the top of the body
            r = r.withY(at->y);
        g.setColour(t.textPrimary);
        if (dot)
            g.fillEllipse(r);
        else
            g.fillRect(r);
        g.setColour(t.surface0);
        if (dot)
            g.drawEllipse(r, 1.0f);
        else
            g.drawRect(r, 1.0f);
    }
}

std::optional<ArrangementView::FadeGrip> ArrangementView::fadeGripAt(juce::Point<float> pos) const
{
    const auto lanes = laneArea();
    const auto row = layout_.hit(pos.y - static_cast<float>(lanes.getY()));
    if (!row || !rows_[static_cast<size_t>(row->track)].holdsClips())
        return std::nullopt;
    const auto& track = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row->track)].track)];
    const float y = static_cast<float>(lanes.getY() + layout_.top(row->track));
    const float h = static_cast<float>(layout_.baseHeight(row->track));
    const auto& t = ui::Theme::current();
    const float grab = t.input.edgeGrab, size = static_cast<float>(t.shell.fadeHandleSize);
    using K = model::FadeHandle::Kind;
    std::optional<FadeGrip> best;
    float bestDistance = 0.0f;
    for (const auto& handle : model::fadeHandles(track))
    {
        if (!showsFadeHandles(track, handle))
            continue;
        auto at = handlePoint(track, handle, y, h);
        if (!at)
            continue;
        if (handle.kind != K::BendIn && handle.kind != K::BendOut)
            at->y += size * 0.5f; // the square hangs from the top of the body
        const float d = at->getDistanceFrom(pos);
        // A short crossfade's two ends sit on one spot: the one on the pointer's side of it.
        const bool tie = best && std::abs(d - bestDistance) < 1.0f && best->handle.clip == handle.clip &&
                         best->handle.kind == K::CrossStart && handle.kind == K::CrossEnd;
        if (d <= grab && (!best || d < bestDistance || (tie && pos.x >= at->x)))
        {
            best = FadeGrip{handle, track.id};
            bestDistance = d;
        }
    }
    return best;
}

std::optional<ArrangementView::FadeRegion> ArrangementView::fadeRegionAt(juce::Point<float> pos) const
{
    const auto lanes = laneArea();
    const auto row = layout_.hit(pos.y - static_cast<float>(lanes.getY()));
    if (!row || !rows_[static_cast<size_t>(row->track)].holdsClips())
        return std::nullopt;
    const auto& track = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row->track)].track)];
    const float y = static_cast<float>(lanes.getY() + layout_.top(row->track));
    const float h = static_cast<float>(layout_.baseHeight(row->track));
    const double at = timeline_.timeForX(pos.x - static_cast<float>(lanes.getX()));
    for (const auto& c : track.clips)
    {
        if (c.kind != model::ClipInfo::Kind::Audio)
            continue;
        const auto body = bodyOf(c, y, h);
        if (pos.y < body.getY() || pos.y > body.getBottom())
            continue;
        if (c.crossfadeOut && at >= c.end() + c.crossfadeOut->from && at <= c.end() + c.crossfadeOut->from + c.crossfadeOut->length)
            return FadeRegion{FadeRegion::Kind::Cross, c.id, track.id};
        if (c.fadeIn.length > 0.0 && at >= c.start && at <= c.start + c.fadeIn.length)
            return FadeRegion{FadeRegion::Kind::In, c.id, track.id};
        if (c.fadeOut.length > 0.0 && at >= c.end() - c.fadeOut.length && at <= c.end())
            return FadeRegion{FadeRegion::Kind::Out, c.id, track.id};
    }
    return std::nullopt;
}

void ArrangementView::fadeMenu(const FadeRegion& region)
{
    const auto* track = snapshot_->channel(region.track);
    if (track == nullptr)
        return;
    const auto it = std::find_if(track->clips.begin(), track->clips.end(), [&](const auto& c) { return c.id == region.clip; });
    if (it == track->clips.end())
        return;
    const auto clip = region.clip;
    juce::PopupMenu menu;
    if (region.kind == FadeRegion::Kind::Cross)
    {
        const auto x = *it->crossfadeOut;
        // Each shape with the one line that says when to use it (fades.md §4).
        const std::array<std::pair<model::CrossfadeShape, const char*>, 3> shapes{
            std::pair{model::CrossfadeShape::EqualPower, "Equal power: different material"},
            std::pair{model::CrossfadeShape::EqualGain, "Equal gain: the same material"},
            std::pair{model::CrossfadeShape::SCurve, "S-curve"}};
        for (const auto& option : shapes)
            menu.addItem(option.second, true, x.shape == option.first, [this, clip, x, chosen = option.first] {
                session_.setCrossfade(clip, model::Crossfade{x.length, x.from, chosen}, "Crossfade shape");
            });
        menu.addSeparator();
        // Where it sits against the join, where the spare audio allows it whole.
        const auto clips = model::fadeClips(*track);
        const auto at = std::find_if(clips.begin(), clips.end(), [&](const auto& f) { return f.id == clip; });
        const std::array<std::pair<model::CrossfadePlacement, const char*>, 3> placements{
            std::pair{model::CrossfadePlacement::Before, "Before the join"},
            std::pair{model::CrossfadePlacement::Centred, "Centred on the join"},
            std::pair{model::CrossfadePlacement::After, "After the join"}};
        for (const auto& option : placements)
        {
            const auto placed = at != clips.end() && at + 1 != clips.end()
                                    ? model::placeCrossfadeAt(*at, *(at + 1), x.length, option.first, x.shape)
                                    : std::nullopt;
            menu.addItem(option.second, placed.has_value(), model::placementOf(x) == option.first, [this, clip, placed] {
                if (placed)
                    session_.setCrossfade(clip, placed, "Crossfade placement");
            });
        }
        menu.addSeparator();
        menu.addItem("Remove crossfade", [this, clip] { session_.setCrossfade(clip, std::nullopt, "Remove crossfade"); });
    }
    else
    {
        const bool in = region.kind == FadeRegion::Kind::In;
        const auto fade = in ? it->fadeIn : it->fadeOut;
        auto set = [this, clip, in](model::Fade f) {
            session_.setFades({clip}, in ? std::optional(f) : std::nullopt, in ? std::nullopt : std::optional(f));
        };
        menu.addItem("Straighten", fade.bend != 0.0f, false, [set, fade] { set({fade.length, 0.0f}); });
        menu.addItem(in ? "Remove fade-in" : "Remove fade-out", [set] { set({0.0, 0.0f}); });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
}

bool ArrangementView::fadeMouseDown(const juce::MouseEvent& e)
{
    using K = model::FadeHandle::Kind;
    auto grip = fadeGripAt(e.position);
    // Alt + drag on a fade bends it, as on an automation segment (fades.md §3).
    if (!grip && e.mods.isAltDown())
        if (const auto region = fadeRegionAt(e.position); region && region->kind != FadeRegion::Kind::Cross)
            grip = FadeGrip{{.kind = region->kind == FadeRegion::Kind::In ? K::BendIn : K::BendOut, .clip = region->clip}, region->track};
    if (!grip)
        return false;
    const auto* track = snapshot_->channel(grip->track);
    if (track == nullptr)
        return false;
    const auto& h = grip->handle;
    const auto it = std::find_if(track->clips.begin(), track->clips.end(), [&](const auto& c) { return c.id == h.clip; });
    if (it == track->clips.end())
        return false;
    if (e.getNumberOfClicks() >= 2)
    {
        // A double-click removes a fade or a crossfade; on a bend, straightens it.
        switch (h.kind)
        {
        case K::FadeIn: session_.setFades({h.clip}, model::Fade{}, std::nullopt); break;
        case K::FadeOut: session_.setFades({h.clip}, std::nullopt, model::Fade{}); break;
        case K::BendIn: session_.setFades({h.clip}, model::Fade{it->fadeIn.length, 0.0f}, std::nullopt); break;
        case K::BendOut: session_.setFades({h.clip}, std::nullopt, model::Fade{it->fadeOut.length, 0.0f}); break;
        case K::CrossStart:
        case K::CrossEnd: session_.setCrossfade(h.clip, std::nullopt, "Remove crossfade"); break;
        }
        return true;
    }
    drag_ = Drag::Fade;
    fadeDrag_ = FadeDrag{.handle = h,
                         .track = grip->track,
                         .baseBend = h.kind == K::BendIn ? it->fadeIn.bend : it->fadeOut.bend,
                         .pointer = e.position};
    return true;
}

void ArrangementView::fadeMouseDrag(const juce::MouseEvent& e)
{
    using K = model::FadeHandle::Kind;
    if (!fadeDrag_)
        return;
    const auto* track = snapshot_->channel(fadeDrag_->track);
    if (track == nullptr)
        return;
    const auto& h = fadeDrag_->handle;
    model::FadeEdit edit;
    if (h.kind == K::BendIn || h.kind == K::BendOut)
    {
        const float up = dragStart_.y - e.position.y;
        edit = model::bendFade(*track, h, model::bendFor(fadeDrag_->baseBend, up, ui::Theme::current().input.bendTravel, h.kind == K::BendIn));
    }
    else
        edit = model::dragFade(*track, h, timeline_.timeForX(e.position.x - static_cast<float>(laneArea().getX())));
    fadeDrag_->edit = edit;
    fadeDrag_->pointer = e.position;
    fadePreview_ = model::withFadeEdit(*track, edit, snapshot_->edgeFades);
    repaint(laneArea());
}

juce::String ArrangementView::fadeDragText() const
{
    if (!fadeDrag_ || !fadeDrag_->edit)
        return {};
    using K = model::FadeHandle::Kind;
    const auto& e = *fadeDrag_->edit;
    auto ms = [](double seconds) { return juce::String(juce::roundToInt(seconds * 1000.0)) + " ms"; };
    auto percent = [](float bend) { return juce::String(juce::roundToInt(bend * 100.0f)) + " %"; };
    if (e.crossClip != 0)
        return e.crossfade ? "Crossfade " + ms(e.crossfade->length) : juce::String("No crossfade");
    const auto kind = fadeDrag_->handle.kind;
    if (kind == K::BendIn && e.in)
        return "Bend " + percent(e.in->bend);
    if (kind == K::BendOut && e.out)
        return "Bend " + percent(e.out->bend);
    if (e.in)
        return "Fade in " + ms(e.in->length);
    if (e.out)
        return "Fade out " + ms(e.out->length);
    return {};
}

void ArrangementView::fadeMouseUp()
{
    if (fadeDrag_ && fadeDrag_->edit && !fadeDrag_->edit->empty())
    {
        const auto& edit = *fadeDrag_->edit;
        if (edit.crossClip != 0)
            session_.setCrossfade(edit.crossClip, edit.crossfade, edit.crossfade ? "Crossfade" : "Remove crossfade");
        else
            session_.setFades({edit.clip}, edit.in, edit.out);
    }
    // An edit's preview stays until the snapshot has it (no flicker), as clip drags do.
    if (!fadeDrag_ || !fadeDrag_->edit || fadeDrag_->edit->empty())
        fadePreview_.reset();
    fadeDrag_.reset();
    repaint(laneArea());
}
} // namespace fd::app
