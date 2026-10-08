#pragma once

#include "engine/Session.h"

#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

class PluginScanner;

struct Session::Impl final : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::ChangeListener
{
    Impl(Session& owner, te::Engine& engine);
    ~Impl() override;

    void rebuildSnapshot();
    [[nodiscard]] te::AudioTrack* track(std::uint64_t id) const;
    [[nodiscard]] te::Clip* clip(std::uint64_t id) const;
    [[nodiscard]] te::Plugin* plugin(std::uint64_t id) const;
    // Edit steps without starting an undo transaction (commands compose them).
    te::AudioTrack* insertTrack(const juce::String& name);
    te::Plugin* insertDevice(te::AudioTrack& track, const std::string& key, int index);
    /// The track's user devices, in order (without tracktion's volume and meter plugins).
    [[nodiscard]] static std::vector<te::Plugin*> chain(te::AudioTrack& track);

    std::unique_ptr<PluginScanner> scanner;

    std::optional<std::uint64_t> midiTarget; // the track live MIDI plays (C7)
    void midiDevicesChanged();
    void applyMidiTarget();
    void beginEdit() { edit->getUndoManager().beginNewTransaction(); }

    Session& owner;
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<const model::Snapshot> snapshot;
    std::uint64_t version = 0;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { midiDevicesChanged(); }
    void changed(juce::ValueTree& tree);
    void handleAsyncUpdate() override { rebuildSnapshot(); }
    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier& id) override
    {
        if (id == te::IDs::lastSignificantChange) // tracktion's own bookkeeping
            return;
        // Device parameters and state change constantly; only a device's power state shows.
        for (auto p = t; p.isValid(); p = p.getParent())
            if (p.hasType(te::IDs::PLUGIN) && !(p == t && id == te::IDs::enabled))
                return;
        changed(t);
    }
    void valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree&) override { changed(t); }
    void valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree&, int) override { changed(t); }
    void valueTreeChildOrderChanged(juce::ValueTree& t, int, int) override { changed(t); }
};
} // namespace fd::engine
