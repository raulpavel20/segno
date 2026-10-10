// Fades and crossfades (M2 step C6, ADR 0036, docs/design/fades.md): fades on audio clips, a
// crossfade stored on the earlier clip of a join, and the project's edge fades.

#include "engine/ClipFades.h"
#include "engine/SessionImpl.h"

#include "model/StackEdits.h"

namespace fd::engine
{
namespace
{
/// The audio clips of a track in time order, as fades see them, with the tracktion clips.
struct FadeTrack
{
    std::vector<model::FadeClip> clips;
    std::vector<te::AudioClipBase*> engine;
};

FadeTrack fadeTrack(te::AudioTrack& track)
{
    std::vector<te::AudioClipBase*> audio;
    for (auto* c : track.getClips())
        if (auto* a = dynamic_cast<te::AudioClipBase*>(c))
            audio.push_back(a);
    std::sort(audio.begin(), audio.end(),
              [](auto* a, auto* b) { return a->getPosition().getStart() < b->getPosition().getStart(); });
    FadeTrack out;
    for (auto* a : audio)
        out.clips.push_back(fadeClipOf(*a));
    out.engine = std::move(audio);
    return out;
}

void writeCrossfade(juce::ValueTree& state, const std::optional<model::Crossfade>& x, juce::UndoManager* um)
{
    if (!x)
    {
        for (const auto& id : {fadeIds::crossLength, fadeIds::crossFrom, fadeIds::crossShape})
            state.removeProperty(id, um);
        return;
    }
    state.setProperty(fadeIds::crossLength, x->length, um);
    state.setProperty(fadeIds::crossFrom, x->from, um);
    state.setProperty(fadeIds::crossShape, static_cast<int>(x->shape), um);
}
} // namespace

void Session::Impl::settleFades(te::AudioTrack& track, juce::UndoManager* um)
{
    auto t = fadeTrack(track);
    for (size_t i = 0; i < t.clips.size(); ++i)
    {
        auto& state = t.engine[i]->state;
        const auto& c = t.clips[i];
        // A crossfade lives only while its clips meet, and never at a seamless join.
        if (c.crossOut && !(i + 1 < t.clips.size() && model::meet(c, t.clips[i + 1]) && !model::seamless(c, t.clips[i + 1])))
            writeCrossfade(state, std::nullopt, um);
        if (c.in.length <= 0.0 && state.hasProperty(fadeIds::inBend))
            state.removeProperty(fadeIds::inBend, um);
        if (c.out.length <= 0.0 && state.hasProperty(fadeIds::outBend))
            state.removeProperty(fadeIds::outBend, um);
    }
}

void Session::Impl::crossfadeJoinsAt(te::AudioTrack& track, const std::set<std::uint64_t>& parts, const std::vector<double>& times,
                                     double length, juce::UndoManager* um)
{
    auto t = fadeTrack(track);
    for (size_t i = 0; i + 1 < t.clips.size(); ++i)
    {
        const auto& a = t.clips[i];
        const auto& b = t.clips[i + 1];
        if (!parts.contains(a.id) || !parts.contains(b.id) || !model::meet(a, b) ||
            std::none_of(times.begin(), times.end(), [&](double at) { return std::abs(at - a.end()) < 1e-6; }))
            continue;
        const auto x = model::seamless(a, b) ? std::nullopt : model::placeCrossfadeOn(t.clips, i, length);
        writeCrossfade(t.engine[i]->state, x, um);
        t.clips[i].crossOut = x; // the next join makes room for it
    }
}

void Session::Impl::fillFades(te::AudioTrack& track, std::vector<model::ClipInfo>& clips) const
{
    const auto t = fadeTrack(track);
    const bool edges = edgeFadesOn(*edit);
    for (auto& info : clips)
    {
        if (info.kind != model::ClipInfo::Kind::Audio)
            continue;
        for (size_t i = 0; i < t.clips.size(); ++i)
            if (t.clips[i].id == info.id)
            {
                info.fadeIn = t.clips[i].in;
                info.fadeOut = t.clips[i].out;
                info.crossfadeOut = model::crossfadeOut(t.clips, i);
                info.playback = model::clipPlayback(t.clips, i, edges);
            }
    }
}

// ---- Session -------------------------------------------------------------------------------

void Session::setFades(const std::vector<std::uint64_t>& asked, std::optional<model::Fade> in, std::optional<model::Fade> out)
{
    // A clip with takes fades at its outer edges: its first part's start, its last part's end.
    std::vector<model::ClipPlace> places;
    std::set<te::AudioTrack*> tracks;
    for (auto id : asked)
        if (auto* c = impl_->clip(id))
            if (auto* t = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                tracks.insert(t);
    for (auto* t : tracks)
    {
        const auto p = impl_->places(*t);
        places.insert(places.end(), p.begin(), p.end());
    }
    const auto ids = model::withParts(places, asked);
    std::map<std::uint64_t, std::pair<te::AudioClipBase*, te::AudioClipBase*>> groups; // stack or clip -> first, last
    for (auto id : ids)
    {
        auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(id));
        if (c == nullptr)
            continue;
        std::uint64_t key = id;
        for (const auto& p : places)
            if (p.clip == id && p.stack != 0)
                key = p.stack;
        auto [it, fresh] = groups.try_emplace(key, c, c);
        if (!fresh)
        {
            if (c->getPosition().getStart() < it->second.first->getPosition().getStart())
                it->second.first = c;
            if (c->getPosition().getEnd() > it->second.second->getPosition().getEnd())
                it->second.second = c;
        }
    }
    if (groups.empty())
        return;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit("Fades");
    for (auto& [key, ends] : groups)
    {
        if (in)
        {
            ends.first->setFadeIn(te::TimeDuration::fromSeconds(std::max(0.0, in->length)));
            ends.first->state.setProperty(fadeIds::inBend, std::clamp(in->bend, -1.0f, 1.0f), um);
        }
        if (out)
        {
            ends.second->setFadeOut(te::TimeDuration::fromSeconds(std::max(0.0, out->length)));
            ends.second->state.setProperty(fadeIds::outBend, std::clamp(out->bend, -1.0f, 1.0f), um);
        }
    }
    for (auto* t : tracks)
        impl_->settleFades(*t, um);
}

std::vector<Session::CrossfadeResult> Session::crossfade(const std::vector<std::uint64_t>& clips, double length,
                                                         std::optional<model::CrossfadeShape> shape)
{
    std::vector<CrossfadeResult> out;
    std::set<te::AudioTrack*> tracks;
    for (auto id : clips)
        if (auto* c = impl_->clip(id))
            if (auto* t = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                tracks.insert(t);
    const std::set<std::uint64_t> chosen(clips.begin(), clips.end());
    auto* um = &impl_->edit->getUndoManager();
    bool begun = false;
    for (auto* track : tracks)
    {
        auto t = fadeTrack(*track);
        for (size_t i = 0; i + 1 < t.clips.size(); ++i)
        {
            const auto& a = t.clips[i];
            const auto& b = t.clips[i + 1];
            if (!chosen.contains(a.id) || !chosen.contains(b.id) || !model::meet(a, b))
                continue;
            CrossfadeResult r{.out = a.id, .in = b.id};
            if (model::seamless(a, b))
                r.why = "These clips play on seamlessly: there's nothing to crossfade.";
            else
            {
                const auto existing = model::crossfadeOut(t.clips, i);
                r.crossfade = existing ? std::optional(model::Crossfade{existing->length, existing->from, shape.value_or(existing->shape)})
                                       : model::placeCrossfadeOn(t.clips, i, length, std::nullopt,
                                                                 shape.value_or(model::CrossfadeShape::EqualPower));
                if (!r.crossfade)
                    r.why = "No audio beyond these clips' edges to crossfade: trim them shorter first.";
                else
                {
                    if (!begun)
                    {
                        impl_->beginEdit("Crossfade");
                        begun = true;
                    }
                    writeCrossfade(t.engine[i]->state, r.crossfade, um);
                    t.clips[i].crossOut = r.crossfade; // the next join makes room for it
                }
            }
            out.push_back(std::move(r));
        }
    }
    return out;
}

bool Session::setCrossfade(std::uint64_t clipId, std::optional<model::Crossfade> crossfade, const std::string& undoName)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    auto* track = c != nullptr ? dynamic_cast<te::AudioTrack*>(c->getClipTrack()) : nullptr;
    if (track == nullptr)
        return false;
    auto t = fadeTrack(*track);
    const auto it = std::find_if(t.clips.begin(), t.clips.end(), [&](const auto& f) { return f.id == clipId; });
    const auto i = static_cast<size_t>(it - t.clips.begin());
    if (it == t.clips.end())
        return false;
    auto* um = &impl_->edit->getUndoManager();
    if (!crossfade)
    {
        if (!it->crossOut)
            return false;
        impl_->beginEdit(juce::String(undoName));
        writeCrossfade(t.engine[i]->state, std::nullopt, um);
        return true;
    }
    if (i + 1 >= t.clips.size() || model::seamless(*it, t.clips[i + 1]))
        return false;
    const auto fitted = model::placeCrossfadeOn(t.clips, i, crossfade->length, crossfade->from, crossfade->shape);
    if (!fitted)
        return false;
    impl_->beginEdit(juce::String(undoName));
    writeCrossfade(t.engine[i]->state, fitted, um);
    return true;
}

void Session::setEdgeFades(bool on)
{
    if (edgeFades() == on)
        return;
    impl_->beginEdit("Edge fades");
    impl_->edit->state.setProperty(fadeIds::edgeFades, on, &impl_->edit->getUndoManager());
}

bool Session::edgeFades() const
{
    return edgeFadesOn(*impl_->edit);
}
} // namespace fd::engine
