// No note outlives what plays it (the stuck notes found in the M2 song test). What changes in a
// clip's MIDI and what a live input stops reaching are handled in the engine (the
// midi-note-continuity and input-note-offs patches); a module's power is the Session's.

#include "engine/SessionImpl.h"

namespace fd::engine
{
void Session::Impl::endNotesOnPowerChange(const juce::ValueTree& pluginState)
{
    // A module turned off is skipped, so it keeps whatever it held: turned back on, it would sound
    // it again. An instrument turned back on, and a MIDI effect turned on or off (it may hold notes
    // it changed), end their track's notes, as tracktion does for a muted MIDI clip.
    if (edit == nullptr)
        return;
    const auto plugin = edit->getPluginCache().getPluginFor(pluginState);
    if (plugin == nullptr || !plugin->takesMidiInput() || (plugin->isSynth() && !plugin->isEnabled()))
        return;
    if (auto* track = dynamic_cast<te::AudioTrack*>(plugin->getOwnerTrack()))
        for (int channel = 1; channel <= 16; ++channel)
            track->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(channel), {});
}

void Session::Impl::endNotesOnRemoval(const juce::ValueTree& parent, const juce::ValueTree& pluginState)
{
    if (edit == nullptr || !parent.hasType(te::IDs::TRACK))
        return;
    // Still in the plugin cache while it's being taken out.
    const auto plugin = edit->getPluginCache().getPluginFor(pluginState);
    if (plugin == nullptr || !plugin->takesMidiInput())
        return;
    for (auto* track : te::getAudioTracks(*edit))
        if (track->state == parent)
            for (int channel = 1; channel <= 16; ++channel)
                track->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(channel), {});
}
} // namespace fd::engine
