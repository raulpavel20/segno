#pragma once

#include "engine/Session.h"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <set>
#include <utility>

namespace fd::engine
{
namespace te = tracktion;

class PluginScanner;
class DevicePlugin;

template <typename PluginType>
PluginType* findIn(te::PluginList& list)
{
    for (auto* p : list)
        if (auto* found = dynamic_cast<PluginType*>(p))
            return found;
    return nullptr;
}

/// A track's send on aux bus `bus`, if it has one.
inline te::AuxSendPlugin* sendIn(te::PluginList& list, int bus)
{
    for (auto* p : list)
        if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p); send != nullptr && send->getBusNumber() == bus)
            return send;
    return nullptr;
}

/// The aux bus a return track listens to (-1 if none).
inline int busOf(te::AudioTrack& returnTrack)
{
    auto* in = findIn<te::AuxReturnPlugin>(returnTrack.pluginList);
    return in != nullptr ? in->busNumber.get() : -1;
}

struct Session::Impl final : private juce::ValueTree::Listener,
                             public juce::AsyncUpdater,
                             private juce::ChangeListener,
                             private te::ParameterChangeHandler::Listener
{
    Impl(Session& owner, te::Engine& engine);
    ~Impl() override;

    void rebuildSnapshot();
    [[nodiscard]] std::unique_ptr<te::Edit> newEdit();
    void attach(std::unique_ptr<te::Edit> edit);
    void detach();
    std::optional<juce::File> file;
    bool dirty = false; // changed since opened or saved (tracktion's own flag updates on a timer)
    /// A track that holds clips (not a return track); null otherwise.
    [[nodiscard]] te::AudioTrack* track(std::uint64_t id) const;
    /// A return track (ADR 0026); null otherwise.
    [[nodiscard]] te::AudioTrack* returnTrack(std::uint64_t id) const;
    /// A track or a return track.
    [[nodiscard]] te::AudioTrack* channelTrack(std::uint64_t id) const;
    /// The chain of any channel: a track, a return, or the master's plugin list.
    [[nodiscard]] te::PluginList* plugins(std::uint64_t channelId) const;
    [[nodiscard]] std::uint64_t masterId() const;
    /// The tracks that hold clips, and the return tracks, each in order.
    [[nodiscard]] std::vector<te::AudioTrack*> tracks() const;
    [[nodiscard]] std::vector<te::AudioTrack*> returns() const;
    [[nodiscard]] static bool isReturn(const te::Track& track);
    [[nodiscard]] te::Clip* clip(std::uint64_t id) const;
    [[nodiscard]] te::Plugin* plugin(std::uint64_t id) const;
    // Edit steps without starting an undo transaction (commands compose them).
    te::AudioTrack* insertTrack(const juce::String& name);
    te::Plugin* insertDevice(te::PluginList& list, const std::string& key, int index);
    /// Every channel's volume uses the balance pan law, and the master has a volume and meter of
    /// its own at the end of its chain (ADR 0026). Brings older projects up to date.
    void ensureChannels(juce::UndoManager* undo);
    /// ADR 0019: clips on `track` covered by [start, end) are cut (except the ones placed).
    void clearUnder(te::AudioTrack& track, double start, double end, const std::set<std::uint64_t>& except);
    /// A MIDI clip needs an instrument (ADR 0019): adds the default one to a track without one.
    /// True if it added it.
    bool ensureInstrument(te::AudioTrack& track);
    /// A chain's user devices, in order (without the mixer's plugins: volume, meter, sends).
    [[nodiscard]] static std::vector<te::Plugin*> chain(te::PluginList& list);

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

    // ---- automation (C4, ADR 0028) ----------------------------------------------------------

    /// A parameter that can have a lane, and how its engine value maps to the lane's 0..1:
    /// lane = (value - offset) / scale.
    struct Lane
    {
        enum class Kind { Volume, Pan, Send, Module, External };
        Kind kind = Kind::Module;
        te::AutomatableParameter* param = nullptr; // null for a send not made yet
        std::string key, name, module, paramId;
        std::uint64_t device = 0, returnId = 0;
        int steps = 0;
        float offset = 0.0f, scale = 1.0f;
        DevicePlugin* native = nullptr; // our module
        size_t index = 0;               // its parameter's index

        [[nodiscard]] float toLane(float value) const noexcept { return std::clamp((value - offset) / scale, 0.0f, 1.0f); }
        [[nodiscard]] float toEngine(float lane) const noexcept { return offset + std::clamp(lane, 0.0f, 1.0f) * scale; }
    };
    /// Every parameter of a channel that can be automated: volume, pan and sends, then each
    /// module's (ours: those flagged Automatable).
    [[nodiscard]] std::vector<Lane> lanesOf(std::uint64_t channelId) const;
    [[nodiscard]] std::optional<Lane> lane(const LaneTarget& target) const;
    /// The track (or the master track) whose state holds a channel's lane view state.
    [[nodiscard]] te::Track* stateTrack(std::uint64_t channelId) const;
    [[nodiscard]] static std::vector<std::string> shownLanes(const te::Track& track);
    static void setShownLanes(te::Track& track, const std::vector<std::string>& keys);
    /// The channel's lanes for the snapshot: the shown ones in order, then any other with points.
    [[nodiscard]] std::vector<model::LaneInfo> laneInfos(std::uint64_t channelId) const;
    /// True while the parameter plays its lane (points, on, not being recorded): its played
    /// values never dirty the project or rebuild snapshots.
    [[nodiscard]] static bool followsLane(const te::AutomatableParameter& param);
    /// The parameter a plugin property belongs to, if any.
    [[nodiscard]] te::AutomatableParameter* parameterFor(const juce::ValueTree& pluginState, const juce::Identifier& id) const;
    /// Every track reads, or (armed) touch-writes, automation.
    void applyAutomationMode();
    void endRecordingPass();

    static inline const juce::Identifier lanesId{"fdLanes"};             // shown lanes, view state
    static inline const juce::Identifier lanesFoldedId{"fdLanesFolded"}; // view state
    bool armed = false;
    std::optional<std::pair<std::uint64_t, juce::String>> lastTouched; // plugin id, parameter id
    std::unique_ptr<te::Edit::UndoTransactionInhibitor> recordingPass;  // one undo step per pass
    bool restartPending = false; // the playback graph is rebuilt when automation comes or goes

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void pluginParameterChanged(te::AutomatableParameter& param, bool isFollowingAutomation) override;
    void changed(juce::ValueTree& tree);
    void handleAsyncUpdate() override
    {
        if (std::exchange(restartPending, false) && edit != nullptr)
            edit->restartPlayback(); // fine-grained automation needs a new graph (automation.md §13)
        rebuildSnapshot();
    }
    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier& id) override
    {
        if (id == te::IDs::lastSignificantChange || id == panelViewId || id == te::IDs::automationMode) // bookkeeping and view state
            return;
        // Lane view state and a lane's on switch: shown at once, never an unsaved change.
        if (id == lanesId || id == lanesFoldedId || id == te::IDs::bypass)
        {
            changed(t);
            return;
        }
        // A parameter playing its lane: tracktion writes each played value back (ADR 0028).
        if (t.hasType(te::IDs::PLUGIN))
            if (auto* param = parameterFor(t, id); param != nullptr && followsLane(*param))
                return;
        if (!inTransport(t) && id != te::IDs::solo) // solo is a listening aid (ADR 0026)
            dirty = true; // includes device parameters
        // A lane's points.
        for (auto p = t; p.isValid() && !p.hasType(te::IDs::PLUGIN); p = p.getParent())
            if (p.hasType(te::IDs::AUTOMATIONCURVE))
            {
                changed(t);
                return;
            }
        // Device parameters and state change constantly; only a device's power state, its
        // sidechain source (ADR 0025) and the mixer's own plugins (volume, pan, sends) show.
        for (auto p = t; p.isValid(); p = p.getParent())
            if (p.hasType(te::IDs::PLUGIN) && !(p == t && (id == te::IDs::enabled || id == te::IDs::sidechainSourceID)))
            {
                const auto type = p[te::IDs::type].toString();
                if (type == te::VolumeAndPanPlugin::xmlTypeName || type == te::AuxSendPlugin::xmlTypeName)
                    break;
                deviceStateChanged(p);
                return;
            }
        changed(t);
    }
    void valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree& child) override
    {
        restartPending = restartPending || child.hasType(te::IDs::AUTOMATIONCURVE);
        structureChanged(t);
    }
    void valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree& child, int) override
    {
        restartPending = restartPending || child.hasType(te::IDs::AUTOMATIONCURVE);
        structureChanged(t);
    }
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
