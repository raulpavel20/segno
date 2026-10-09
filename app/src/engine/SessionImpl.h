#pragma once

#include "engine/Session.h"

#include <tracktion_engine/tracktion_engine.h>

#include <set>

namespace fd::engine
{
namespace te = tracktion;

class PluginScanner;

struct Session::Impl final : private juce::ValueTree::Listener, public juce::AsyncUpdater, private juce::ChangeListener
{
    Impl(Session& owner, te::Engine& engine);
    ~Impl() override;

    void rebuildSnapshot();
    [[nodiscard]] std::unique_ptr<te::Edit> newEdit();
    void attach(std::unique_ptr<te::Edit> edit);
    void detach();
    std::optional<juce::File> file;
    bool dirty = false; // changed since opened or saved (tracktion's own flag updates on a timer)
    [[nodiscard]] te::AudioTrack* track(std::uint64_t id) const;
    [[nodiscard]] te::Clip* clip(std::uint64_t id) const;
    [[nodiscard]] te::Plugin* plugin(std::uint64_t id) const;
    // Edit steps without starting an undo transaction (commands compose them).
    te::AudioTrack* insertTrack(const juce::String& name);
    te::Plugin* insertDevice(te::AudioTrack& track, const std::string& key, int index);
    /// ADR 0019: clips on `track` covered by [start, end) are cut (except the ones placed).
    void clearUnder(te::AudioTrack& track, double start, double end, const std::set<std::uint64_t>& except);
    /// A MIDI clip needs an instrument (ADR 0019): adds the default one to a track without one.
    /// True if it added it.
    bool ensureInstrument(te::AudioTrack& track);
    /// The track's user devices, in order (without tracktion's volume and meter plugins).
    [[nodiscard]] static std::vector<te::Plugin*> chain(te::AudioTrack& track);

    std::unique_ptr<PluginScanner> scanner;

    std::optional<std::uint64_t> midiTarget; // the track live MIDI plays (C7)
    void midiDevicesChanged();
    void applyMidiTarget();
    /// Starts a command's undo step, named for undo menus and assistants ("Add track"); inside
    /// an EditGroup the group's step takes the edits instead.
    void beginEdit(const juce::String& name)
    {
        if (groupDepth == 0)
            edit->getUndoManager().beginNewTransaction(name);
    }
    int groupDepth = 0;
    juce::ListenerList<Session::Listener> listeners;
    struct Render;
    std::vector<std::shared_ptr<Render>> renders; // offline renders in flight (each on an Edit copy)

    Session& owner;
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<const model::Snapshot> snapshot;
    std::uint64_t version = 0;
    /// A module panel's view state on its plugin (ADR 0022): saved, never undoable or "unsaved".
    static inline const juce::Identifier panelViewId{"fdPanel"};

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { midiDevicesChanged(); }
    void changed(juce::ValueTree& tree);
    void handleAsyncUpdate() override { rebuildSnapshot(); }
    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier& id) override
    {
        if (id == te::IDs::lastSignificantChange || id == panelViewId) // bookkeeping and view state
            return;
        if (!inTransport(t))
            dirty = true; // includes device parameters
        // Device parameters and state change constantly; only a device's power state and its
        // sidechain source (ADR 0025) show.
        for (auto p = t; p.isValid(); p = p.getParent())
            if (p.hasType(te::IDs::PLUGIN) && !(p == t && (id == te::IDs::enabled || id == te::IDs::sidechainSourceID)))
            {
                deviceStateChanged(p);
                return;
            }
        changed(t);
    }
    void valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree&) override { structureChanged(t); }
    void valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree&, int) override { structureChanged(t); }
    void valueTreeChildOrderChanged(juce::ValueTree& t, int, int) override { structureChanged(t); }
    void deviceStateChanged(const juce::ValueTree& plugin)
    {
        const auto id = te::EditItemID::fromID(plugin).getRawID();
        listeners.call([id](Session::Listener& l) { l.deviceStateChanged(id); });
    }
    void structureChanged(juce::ValueTree& t)
    {
        if (!inTransport(t))
            dirty = true;
        changed(t);
    }
    static bool inTransport(juce::ValueTree t)
    {
        for (; t.isValid(); t = t.getParent())
            if (t.hasType(te::IDs::TRANSPORT))
                return true;
        return false;
    }
};
} // namespace fd::engine
