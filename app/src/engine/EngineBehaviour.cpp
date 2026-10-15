#include "engine/EngineBehaviour.h"

#include "engine/ClipFades.h"
#include "engine/ClipNotes.h"
#include "engine/WarpPlayer.h"

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
        auto sound = soundOf(clip);
        // Nothing to fade and its sound as recorded: no node, unless tracktion would apply its own
        // curves to tiny fades.
        if (!playback.any() && sound.neutral() && clip.getFadeIn() == tracktion::TimeDuration() &&
            clip.getFadeOut() == tracktion::TimeDuration())
            return {};
        return std::make_unique<ClipFadeNode>(std::move(input), state, clips[index], playback,
                                              std::move(sound));
    }

    // Warping (ADR 0063, audio-editor.md §22): a clip that follows the tempo plays through Segno's
    // piece player.
    std::unique_ptr<tracktion::graph::Node> createAudioClipSourceNode(tracktion::AudioClipBase& clip,
                                                                      tracktion::TimeRange played,
                                                                      tracktion::ProcessState& state,
                                                                      bool forRendering) override
    {
        if (!clip.getAutoTempo())
            return {};
        std::vector<double> found;
        if (transients && warpOf(clip).mode == model::WarpMode::Beats)
            found = transients(clip);
        return makeWarpPlayer(clip, played, state, forRendering, found);
    }

    // Chance (piano-roll.md §10): every render of a MIDI clip decides its notes the same way.
    juce::MidiMessageSequence createPlaybackMidiSequence(const tracktion::MidiList& list,
                                                         tracktion::MidiClip& clip,
                                                         tracktion::MidiList::TimeBase timeBase,
                                                         bool generateMPE) override
    {
        return playbackSequence(list, clip, timeBase, generateMPE);
    }

    RecordingFiles files;
    TransientSource transients;
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

void setTransientSource(tracktion::Engine& engine, TransientSource source)
{
    if (auto* behaviour = dynamic_cast<Behaviour*>(&engine.getEngineBehaviour()))
        behaviour->transients = std::move(source);
}
} // namespace fd::engine
