#pragma once

#include "engine/MidiTap.h"
#include "engine/Session.h"
#include "model/StackEdits.h"
#include "model/Takes.h"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <atomic>
#include <map>
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
                             private te::ParameterChangeHandler::Listener,
                             private te::AutomatableParameter::Listener,
                             private te::TransportControl::Listener
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
    /// A chain's user devices, in order (without the mixer's plugins: volume, meter, sends).
    [[nodiscard]] static std::vector<te::Plugin*> chain(te::PluginList& list);

    std::unique_ptr<PluginScanner> scanner;

    std::optional<std::uint64_t> midiTarget; // the track live MIDI plays (C7): softly armed (C5)
    void midiDevicesChanged();

    // ---- recording (C5, ADRs 0030 and 0031) ----------------------------------------------------
    static inline const juce::Identifier audioInId{"fdAudioIn"}, midiInId{"fdMidiIn"}, midiChannelId{"fdMidiChannel"},
        monitorId{"fdMonitor"}, recordsId{"fdRecords"}, armedId{"fdArmed"}, overdubId{"fdMidiOverdub"};
    /// A track's inputs and arm state (softly armed: the live MIDI target, while none is armed).
    [[nodiscard]] model::InputInfo inputOf(const te::AudioTrack& track) const;
    [[nodiscard]] bool anyArmed() const;
    /// Routes every track's inputs to tracktion's input destinations: a destination where a track
    /// hears or records an input, with its own monitoring and MIDI channel (the per-track inputs
    /// patch). Never while recording (tracktion refuses); arming then only punches in or out.
    void applyInputs();
    /// The project's folder: the project file's, or before the first save a temporary one under
    /// `unsavedRoot`, made when first needed.
    juce::File projectFolder();
    juce::File unsavedRoot = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("Segno")
                                 .getChildFile("Unsaved");
    std::optional<juce::File> unsavedFolder;
    /// A new recording's file: `<Track> <n>.<extension>` in Recordings.
    juce::File recordingFile(te::Track& track, const juce::String& extension);
    /// Saving to `file`: recordings made before the first save move into its folder, and every
    /// source path becomes relative to it (ADR 0030).
    void moveToProject(const juce::File& file);
    /// One undo step for a recording pass (audio, MIDI and automation together).
    void beginPass(const juce::String& name);

    // ---- takes (C5b, ADRs 0031 and 0032) ---------------------------------------------------------
    // A track's FDTAKES holds a STACK per clip with takes: its range (from, to), whether it's open
    // (fdOpen, view state), and its TAKEs (id, name) holding the states of their clips (plain
    // clips with absolute times, never played). A main-lane clip made from a take carries "fdTake",
    // the take clip it plays; through it, its stack. Every edit goes through the undo manager.
    static inline const juce::Identifier takesId{"FDTAKES"}, stackId{"STACK"}, takeId{"TAKE"},
        fromTakeId{"fdTake"}, showTakesId{"fdShowTakes"}, stackOpenId{"fdOpen"};
    [[nodiscard]] juce::ValueTree takesOf(te::AudioTrack& track, juce::UndoManager* um);
    [[nodiscard]] juce::ValueTree takeTree(te::AudioTrack& track, std::uint64_t take);
    [[nodiscard]] juce::ValueTree stackTree(te::AudioTrack& track, std::uint64_t stack);
    /// A stack anywhere, with its track.
    [[nodiscard]] std::pair<te::AudioTrack*, juce::ValueTree> findStack(std::uint64_t stack);
    /// The stack a main-lane clip plays from (invalid for a plain clip, or one whose take is gone).
    [[nodiscard]] juce::ValueTree stackOf(te::Clip& clip);
    /// Every take, flat (the C5b take rows, until they go).
    [[nodiscard]] std::vector<model::TakeInfo> takeInfos(te::AudioTrack& track, int colour) const;
    [[nodiscard]] std::vector<model::TakeStackInfo> stackInfos(te::AudioTrack& track, int colour) const;
    [[nodiscard]] model::TakeInfo takeInfo(const juce::ValueTree& take, int colour) const;
    /// A stored clip's info (a take's), as the snapshot shows clips.
    [[nodiscard]] model::ClipInfo storedClipInfo(const juce::ValueTree& clip, int colour) const;
    /// A new stack over [from, to].
    juce::ValueTree makeStack(te::AudioTrack& track, double from, double to, juce::UndoManager* um);
    /// A take in `stack` holding copies of `clips` (new ids). Returns it.
    juce::ValueTree addTake(juce::ValueTree stack, const juce::String& name, const std::vector<juce::ValueTree>& clips,
                            juce::UndoManager* um);
    /// A take clip's copy in the main lane over [from, to] (its part of it), cutting what it covers;
    /// `asTake` false: a plain clip. Returns the new clip (null if nothing of it is in the range).
    te::Clip* useTakeClip(te::AudioTrack& track, const juce::ValueTree& takeClip, double from, double to, bool asTake = true);
    /// A take's clips over the stack's range.
    void fillFromTake(te::AudioTrack& track, const juce::ValueTree& take);
    /// Opens (or closes) a stack's takes; opening closes the others it overlaps. View state.
    void openStack(te::AudioTrack& track, juce::ValueTree stack, bool open);
    /// Moves a stack by `delta` seconds, to `target` if it's another track.
    void moveStack(std::uint64_t stack, te::AudioTrack& target, double delta, juce::UndoManager* um);
    /// A copy of a stack, `delta` later on `target`; `map` gets each take clip's new id.
    juce::ValueTree copyStack(const juce::ValueTree& stack, te::AudioTrack& target, double delta,
                              std::map<std::uint64_t, std::uint64_t>& map, juce::UndoManager* um);
    /// Splits a stack at `at`: its right halves become a new stack, and main clips at or after
    /// `at` play from them. Returns the new stack.
    juce::ValueTree splitStack(te::AudioTrack& track, juce::ValueTree stack, double at, juce::UndoManager* um);
    /// Deletes takes: parts playing them switch to another take of their stack, or become plain.
    void removeTakes(te::AudioTrack& track, const std::set<std::uint64_t>& takes, juce::UndoManager* um);
    /// A track's main clips and stacks, as model::StackEdits sees them.
    [[nodiscard]] std::vector<model::ClipPlace> places(te::AudioTrack& track);
    [[nodiscard]] std::vector<model::StackRange> stackRanges(te::AudioTrack& track);
    /// After any edit of a track's clips (model::settleStacks): emptied stacks go, cut-off parts
    /// become plain, ranges cover their parts.
    void settle(te::AudioTrack& track, juce::UndoManager* um);
    /// Opening a project: C5b's take lanes (format 3) become their clips' stacks.
    void loadTakes(bool convert);
    /// A pass's clips with their takes (one per loop lap): what they recorded over is kept as a
    /// take, a clip with takes they cover joins them, and the clip plays the newest complete lap.
    /// Returns the main-lane clips.
    std::vector<std::uint64_t> importTakes(te::AudioTrack& track, const juce::ReferenceCountedArray<te::Clip>& clips);
    // Capture MIDI (recording.md §8): everything played, on the input clock (MidiTap::now).
    double consumedUntil = -1e300; // a capture or a pass used up what was played before
    double playStartedAt = 1e300;  // while playing, nothing played before starting counts
    bool wasPlaying = false;
    /// Moves what was played into the tap's events, with where in the song (while playing).
    void drainTap();
    /// The last phrase now (empty: nothing to capture).
    [[nodiscard]] std::vector<model::MidiEvent> phrase();
    struct TapDrain final : juce::Timer
    {
        explicit TapDrain(Impl& i) : impl(i) {}
        void timerCallback() override { impl.drainTap(); }
        Impl& impl;
    };
    TapDrain tapDrain{*this};
    std::vector<std::uint64_t> recordedClips; // made in the pass now ending
    std::optional<double> recordFrom;         // where the pass records from (the count-in ends there)
    std::map<std::uint64_t, juce::File> liveFiles;  // audio being recorded, by track
    std::map<std::uint64_t, double> liveStarts;     // recordings in progress: where each starts
    /// Stops playback after `seconds` of recording (recordUntil), however the loop wraps: counted
    /// on tracktion's unlooped position from where the pass starts recording.
    struct StopAt final : juce::Timer
    {
        explicit StopAt(Impl& i) : impl(i) {}
        void timerCallback() override;
        Impl& impl;
        double seconds = 0.0;
        std::optional<double> startedAt;
    };
    StopAt stopAt{*this};
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
    /// Renders a copy of the project offline (the render tool, bounces; ADR 0021, 0035): `setup`
    /// sets the copy and the renderer up (32-bit float WAV, plugins on, silence allowed) and may
    /// refuse (false: null returned). `done` runs on the message thread, with why it failed or
    /// empty. The job's progress and cancel work from any thread.
    std::shared_ptr<Session::RenderJob> renderCopy(const std::function<bool(te::Edit&, te::Renderer::Parameters&)>& setup,
                                                   std::function<void(const std::string&)> done);
    /// Renders one track of `copy`, with the tracks feeding its sidechains playing into it, muted.
    static void renderOneTrack(te::Edit& copy, te::Renderer::Parameters& params, std::uint64_t trackId);
    // Bounces in flight (C7, ADR 0035): each renders its runs one after another, then lands.
    struct Bounce;
    std::vector<std::shared_ptr<Bounce>> bounces;
    /// `Bounces` in the project's folder.
    juce::File bouncesFolder();

    Session& owner;
    te::Engine& engine;
    MidiTap tap{engine}; // Capture MIDI: everything played (after the engine it listens to)
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<const model::Snapshot> snapshot;
    std::uint64_t version = 0;
    /// A module panel's view state on its plugin (ADR 0022): saved, never undoable or "unsaved".
    static inline const juce::Identifier panelViewId{"fdPanel"};

    // ---- automation (C4, ADRs 0028 and 0029) ------------------------------------------------

    /// A parameter that can be automated, and how its engine value maps to its control's travel
    /// (0..1): travel = (value - offset) / scale.
    struct Param
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

        [[nodiscard]] float toTravel(float value) const noexcept { return std::clamp((value - offset) / scale, 0.0f, 1.0f); }
        [[nodiscard]] float toEngine(float travel) const noexcept { return offset + std::clamp(travel, 0.0f, 1.0f) * scale; }
    };
    /// Every parameter of a channel that can be automated: volume, pan and sends, then each
    /// module's (ours: those flagged Automatable).
    [[nodiscard]] std::vector<Param> paramsOf(std::uint64_t channelId) const;
    [[nodiscard]] std::optional<Param> param(const ParamTarget& target) const;
    /// Where an engine parameter is: its channel and key.
    [[nodiscard]] std::optional<ParamTarget> targetOf(const te::AutomatableParameter& param) const;
    /// Every channel: the tracks, the returns, the master.
    [[nodiscard]] std::vector<std::uint64_t> channels() const;

    // Lanes (ADR 0029) are Segno's own state in the edit: FDAUTOMATION holds a LANE per lane (id,
    // name, colour, after), each with its POINTS (POINT t v c) and ASSIGN entries (channel, key,
    // from, to), written through the undo manager. Each assigned parameter's curve is derived:
    // the lane through the range, onto the parameter's own values (automation.md §13).
    static inline const juce::Identifier automationId{"FDAUTOMATION"}, laneId{"LANE"}, pointsId{"POINTS"},
        assignId{"ASSIGN"}, nameId{"name"}, colourId{"colour"}, afterId{"after"}, assignChannelId{"channel"}, keyId{"key"},
        fromId{"from"}, toId{"to"};
    [[nodiscard]] juce::ValueTree lanesTree() const { return edit->state.getChildWithName(automationId); }
    [[nodiscard]] juce::ValueTree laneTree(std::uint64_t id) const;
    /// A parameter's ASSIGN entry, on whichever lane it is.
    [[nodiscard]] juce::ValueTree assignTree(const ParamTarget& target) const;
    [[nodiscard]] static std::uint64_t idOf(const juce::ValueTree& lane);
    [[nodiscard]] static model::AutomationPoints pointsOf(const juce::ValueTree& lane);
    [[nodiscard]] static ParamTarget targetIn(const juce::ValueTree& assign);
    [[nodiscard]] static model::AssignmentRange rangeOf(const juce::ValueTree& assign);
    /// Writes points in place: existing ones change, extra ones are added or removed.
    static void writePoints(juce::ValueTree points, const model::AutomationPoints& with, juce::UndoManager* um);
    /// The same for a parameter's curve (engine values), so a curve with points is never emptied
    /// and attached again: the playback graph isn't rebuilt (automation.md §13).
    static void writeCurve(te::AutomationCurve& curve, const model::AutomationPoints& with, juce::UndoManager* um);
    /// A new lane at `index` of FDAUTOMATION (-1: the end).
    juce::ValueTree makeLane(std::uint64_t after, int colour, int index, juce::UndoManager* um);
    /// A new lane's place for `after` (a track, a lane, a return or the master, or 0): its anchor
    /// track and index.
    [[nodiscard]] std::pair<std::uint64_t, int> placeFor(std::uint64_t after) const;
    /// Assigns a parameter to a lane (moving it). False if it can't be automated.
    bool assignTo(juce::ValueTree lane, const ParamTarget& target, model::AssignmentRange range, juce::UndoManager* um);
    /// A lane's steps: its one assignment's, when stepped and full-range (automation.md §5).
    [[nodiscard]] int laneSteps(const juce::ValueTree& lane) const;
    /// A channel's colour (the master: -1).
    [[nodiscard]] int colourOf(std::uint64_t channel) const;
    /// The colour of a new row: tracks and lanes take turns through the palette.
    [[nodiscard]] int nextRowColour() const;
    /// Where a new lane goes: at the end of the tracks (after the last one, and its lanes).
    [[nodiscard]] std::uint64_t lastTrack() const;
    /// Writes every assignment of a lane to its parameter's curve (not those being recorded).
    void derive(const juce::ValueTree& lane, juce::UndoManager* um);
    void derive(const model::AutomationPoints& lanePoints, const juce::ValueTree& assign, juce::UndoManager* um);
    /// The lanes for the snapshot, in row order; assignments whose parameter is gone are left out.
    [[nodiscard]] std::vector<model::AutomationLaneInfo> laneInfos() const;
    /// Opening a project: converts format 2, drops assignments whose channel or module is gone,
    /// and derives every curve. Without undo.
    void loadLanes(bool convert);

    /// True while the parameter plays its lane (points, on, not being recorded): its played
    /// values never dirty the project or rebuild snapshots.
    [[nodiscard]] static bool followsLane(const te::AutomatableParameter& param);
    /// The parameter a plugin property belongs to, if any.
    [[nodiscard]] te::AutomatableParameter* parameterFor(const juce::ValueTree& pluginState, const juce::Identifier& id) const;
    /// Every track reads, or (armed) touch-writes, automation.
    void applyAutomationMode();
    void endRecordingPass();

    bool armed = false;
    std::optional<std::pair<std::uint64_t, juce::String>> lastTouched; // plugin id, parameter id
    std::unique_ptr<te::Edit::UndoTransactionInhibitor> recordingPass;  // one undo step per pass
    /// A parameter recorded in this pass, and its lane when its recording began (§8).
    struct Recorded
    {
        te::AutomatableParameter::Ptr param;
        ParamTarget target;
        std::uint64_t lane = 0;
        model::AutomationPoints laneAtStart;
    };
    std::vector<Recorded> recorded;
    void startRecording(Recorded& r);
    /// Splices a parameter's recorded pass into its lane (a new lane if it has none).
    void importRecording(const Recorded& r);
    bool restartPending = false; // the playback graph is rebuilt when automation comes or goes

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void recordingFinished(te::InputDeviceInstance& instance, te::EditItemID target,
                           const juce::ReferenceCountedArray<te::Clip>& clips) override;
    void recordingAboutToStart(te::InputDeviceInstance& instance, te::EditItemID target) override;
    void pluginParameterChanged(te::AutomatableParameter& param, bool isFollowingAutomation) override;
    void curveHasChanged(te::AutomatableParameter&) override {}
    void recordingStatusChanged(te::AutomatableParameter& param) override;
    void changed(juce::ValueTree& tree);
    void handleAsyncUpdate() override
    {
        applyInputs(); // tracks, inputs or instruments may have changed (undo too)
        if (std::exchange(restartPending, false) && edit != nullptr)
            edit->restartPlayback(); // fine-grained automation needs a new graph (automation.md §13)
        rebuildSnapshot();
    }
    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier& id) override
    {
        if (id == te::IDs::lastSignificantChange || id == panelViewId || id == te::IDs::automationMode) // bookkeeping and view state
            return;
        // Arming is a listening state, like solo; showing takes is view state; input routing
        // follows the tracks' inputs.
        if (id == armedId || id == showTakesId || id == stackOpenId || inInputs(t))
        {
            if (id != armedId && id != showTakesId && id != stackOpenId)
                return;
            changed(t);
            return;
        }
        // An assignment's on switch: shown at once, never an unsaved change.
        if (id == te::IDs::bypass)
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
        if (inInputs(t) || child.hasType(te::IDs::INPUTDEVICES))
            return; // routing, rebuilt from the tracks' inputs
        restartPending = restartPending || child.hasType(te::IDs::AUTOMATIONCURVE);
        structureChanged(t);
    }
    void valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree& child, int) override
    {
        if (inInputs(t) || child.hasType(te::IDs::INPUTDEVICES))
            return;
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
    static bool inInputs(juce::ValueTree t)
    {
        for (; t.isValid(); t = t.getParent())
            if (t.hasType(te::IDs::INPUTDEVICES))
                return true;
        return false;
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
