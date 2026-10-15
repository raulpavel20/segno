#include "engine/ClipFades.h"
#include "engine/SessionImpl.h"

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
    c.loop = loopOf(clip.state, clip.edit.tempoSequence); // ADR 0048
    // A looped clip plays past its edges too: its neighbouring laps (the engine wraps them).
    c.canExtend = !clip.getAutoTempo() && !clip.getAutoPitch() && std::abs(clip.getSpeedRatio() - 1.0) < 1e-9 &&
                  !clip.usesTimeStretchedProxy();
    c.reversed = clip.state.hasProperty(soundIds::forward);
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

model::AudioSound soundOf(const te::AudioClipBase& clip)
{
    const auto& s = clip.state;
    model::AudioSound sound;
    sound.gainDb = std::clamp(static_cast<float>(s.getProperty(soundIds::gain, 0.0f)), model::minusInfinityDb,
                              model::kMaxClipGainDb);
    sound.reversed = s.hasProperty(soundIds::forward);
    sound.inverted = s.getProperty(soundIds::inverted, false);
    sound.channels = static_cast<model::ChannelMode>(
        std::clamp(static_cast<int>(s.getProperty(soundIds::channels, 0)), 0, 4));
    for (const auto& p : s.getChildWithName(soundIds::line))
        if (p.hasType(soundIds::point))
            sound.line.push_back({static_cast<double>(p[soundIds::time]),
                                  static_cast<float>(p[soundIds::value]),
                                  static_cast<float>(p[soundIds::curve])});
    std::stable_sort(sound.line.begin(), sound.line.end(),
                     [](const auto& a, const auto& b) { return a.time < b.time; });
    return sound;
}

void writeGainLine(juce::ValueTree& clip, const model::AutomationPoints& line, juce::UndoManager* um)
{
    if (auto old = clip.getChildWithName(soundIds::line); old.isValid())
        clip.removeChild(old, um);
    if (line.empty())
        return;
    juce::ValueTree tree(soundIds::line);
    for (const auto& p : line)
        tree.appendChild(juce::ValueTree(soundIds::point, {{soundIds::time, p.time},
                                                           {soundIds::value, std::clamp(p.value, 0.0f, 1.0f)},
                                                           {soundIds::curve, p.curve}}),
                         nullptr);
    clip.appendChild(tree, um);
}

bool affectsSound(const juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree.hasType(te::IDs::AUDIOCLIP))
        return id == soundIds::gain || id == soundIds::inverted || id == soundIds::channels ||
               id == soundIds::forward;
    return tree.hasType(soundIds::point) || tree.hasType(soundIds::line);
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

ClipFadeNode::ClipFadeNode(std::unique_ptr<te::graph::Node> input, te::ProcessState& state,
                           model::FadeClip clip, model::ClipPlayback playback, model::AudioSound sound)
    : te::TracktionEngineNode(state), input_(std::move(input)), clip_(std::move(clip)), playback_(playback),
      sound_(std::move(sound)), gain_(model::soundGainAt({.gainDb = sound_.gainDb}, 0.0)),
      neutral_(sound_.neutral())
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
    // Wholly inside what the clip plays, clear of its ramps, and its sound as recorded: as it is.
    const double from = range.getStart().inSeconds(), to = range.getEnd().inSeconds();
    const double start = clip_.start - playback_.extendStart, end = clip_.end() + playback_.extendEnd;
    const bool inRamp = playback_.in.kind != model::Ramp::Kind::None && from < start + playback_.in.length;
    const bool outRamp = playback_.out.kind != model::Ramp::Kind::None && to > end - playback_.out.length;
    const bool seamRamp = model::seamRampBetween(playback_.seams, from, to);
    if (from >= start && to <= end && !inRamp && !outRamp && !seamRamp && neutral_)
    {
        setAudioOutput(input_.get(), source.audio);
        return;
    }

    te::graph::copyIfNotAliased(dest, source.audio);
    const auto frames = dest.getNumFrames(), channels = dest.getNumChannels();
    const double step = (to - from) / static_cast<double>(frames);
    const bool line = !sound_.line.empty();
    const float sign = sound_.inverted ? -1.0f : 1.0f;
    const bool pair = channels >= 2 && sound_.channels != model::ChannelMode::Stereo;
    for (choc::buffer::FrameCount i = 0; i < frames; ++i)
    {
        const double t = from + step * static_cast<double>(i);
        float gain = model::playbackGain(clip_, playback_, t) * gain_ * sign;
        if (line)
        {
            const double content = model::contentAt(clip_.start, clip_.offset, clip_.loop, t);
            gain *= model::lineGainAt(sound_.line,
                                      model::fileTimeOf(content, clip_.sourceLength, clip_.reversed));
        }
        if (pair)
            model::applyChannels(sound_.channels, dest.getSample(0, i), dest.getSample(1, i));
        if (gain == 1.0f)
            continue;
        for (choc::buffer::ChannelCount c = 0; c < channels; ++c)
            dest.getSample(c, i) *= gain;
    }
}
} // namespace fd::engine
