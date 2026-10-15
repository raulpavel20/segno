#pragma once

#include "model/AudioSound.h"
#include "model/Fades.h"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>

namespace fd::engine
{
// Clip fades and crossfades in the engine (M2 step C6, ADR 0036, docs/design/fades.md): Segno's
// state on a clip, the clip as model::Fades sees it, and the node that plays its ramps. The node
// plays the clip's sound too (P6, ADR 0063, audio-editor.md §6): its gain, gain line, polarity and
// channels.

/// Segno's fade state on an audio clip (tracktion's own fadeIn and fadeOut hold the lengths).
namespace fadeIds
{
inline const juce::Identifier inBend{"fdFadeInBend"}, outBend{"fdFadeOutBend"};
/// The crossfade at its end, into the clip that starts there (model::Crossfade).
inline const juce::Identifier crossLength{"fdXLength"}, crossFrom{"fdXFrom"}, crossShape{"fdXShape"};
/// On the edit: the project's edge fades (fades.md §2); absent means on.
inline const juce::Identifier edgeFades{"fdEdgeFades"};
} // namespace fadeIds

/// Segno's sound state on an audio clip (audio-editor.md §6).
namespace soundIds
{
inline const juce::Identifier gain{"fdGain"}, inverted{"fdInverted"}, channels{"fdChannels"};
/// A reversed clip plays a reversed copy of its file (in the project's Reversed/ folder); this is
/// the source it plays forwards, as tracktion's `source` stored it.
inline const juce::Identifier forward{"fdForward"};
/// The gain line: a child of the clip, its points children of it (time in the file, value, curve).
inline const juce::Identifier line{"FDGAINLINE"}, point{"FDGAINPOINT"}, time{"t"}, value{"v"}, curve{"c"};
} // namespace soundIds

/// The clip's sound as stored on it.
[[nodiscard]] model::AudioSound soundOf(const tracktion::AudioClipBase& clip);
/// Writes the clip's gain line (an empty one removes it), undoably.
void writeGainLine(juce::ValueTree& clip, const model::AutomationPoints& line, juce::UndoManager* um);
/// Whether a change to `tree` changes what a clip plays through its sound: a property of its own
/// or a point of its gain line.
[[nodiscard]] bool affectsSound(const juce::ValueTree& tree, const juce::Identifier& id);

/// The clip as fades see it. Clips that play stretched or synced have no spare audio to crossfade
/// with; a looped one's is its neighbouring lap (ADR 0048).
[[nodiscard]] model::FadeClip fadeClipOf(tracktion::AudioClipBase& clip);
/// The clip with the audio clips just before and after it on its track (what its playback depends
/// on), and its place among them: cheap, for the engine's graph building.
[[nodiscard]] std::pair<std::vector<model::FadeClip>, size_t> fadeNeighboursOf(tracktion::AudioClipBase& clip);
/// What the clip plays (model::clipPlayback with the project's edge fades).
[[nodiscard]] model::ClipPlayback playbackOf(tracktion::AudioClipBase& clip);
[[nodiscard]] bool edgeFadesOn(const tracktion::Edit& edit);

/// Applies a clip's playback ramps over the time it plays (model::playbackGain, the function the
/// arrangement draws), silent outside it, and its sound: gain and gain line (model::soundGainAt),
/// polarity and channels.
class ClipFadeNode final : public tracktion::graph::Node, public tracktion::TracktionEngineNode
{
public:
    ClipFadeNode(std::unique_ptr<tracktion::graph::Node> input, tracktion::ProcessState& state,
                 model::FadeClip clip, model::ClipPlayback playback, model::AudioSound sound);

    tracktion::graph::NodeProperties getNodeProperties() override;
    std::vector<tracktion::graph::Node*> getDirectInputNodes() override;
    bool isReadyToProcess() override;
    void process(ProcessContext& context) override;

private:
    std::unique_ptr<tracktion::graph::Node> input_;
    model::FadeClip clip_;
    model::ClipPlayback playback_;
    model::AudioSound sound_;
    float gain_ = 1.0f;   // the clip's gain, linear
    bool neutral_ = true; // its sound as recorded
};
} // namespace fd::engine
