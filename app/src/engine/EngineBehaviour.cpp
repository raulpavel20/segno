#include "engine/EngineBehaviour.h"

#include "engine/ClipChance.h"
#include "engine/ClipFades.h"
#include "engine/MonitoringLatency.h"
#include "engine/WarpPlayer.h"

#include <map>

namespace fd::engine
{
namespace
{
class Behaviour final : public tracktion::EngineBehaviour
{
public:
    explicit Behaviour(EngineOptions options) : options_(options) {}

    bool canScanPluginsOutOfProcess() override { return options_.scanPluginsOutOfProcess; }

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

    // Chance (piano-roll.md §10, §26, ADR 0062): a MIDI clip's notes left to chance are decided as
    // they play, again each pass and each lap of the loop, or as kept.
    std::shared_ptr<tracktion::MidiPlaybackFilter>
    createMidiPlaybackFilter(tracktion::MidiClip& clip, const juce::MidiMessageSequence& sequence,
                             tracktion::MidiList::TimeBase timeBase) override
    {
        return chanceFilter(clip, sequence, timeBase);
    }

    // Reduced latency when monitoring (recording.md §22.1, the reduced-monitoring-latency patch).
    bool isMonitoredWithReducedLatency(tracktion::AudioTrack& track) override
    {
        return engine::isMonitoredWithReducedLatency(track);
    }
    bool shouldBypassWhileMonitoring(tracktion::Plugin& module) override
    {
        const bool bypass = engine::shouldBypassWhileMonitoring(module);
        leftOut[&module] = bypass; // what the graph does, for its header
        return bypass;
    }

    RecordingFiles files;
    TransientSource transients;
    /// What the graph last decided for each module it asked about (every build asks again about
    /// every module on a monitored track, so a stale entry is never read for one).
    std::map<const tracktion::Plugin*, bool> leftOut;

private:
    EngineOptions options_;
};
} // namespace

std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour(EngineOptions options)
{
    return std::make_unique<Behaviour>(options);
}

bool leftOutWhileMonitoring(tracktion::Plugin& module)
{
    if (auto* behaviour = dynamic_cast<Behaviour*>(&module.engine.getEngineBehaviour()))
        if (const auto it = behaviour->leftOut.find(&module); it != behaviour->leftOut.end())
            return it->second;
    return false;
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
