#include "engine/EngineBehaviour.h"

#include "engine/ClipFades.h"

namespace fd::engine
{
namespace
{
class Behaviour final : public tracktion::EngineBehaviour
{
public:
    bool shouldProcessAuxSendWhenTrackIsMuted(tracktion::AuxSendPlugin& send) override
    {
        return send.edit.state.getProperty(sendsWhileMutedId, false);
    }

    juce::File getFileForNewAudioRecording(tracktion::Track& track, const juce::String& extension) override
    {
        return files ? files(track, extension) : juce::File();
    }

    // Fades and crossfades (ADR 0036): a clip plays its crossfades' spare audio past its edges,
    // through Segno's fade node.
    tracktion::AudioClipExtension getAudioClipExtension(tracktion::AudioClipBase& clip) override
    {
        const auto p = playbackOf(clip);
        return {tracktion::TimeDuration::fromSeconds(p.extendStart), tracktion::TimeDuration::fromSeconds(p.extendEnd)};
    }

    std::unique_ptr<tracktion::graph::Node> createAudioClipFadeNode(tracktion::AudioClipBase& clip, tracktion::TimeRange,
                                                                    std::unique_ptr<tracktion::graph::Node>& input,
                                                                    tracktion::ProcessState& state) override
    {
        auto [clips, index] = fadeNeighboursOf(clip);
        const auto playback = model::clipPlayback(clips, index, edgeFadesOn(clip.edit));
        // Nothing to fade: no node, unless tracktion would apply its own curves to tiny fades.
        if (!playback.any() && clip.getFadeIn() == tracktion::TimeDuration() && clip.getFadeOut() == tracktion::TimeDuration())
            return {};
        return std::make_unique<ClipFadeNode>(std::move(input), state, clips[index], playback);
    }

    RecordingFiles files;
};
} // namespace

std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour()
{
    return std::make_unique<Behaviour>();
}

void setRecordingFiles(tracktion::Engine& engine, RecordingFiles files)
{
    if (auto* behaviour = dynamic_cast<Behaviour*>(&engine.getEngineBehaviour()))
        behaviour->files = std::move(files);
}
} // namespace fd::engine
