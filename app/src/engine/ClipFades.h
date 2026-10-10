#pragma once

#include "model/Fades.h"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>

namespace fd::engine
{
// Clip fades and crossfades in the engine (M2 step C6, ADR 0036, docs/design/fades.md): Segno's
// state on a clip, the clip as model::Fades sees it, and the node that plays its ramps.

/// Segno's fade state on an audio clip (tracktion's own fadeIn and fadeOut hold the lengths).
namespace fadeIds
{
inline const juce::Identifier inBend{"fdFadeInBend"}, outBend{"fdFadeOutBend"};
/// The crossfade at its end, into the clip that starts there (model::Crossfade).
inline const juce::Identifier crossLength{"fdXLength"}, crossFrom{"fdXFrom"}, crossShape{"fdXShape"};
/// On the edit: the project's edge fades (fades.md §2); absent means on.
inline const juce::Identifier edgeFades{"fdEdgeFades"};
} // namespace fadeIds

/// The clip as fades see it. Clips that play stretched, synced or looped have no spare audio to
/// crossfade with.
[[nodiscard]] model::FadeClip fadeClipOf(tracktion::AudioClipBase& clip);
/// The clip with the audio clips just before and after it on its track (what its playback depends
/// on), and its place among them: cheap, for the engine's graph building.
[[nodiscard]] std::pair<std::vector<model::FadeClip>, size_t> fadeNeighboursOf(tracktion::AudioClipBase& clip);
/// What the clip plays (model::clipPlayback with the project's edge fades).
[[nodiscard]] model::ClipPlayback playbackOf(tracktion::AudioClipBase& clip);
[[nodiscard]] bool edgeFadesOn(const tracktion::Edit& edit);

/// Applies a clip's playback ramps over the time it plays (model::playbackGain, the function the
/// arrangement draws); silent outside it.
class ClipFadeNode final : public tracktion::graph::Node, public tracktion::TracktionEngineNode
{
public:
    ClipFadeNode(std::unique_ptr<tracktion::graph::Node> input, tracktion::ProcessState& state, model::FadeClip clip,
                 model::ClipPlayback playback);

    tracktion::graph::NodeProperties getNodeProperties() override;
    std::vector<tracktion::graph::Node*> getDirectInputNodes() override;
    bool isReadyToProcess() override;
    void process(ProcessContext& context) override;

private:
    std::unique_ptr<tracktion::graph::Node> input_;
    model::FadeClip clip_;
    model::ClipPlayback playback_;
};
} // namespace fd::engine
