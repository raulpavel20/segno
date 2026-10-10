#include "engine/ClipFades.h"

#include <cmath>

namespace fd::engine
{
namespace te = tracktion;

model::FadeClip fadeClipOf(te::AudioClipBase& clip)
{
    model::FadeClip c;
    c.id = clip.itemID.getRawID();
    const auto pos = clip.getPosition();
    c.start = pos.getStart().inSeconds();
    c.length = pos.getLength().inSeconds();
    c.offset = pos.getOffset().inSeconds();
    c.sourceLength = clip.getSourceLength().inSeconds();
    c.source = clip.getOriginalFile().getFullPathName().toStdString();
    c.muted = clip.isMuted();
    c.canExtend = !clip.getAutoTempo() && !clip.getAutoPitch() && !clip.isLooping() &&
                  std::abs(clip.getSpeedRatio() - 1.0) < 1e-9 && !clip.usesTimeStretchedProxy();
    const auto& s = clip.state;
    c.in = {clip.getFadeIn().inSeconds(), static_cast<float>(s.getProperty(fadeIds::inBend, 0.0f))};
    c.out = {clip.getFadeOut().inSeconds(), static_cast<float>(s.getProperty(fadeIds::outBend, 0.0f))};
    if (const double length = s.getProperty(fadeIds::crossLength, 0.0); length > 0.0)
        c.crossOut = model::Crossfade{.length = length,
                                      .from = s.getProperty(fadeIds::crossFrom, -length * 0.5),
                                      .shape = static_cast<model::CrossfadeShape>(static_cast<int>(s.getProperty(fadeIds::crossShape, 0)))};
    return c;
}

std::pair<std::vector<model::FadeClip>, size_t> fadeNeighboursOf(te::AudioClipBase& clip)
{
    te::AudioClipBase *prev = nullptr, *next = nullptr;
    const auto at = clip.getPosition().getStart();
    if (auto* track = clip.getClipTrack())
        for (auto* c : track->getClips())
            if (auto* audio = dynamic_cast<te::AudioClipBase*>(c); audio != nullptr && audio != &clip)
            {
                const auto start = audio->getPosition().getStart();
                if (start < at && (prev == nullptr || start > prev->getPosition().getStart()))
                    prev = audio;
                else if (start > at && (next == nullptr || start < next->getPosition().getStart()))
                    next = audio;
            }
    std::vector<model::FadeClip> clips;
    if (prev != nullptr)
        clips.push_back(fadeClipOf(*prev));
    clips.push_back(fadeClipOf(clip));
    if (next != nullptr)
        clips.push_back(fadeClipOf(*next));
    return {std::move(clips), prev != nullptr ? 1u : 0u};
}

bool edgeFadesOn(const te::Edit& edit)
{
    return edit.state.getProperty(fadeIds::edgeFades, true);
}

model::ClipPlayback playbackOf(te::AudioClipBase& clip)
{
    const auto [clips, index] = fadeNeighboursOf(clip);
    return model::clipPlayback(clips, index, edgeFadesOn(clip.edit));
}

// ---- the node ------------------------------------------------------------------------------

ClipFadeNode::ClipFadeNode(std::unique_ptr<te::graph::Node> input, te::ProcessState& state, model::FadeClip clip,
                           model::ClipPlayback playback)
    : te::TracktionEngineNode(state), input_(std::move(input)), clip_(std::move(clip)), playback_(playback)
{
    setOptimisations({te::graph::ClearBuffers::no, te::graph::AllocateAudioBuffer::yes});
}

te::graph::NodeProperties ClipFadeNode::getNodeProperties()
{
    auto props = input_->getNodeProperties();
    constexpr size_t magicHash = size_t(0x5345474e4f464144); // "SEGNOFAD"
    if (props.nodeID != 0)
        tracktion::hash_combine(props.nodeID, magicHash);
    return props;
}

std::vector<te::graph::Node*> ClipFadeNode::getDirectInputNodes()
{
    return {input_.get()};
}

bool ClipFadeNode::isReadyToProcess()
{
    return input_->hasProcessed();
}

void ClipFadeNode::process(ProcessContext& pc)
{
    auto source = input_->getProcessedOutput();
    auto dest = pc.buffers.audio;
    pc.buffers.midi.copyFrom(source.midi);

    const auto range = getEditTimeRange();
    if (!getPlayHead().isPlaying() || range.isEmpty())
    {
        setAudioOutput(input_.get(), source.audio);
        return;
    }
    // Wholly inside what the clip plays and clear of its ramps: as it is.
    const double from = range.getStart().inSeconds(), to = range.getEnd().inSeconds();
    const double start = clip_.start - playback_.extendStart, end = clip_.end() + playback_.extendEnd;
    const bool inRamp = playback_.in.kind != model::Ramp::Kind::None && from < start + playback_.in.length;
    const bool outRamp = playback_.out.kind != model::Ramp::Kind::None && to > end - playback_.out.length;
    if (from >= start && to <= end && !inRamp && !outRamp)
    {
        setAudioOutput(input_.get(), source.audio);
        return;
    }

    te::graph::copyIfNotAliased(dest, source.audio);
    const auto frames = dest.getNumFrames(), channels = dest.getNumChannels();
    const double step = (to - from) / static_cast<double>(frames);
    for (choc::buffer::FrameCount i = 0; i < frames; ++i)
    {
        const float gain = model::playbackGain(clip_, playback_, from + step * static_cast<double>(i));
        if (gain == 1.0f)
            continue;
        for (choc::buffer::ChannelCount c = 0; c < channels; ++c)
            dest.getSample(c, i) *= gain;
    }
}
} // namespace fd::engine
