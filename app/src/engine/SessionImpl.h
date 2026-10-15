#pragma once

#include "engine/ClipChance.h"
#include "engine/ClipNotes.h"
#include "engine/FileIndexer.h"
#include "engine/MidiTap.h"
#include "engine/Modulator.h"
#include "engine/PreviewPlayer.h"
#include "engine/Session.h"
#include "model/StackEdits.h"
#include "model/Takes.h"
#include "model/TrackEdits.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <tracktion_engine/tracktion_engine.h>
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

/// A clip's loop as the model sees it (ADR 0048, model/ClipLoops.h): seconds of its content, from
/// the engine's own properties on a clip's state (a MIDI clip's beats at the song's tempo). None
/// while it doesn't loop, and while the engine stretches it to the tempo (it can't loop then).
[[nodiscard]] model::ClipLoop loopOf(const juce::ValueTree& clip, const te::TempoSequence& tempo);
/// Writes a loop into a clip (length 0: none): an audio clip's in whole samples of its file, the
/// tempo sync left off; a MIDI clip's in beats.
void writeLoop(te::Clip& clip, const model::ClipLoop& loop, juce::UndoManager* um);
/// Places a clip; while it loops, its offset (the phase) is wrapped into the loop.
void placeClip(te::Clip& clip, double start, double length, double offset);

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
    /// Why a clip can't loop (model::whyCantLoop), or empty if it can (ADR 0048).
    [[nodiscard]] std::string loopRefusal(te::Clip& clip);
    [[nodiscard]] te::Plugin* plugin(std::uint64_t id) const;
    // Edit steps without starting an undo transaction (commands compose them).
    te::AudioTrack* insertTrack(); // named after the highest "N Track" (tracks-and-clips.md §18)
    /// Every track's and return's name, and every lane's (for the names the app gives).
    [[nodiscard]] std::vector<std::string> trackNames() const;
    [[nodiscard]] std::vector<std::string> laneNames() const;
    /// The tracks and lanes in the arrangement's order: the lanes at the top, each track and the
    /// lanes after it, then lanes whose track is gone.
    [[nodiscard]] std::vector<model::RowRef> rowOrder() const;
    /// Puts the tracks and lanes in `order` (tracks-and-clips.md §19): `moved`, if a track moved,
    /// to its new place; each lane after its track, in order.
    void placeRows(const model::RowOrder& order, te::Track* moved, juce::UndoManager* um);
    te::Plugin* insertDevice(te::PluginList& list, const std::string& key, int index);
    /// Every channel's volume uses the balance pan law, and the master has a volume and meter of
    /// its own at the end of its chain (ADR 0026). Brings older projects up to date.
    void ensureChannels(juce::UndoManager* undo);
    /// ADR 0019: clips on `track` covered by [start, end) are cut (except the ones placed).
    void clearUnder(te::AudioTrack& track, double start, double end, const std::set<std::uint64_t>& except);
    /// A chain's user devices, in order (without the mixer's plugins: volume, meter, sends).
    [[nodiscard]] static std::vector<te::Plugin*> chain(te::PluginList& list);

    std::unique_ptr<PluginScanner> scanner;

    // The browser (C2, ADR 0046): the index of the places, the preview, and every file a clip or
    // take has used since the project opened (Remove unused files leaves those).
    FileIndexer indexer;
    PreviewPlayer preview;
    std::vector<std::string> browserPlaces;
    juce::File segnoFolder = defaultSegnoFolder();
    juce::File libraryChoice;    // Settings > Library; none: where it's installed
    bool browserStarted = false; // places given (Settings): the indexer looks at them
    std::set<std::string> usedSources;
    /// A MIDI file previewed on the live MIDI target: its notes sent as live MIDI, in time.
    struct MidiPreview final : juce::Timer
    {
        explicit MidiPreview(Impl& i) : impl(i) {}
        ~MidiPreview() override { stop(); }
        void timerCallback() override;
        void stop();
        Impl& impl;
        std::uint64_t track = 0;
        std::vector<std::pair<double, juce::MidiMessage>> events; // seconds from the start
        size_t next = 0;
        double started = 0.0; // juce::Time::getMillisecondCounterHiRes() / 1000
    };
    MidiPreview midiPreview{*this};
    /// Gives the indexer the places: yours, the project's folder, the Segno folder.
    void updateBrowserPlaces();

    std::optional<std::uint64_t> midiTarget; // the track live MIDI plays (C7): softly armed (C5)
    void midiDevicesChanged();

    /// A track's or clip's colour (an index into the clip palette, `paletteSize` long) and a
    /// track's role ("return" on a return track, ADR 0026).
    static inline const juce::Identifier colourPropertyId{"fdColour"}, rolePropertyId{"fdRole"};
    static constexpr int paletteSize = model::kPaletteSize;
    static constexpr int maxReturns = 32; // tracktion's aux buses
    /// The lowest aux bus no return listens to yet (maxReturns: none free).
    [[nodiscard]] int freeBus() const;

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
    // Presets (ADR 0044): made when first asked for, in presetsFolder.
    std::unique_ptr<PresetLibrary> presets;
    // Settings (ADR 0045).
    std::vector<std::string> pluginFolders;
    model::ExportSettings exportDefaults;
    juce::File presetsFolder = ui::defaultPresetsFolder();
    /// Sets a whole module state and its preset memo in the current undo step (ADR 0044).
    void applyModuleState(DevicePlugin& plugin, const DeviceState& state, const PresetMemo& memo);
    /// The memo a module keeps beside its state (`fdPreset`).
    [[nodiscard]] static PresetMemo presetMemo(const DevicePlugin& plugin);
    /// A modulator's state as presets keep it (modulators.md §14): its settings and a Steps
    /// pattern, never its routes or its roll; set whole, with its memo, in the current step.
    [[nodiscard]] static DeviceState modulatorState(const Modulator& modulator);
    void applyModulatorState(Modulator& modulator, const DeviceState& state, const PresetMemo& memo);
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
        fromTakeId{"fdTake"}, showTakesId{"fdShowTakes"}, stackOpenId{"fdOpen"}, auditionId{"fdAudition"};
    /// A take heard in place of its clip (ADR 0038): copies of its clips on the track and the
    /// clip's parts muted, all outside the undo history; never in the snapshot or a saved file.
    struct Audition
    {
        std::uint64_t track = 0, stack = 0, take = 0;
        std::vector<std::uint64_t> added, muted; // the copies; the parts muted for it
    };
    std::optional<Audition> audition;
    /// Ends the audition, if any: what changes the project, saves or renders it, first does this.
    void stopAudition();
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
    /// The take alone over its stack's range (ADR 0037): the clip's parts go and the take's clips
    /// fill the range; the clip keeps its fades. False, changing nothing, if the take has nothing there.
    bool fillFromTake(te::AudioTrack& track, const juce::ValueTree& take);
    /// A stack's clips in the main lane, in time order.
    [[nodiscard]] std::vector<te::Clip*> partsOf(te::AudioTrack& track, const juce::ValueTree& stack);
    /// A clip's own fades: its first part's fade-in, its last part's fade-out and crossfade into
    /// the next clip (copies of the two parts' states), to keep while its parts change.
    [[nodiscard]] std::pair<juce::ValueTree, juce::ValueTree> clipEnds(te::AudioTrack& track, const juce::ValueTree& stack);
    /// Puts a clip's own fades back on its parts' ends; inner edges between its parts fade only
    /// by their crossfades.
    void restoreEnds(te::AudioTrack& track, const juce::ValueTree& stack, const std::pair<juce::ValueTree, juce::ValueTree>& ends,
                     juce::UndoManager* um);
    /// Joins a clip's neighbouring parts that play on seamlessly from one take clip into one part.
    void joinSeamlessParts(te::AudioTrack& track, const juce::ValueTree& stack, juce::UndoManager* um);
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
    /// become plain, ranges cover their parts and stay clear of other clips.
    void settle(te::AudioTrack& track, juce::UndoManager* um);
    /// A track's clips and take clips without a colour take one (ADR 0043): a clip's own, once.
    /// Main-lane clips take the track's; take clips their stack's playing clip's, else the track's.
    /// Called by settle(), so it runs after every edit that makes clips, and on opening a project.
    void colourNewClips(te::AudioTrack& track, juce::UndoManager* um);
    /// After any edit of a track's clips (fades.md §4): a crossfade whose clips no longer meet, or
    /// meet seamlessly, goes; a bend without its fade goes. Called by settle().
    void settleFades(te::AudioTrack& track, juce::UndoManager* um);
    /// Comp joins (fades.md §5): a crossfade of `length` (equal power, centred, fitted to the spare
    /// audio) at each join between two of `parts` at one of `times`; none where they play on
    /// seamlessly.
    void crossfadeJoinsAt(te::AudioTrack& track, const std::set<std::uint64_t>& parts, const std::vector<double>& times,
                          double length, juce::UndoManager* um);
    /// A track's audio clips in the snapshot get their fades, crossfades and playback.
    void fillFades(te::AudioTrack& track, std::vector<model::ClipInfo>& clips) const;
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
    /// This pass merges MIDI into the clips it plays over (overdub), unless an armed track has a
    /// looped MIDI clip: merged notes would land outside its loop (ADR 0048), so it makes its own.
    bool mergePass = false;
    [[nodiscard]] bool armedTrackLoopsMidi() const;
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
        stopAudition(); // an edit ends an audition (ADR 0038)
        keyStepOpen = false; // any other command closes the Key popover's step
        if (groupDepth == 0)
            edit->getUndoManager().beginNewTransaction(name);
    }
    int groupDepth = 0;
    // The Key popover's step (Session::KeyChoice, key-and-scale.md §6): while a choice is open its
    // changes are one undo step, kept open against tracktion's timer.
    int keyChoices = 0;
    bool keyStepOpen = false;
    std::optional<music::Key> keyBefore; // the key where the popover's open step began
    std::unique_ptr<te::Edit::UndoTransactionInhibitor> keyInhibitor;
    juce::ListenerList<Session::Listener> listeners;
    /// The editors' grids and the steps they show (ADR 0047), by Session::GridEditor.
    std::array<model::GridSettings, 2> grids{model::GridSettings::arrangement(),
                                             model::GridSettings::pianoRoll()};
    std::array<std::string, 2> gridSteps;
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
    // The export in flight (C8, ADR 0039): it renders its files one after another, each finished
    // (measured, normalised, dithered, written) on its own thread.
    struct Export;
    std::shared_ptr<Export> exporting;
    std::unique_ptr<juce::ThreadPool> exportThread;
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
        enum class Kind
        {
            Volume,
            Pan,
            Send,
            Module,
            External,
            Macro
        };
        Kind kind = Kind::Module;
        te::AutomatableParameter* param = nullptr; // null for a send not made yet
        std::string key, name, module, paramId;
        std::uint64_t device = 0, returnId = 0;
        int steps = 0;
        float offset = 0.0f, scale = 1.0f;
        DevicePlugin* native = nullptr; // our module
        size_t index = 0;               // its parameter's index
        const ParamSpec* spec = nullptr; // ours (a module's or a modulator's): what its values mean

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
    /// The rack macro whose state this is (racks.md §4); null if none.
    [[nodiscard]] te::MacroParameter* macroFor(const juce::ValueTree& macroState) const;
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

    // ---- racks (M3 P2, docs/design/racks.md, ADR 0056) -----------------------------------------
    // A rack is tracktion's RackType placed as a RackInstance in a chain, one type each. Its chains
    // are Segno's: each module's PLUGININSTANCE in the rack names its chain (fdChain), in the
    // rack's order; each chain ends in a volume and pan of its own (fdChainMixer) holding the
    // chain's name, on and solo. The connections follow from them (wireRack). Its eight macros are
    // the type's macro parameters; a mapping is a MACRO modifier assignment on the parameter, its
    // base value From, its value To - From, its curve half Curve (racks.md §12).
    static inline const juce::Identifier chainId{"fdChain"}, chainMixerId{"fdChainMixer"},
        chainNameId{"fdChainName"}, chainOnId{"fdChainOn"}, chainSoloId{"fdChainSolo"},
        shownChainId{"fdShownChain"};
    /// One item of a channel's chain as the views show it: a module or a modulator.
    struct ChainItem
    {
        te::Plugin* plugin = nullptr;
        Modulator* modulator = nullptr;
        [[nodiscard]] std::uint64_t id() const;
    };
    struct RackChain
    {
        juce::ValueTree mixer; // its mixer's PLUGININSTANCE: the chain's name, on and solo
        te::VolumeAndPanPlugin* volume = nullptr;
        std::vector<te::Plugin*> modules;
        /// Its modulators (modulators.md §14), and everything in it in order as the views show it.
        std::vector<Modulator*> modulators;
        std::vector<ChainItem> items;
        [[nodiscard]] bool on() const { return static_cast<bool>(mixer.getProperty(chainOnId, true)); }
        [[nodiscard]] bool solo() const { return static_cast<bool>(mixer.getProperty(chainSoloId, false)); }
    };
    /// A rack instance's type (null for any other plugin).
    [[nodiscard]] static te::RackType* rackTypeOf(const te::Plugin* plugin);
    /// A rack's chains in order, each its modules in order and its mixer.
    [[nodiscard]] static std::vector<RackChain> rackChains(const te::RackType& rack);
    /// Every module in a rack, chain by chain (not the chains' mixers).
    [[nodiscard]] static std::vector<te::Plugin*> rackModules(const te::RackType& rack);
    /// Connects a rack as its chains say: its input to each chain's first module, module to
    /// module, each chain's mixer to its output while the chain is on (and soloed, if any is).
    /// A modulator taps the chain where it sits: what comes there (notes, sound) goes into it too.
    static void wireRack(te::RackType& rack, juce::UndoManager* um);
    /// Puts a rack's chain in `order` (its modules and modulators, all of them), as arrangeChain
    /// does a track's: the modules in that order, each modulator placed before the module after it.
    void arrangeRackChain(te::RackType& rack, int chain, const std::vector<ChainItem>& order);
    /// The racks in the song, as their instances play them.
    [[nodiscard]] std::vector<te::RackType*> rackTypes() const;
    /// The rack a modulator is in (null: a track's chain).
    [[nodiscard]] te::RackType* rackOf(const Modulator& modulator) const;
    /// A rack type's instance in the song (racks have one each).
    [[nodiscard]] te::RackInstance* instanceOf(const te::RackType& rack) const;
    /// Puts a modulator in a rack's chain, before the item now at `index` of its items (-1: the
    /// end), from wherever it is; its routes leaving the rack go (§14). How many went.
    int putModulatorInRack(te::RackType& rack, Modulator& modulator, int chain, int index);
    /// Takes a modulator out of its rack into its track's chain at `index` (-1: the end).
    void takeModulatorOutOfRack(Modulator& modulator, te::Track& track, int index);
    /// Whether a parameter is in a rack: a module's in it, or its macros and Morph.
    [[nodiscard]] bool inRack(const te::RackType& rack, const te::AutomatableParameter& param) const;
    /// A modulator's slot as the snapshot lists it.
    [[nodiscard]] model::DeviceSlot modulatorSlot(const Modulator& modulator) const;
    /// The rack instance with this id, or holding this module (null if none).
    [[nodiscard]] te::RackInstance* rackInstance(std::uint64_t id) const;
    [[nodiscard]] te::RackInstance* rackHolding(const te::Plugin& module) const;
    /// The track or return a module plays on, through its rack if it's in one (null: the master).
    [[nodiscard]] te::AudioTrack* trackOf(const te::Plugin& module) const;
    /// An instrument, or a rack with one at the start of a chain (racks.md §3). tracktion's racks
    /// say they're synths whatever they hold.
    [[nodiscard]] static bool playsNotes(te::Plugin& plugin);
    /// A new rack type named `name`, its eight macros made, and its first chain.
    te::RackType::Ptr makeRack(const juce::String& name);
    /// A new chain at the end of a rack: its mixer, passing what comes in.
    void addChain(te::RackType& rack);
    /// Puts a module into a rack's chain at `index` (-1: its end), taking it from where it was.
    bool putInRack(te::RackType& rack, const te::Plugin::Ptr& module, int chain, int index);
    /// A rack instance and its type go; their modules' lanes and mappings with them.
    void removeRack(te::RackInstance& instance);
    /// A module's mappings from the macros of `rack` go (it leaves it), each parameter keeping its
    /// value now.
    void unmapFrom(te::Plugin& module, const te::RackType& rack);
    /// Removes the lane assignments of a module's parameters (it's going).
    void dropLanesOf(std::uint64_t module);
    /// A track's state copied (its ids remapped into `ids`): each rack in it gets a copy of its
    /// rack of its own, with new ids (also into `ids`), which it then refers to.
    void copyRacksIn(juce::ValueTree trackCopy, te::EditItemID::IDMap& ids);
    // Step 2 (racks.md §18): a rack's snapshots (JSON), the current one and Morph's on, A and B are
    // properties of its type; a macro's lock is on its own state; Morph is one more macro of the
    // type (fdMorph), mapped to each macro it moves, From its place in A by B - A (syncMorph).
    static inline const juce::Identifier snapshotsId{"fdSnapshots"}, snapshotId{"fdSnapshot"},
        morphOnId{"fdMorphOn"}, morphAId{"fdMorphA"}, morphBId{"fdMorphB"}, morphId{"fdMorph"},
        lockedId{"fdLocked"};
    [[nodiscard]] static std::vector<model::RackInfo::Snapshot> snapshotsOf(const te::RackType& rack);
    static void setSnapshots(te::RackType& rack, const std::vector<model::RackInfo::Snapshot>& snapshots,
                             juce::UndoManager* um);
    /// The rack's Morph: null for a rack from before step 2 until ensureMorph makes it.
    [[nodiscard]] static te::MacroParameter* morphOf(const te::RackType& rack);
    te::MacroParameter* ensureMorph(te::RackType& rack);
    /// Snapshots, Morph and the macros' locks, positions and lanes, as the model has them.
    static void fillStep2(const te::RackType& rack, model::RackInfo& info);
    /// Where a macro is now: Morph's place for it while Morph sets it, its lane's while it
    /// follows one, else its own.
    [[nodiscard]] static float macroPosition(const te::RackType& rack, int macro);
    /// Morph's mappings onto the macros made to agree with the rack's state: each macro it sets
    /// mapped From A To B; one it no longer sets left where Morph had it. Changes nothing when
    /// they agree, so it's run whenever something they depend on may have changed.
    void syncMorph(te::RackType& rack);
    void syncMorphs();
    /// The parameters the macros move, where the macros now put them: the engine moves them only
    /// while it plays, and not at all for a macro Morph moves.
    void refreshMacroTargets(te::RackType& rack);
    bool morphCheckPending = false; // a lane came or went: a macro may now follow one
    /// The rack's info for the snapshot.
    [[nodiscard]] std::shared_ptr<const model::RackInfo>
    rackInfo(te::RackInstance& instance, const std::function<model::DeviceSlot(te::Plugin&)>& slotOf) const;
    Session::MacroPointer macroPointer;

    // ---- modulators (docs/design/modulators.md, ADR 0061) --------------------------------------
    // A modulator is an engine::Modulator in its track's modifier list, processed in the chain just
    // before the plugin it's placed before (fdBefore): the chain's order is the plugins' with each
    // modulator before the plugin it names, and those placed before the mixer (or nothing) after
    // the last module. A route is the modulator's assignment on a parameter, its value the amount.
    /// A track's chain with its modulators where they sit.
    [[nodiscard]] static std::vector<ChainItem> chainItems(te::Track& track);
    /// Puts a track's chain in `order` (its modules and modulators, all of them): the modules in
    /// that order before the mixer's plugins, each modulator placed before the plugin after it
    /// (the mixer, or nothing, after the last module), the modulators in that order. One step.
    void arrangeChain(te::Track& track, const std::vector<ChainItem>& order);
    /// Puts `item` at `index` of its track's chain as the views count it (its modules and
    /// modulators; -1 or past the end: the end), the rest staying in their order. One step.
    void placeInChain(te::Track& track, const ChainItem& item, int index);
    /// Where a module or modulator is along its track's chain as the views count it; -1 if it
    /// isn't there (in a rack, or on no track).
    [[nodiscard]] static int chainIndexOf(te::Track& track, std::uint64_t id);
    /// A modulator by id, on any track.
    [[nodiscard]] Modulator* modulator(std::uint64_t id) const;
    /// Every modulator in the project.
    [[nodiscard]] std::vector<Modulator*> modulators() const;
    /// The track a modulator is on (the master's track for the master's).
    [[nodiscard]] te::Track* trackOf(const Modulator& modulator) const;
    /// The tracks whose chains can hold modulators: the tracks, the returns and the master.
    [[nodiscard]] std::vector<te::Track*> chainTracks() const;
    /// A channel's track for its chain with modulators (the master's track for the master).
    [[nodiscard]] te::Track* chainTrack(std::uint64_t channel) const;
    /// The track whose chain holds a plugin (the master's track for the master's), not in a rack.
    [[nodiscard]] te::Track* chainTrackOf(te::Plugin& plugin) const;
    /// A track's modules: the master track's are the Edit's master plugins.
    [[nodiscard]] static te::PluginList& chainList(te::Track& track);
    /// A modulator's routes: each parameter it moves and its assignment there.
    struct Route
    {
        Param param;
        te::AutomatableParameter::ModifierAssignment::Ptr assignment;
    };
    [[nodiscard]] std::vector<Route> routesOf(const Modulator& modulator) const;
    /// Whether routing `from` to `param` would make a loop: `param` is one of `from`'s own, or of a
    /// modulator that moves `from`, however far round (modulators.md §8).
    [[nodiscard]] bool wouldLoop(const Modulator& from, const te::AutomatableParameter& param) const;
    /// Whether a modulator may move a parameter on its track: no loop, and in a rack only what's
    /// in the rack (§14).
    [[nodiscard]] bool canMove(const Modulator& from, const te::AutomatableParameter& param) const;
    /// Each modulator's copies (Per Target) counted from its routes: after routes change, a load,
    /// undo or redo.
    void refreshModulatorRoutes();
    /// The modulators that move a parameter, and the range they move it over now (normalised).
    struct Moved
    {
        float from = 0.0f, to = 0.0f;
        std::vector<Modulator*> by;
    };
    [[nodiscard]] std::optional<Moved> movedBy(te::AutomatableParameter& param) const;
    /// Playback starting: every modulator and clip that doesn't keep a roll rolls again (ADR 0062).
    void startPasses();
    /// Keeps a clip's chance as it was heard, or lets it roll (in the step under way).
    void keepChance(te::MidiClip& clip, bool keep);
    /// A module's sidechain picker (ADR 0025), or a Follower's Listen with `none` "Here".
    [[nodiscard]] static std::unique_ptr<ui::SidechainRouting>
    sidechainRouting(Session& session, std::uint64_t deviceId, std::string none);
    /// What the Edit's clips roll this pass (ClipChance.h).
    std::shared_ptr<ChancePass> chancePass;
    /// The modulators' info for the snapshot.
    [[nodiscard]] std::shared_ptr<const model::ModulatorInfo> modulatorInfo(const Modulator& modulator) const;
    Session::ModulatorPointer modulatorPointer;
    /// The modulator whose handle was hovered or dragged last: a ring's drag sets its route (§7.2).
    std::uint64_t lastModulator = 0;
    /// Routes the last move or grouping into a rack removed (Session::lastDroppedRoutes).
    int droppedRoutes = 0;
    /// A control's look while modulators move it or one is dragged over it (modulators.md §7.4).
    [[nodiscard]] std::optional<ui::ParamBinding::Modulated>
    modulatedLook(te::AutomatableParameter& param) const;
    [[nodiscard]] ui::ParamBinding::MacroDrag modulatorDragLook(te::AutomatableParameter& param) const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void recordingFinished(te::InputDeviceInstance& instance, te::EditItemID target,
                           const juce::ReferenceCountedArray<te::Clip>& clips) override;
    void recordingAboutToStart(te::InputDeviceInstance& instance, te::EditItemID target) override;
    void pluginParameterChanged(te::AutomatableParameter& param, bool isFollowingAutomation) override;
    void curveHasChanged(te::AutomatableParameter&) override {}
    void recordingStatusChanged(te::AutomatableParameter& param) override;
    void changed(juce::ValueTree& tree);
    /// A module turned on or off: its track's notes end, so none it held while off plays on
    /// (SessionNotes.cpp).
    void endNotesOnPowerChange(const juce::ValueTree& pluginState);
    void handleAsyncUpdate() override
    {
        applyInputs(); // tracks, inputs or instruments may have changed (undo too)
        if (std::exchange(morphCheckPending, false) && edit != nullptr)
            syncMorphs(); // a macro that now follows a lane is left alone by Morph (racks.md §18.4)
        if (std::exchange(restartPending, false) && edit != nullptr)
            edit->restartPlayback(); // fine-grained automation needs a new graph (automation.md §13)
        rebuildSnapshot();
    }
    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier& id) override
    {
        if (id == te::IDs::lastSignificantChange || id == panelViewId || id == te::IDs::automationMode) // bookkeeping and view state
            return;
        if (id == te::IDs::enabled && t.hasType(te::IDs::PLUGIN)) // undo and MCP too
            endNotesOnPowerChange(t);
        // A modulator's settings (modulators.md): saved, not the snapshot's (its knobs show them).
        // Its place and what it listens to change how it's processed (playback is rebuilt); its
        // roll and power show.
        if (t.hasType(Modulator::stateType))
        {
            dirty = true;
            if (id == Modulator::placeId || id == Modulator::listenId)
                restartPending = true;
            if (id == Modulator::placeId || id == Modulator::keptRollId || id == te::IDs::enabled ||
                id == Modulator::listenId)
                changed(t);
            return;
        }
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
            morphCheckPending = true;
            changed(t);
            return;
        }
        // A parameter playing its lane: tracktion writes each played value back (ADR 0028). A
        // rack's macro too (racks.md §4).
        if (t.hasType(te::IDs::PLUGIN))
            if (auto* param = parameterFor(t, id); param != nullptr && followsLane(*param))
                return;
        if (t.hasType(te::IDs::MACROPARAMETER) && id == te::IDs::value)
            if (auto* macro = macroFor(t); macro != nullptr && followsLane(*macro))
                return;
        if (!inTransport(t) && id != te::IDs::solo) // solo is a listening aid (ADR 0026)
            dirty = true; // includes device parameters
        // What a clip plays depends on its fades and its neighbours (ADR 0036), and its notes'
        // chance on its seed (piano-roll.md §10): playback is rebuilt.
        if (affectsFades(t, id) || id == noteIds::seed || id == chanceIds::keptRoll ||
            id == chanceIds::keptLap)
            restartPending = true;
        // A lane's points.
        for (auto p = t; p.isValid() && !p.hasType(te::IDs::PLUGIN); p = p.getParent())
            if (p.hasType(te::IDs::AUTOMATIONCURVE))
            {
                changed(t);
                return;
            }
        // A macro's mapping of a parameter (racks.md §4) shows.
        for (auto p = t; p.isValid() && !p.hasType(te::IDs::PLUGIN); p = p.getParent())
            if (p.hasType(te::IDs::MODIFIERASSIGNMENTS))
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
        morphCheckPending = morphCheckPending || inCurve(t) || child.hasType(te::IDs::AUTOMATIONCURVE);
        structureChanged(t);
    }
    void valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree& child, int) override
    {
        if (inInputs(t) || child.hasType(te::IDs::INPUTDEVICES))
            return;
        restartPending = restartPending || child.hasType(te::IDs::AUTOMATIONCURVE);
        morphCheckPending = morphCheckPending || inCurve(t) || child.hasType(te::IDs::AUTOMATIONCURVE);
        structureChanged(t);
    }
    /// Within a parameter's automation curve (its points).
    static bool inCurve(juce::ValueTree t)
    {
        for (; t.isValid() && !t.hasType(te::IDs::PLUGIN); t = t.getParent())
            if (t.hasType(te::IDs::AUTOMATIONCURVE))
                return true;
        return false;
    }
    void valueTreeChildOrderChanged(juce::ValueTree& t, int, int newIndex) override
    {
        // A module or a send moved in its chain: the engine rebuilds its graph only for one added or
        // removed, so the new order would be heard only after something else rebuilt it. Likewise
        // a modulator among its track's (the order they're processed in).
        restartPending = restartPending || t.getChild(newIndex).hasType(te::IDs::PLUGIN) ||
                         t.getChild(newIndex).hasType(Modulator::stateType);
        structureChanged(t);
    }
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
    static bool affectsFades(const juce::ValueTree& t, const juce::Identifier& id)
    {
        static const juce::Identifier edgeFades{"fdEdgeFades"};
        if (id == edgeFades)
            return true;
        if (!t.hasType(te::IDs::AUDIOCLIP))
            return false;
        static const std::array<juce::Identifier, 12> ids{te::IDs::fadeIn, te::IDs::fadeOut, te::IDs::mute, te::IDs::start, te::IDs::length,
                                                          te::IDs::offset, "fdFadeInBend", "fdFadeOutBend", "fdXLength", "fdXFrom",
                                                          te::IDs::loopStart, te::IDs::loopLength};
        static const juce::Identifier shape{"fdXShape"};
        return id == shape || std::find(ids.begin(), ids.end(), id) != ids.end();
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

/// A drag of the ring the track's modulators draw round a control (modulators.md §7.2): the
/// amount of one route, one undo step from start to end. The bindings of modules, racks' macros
/// and modulators share it.
class RouteDrag
{
public:
    /// The route of `target` the drag sets (Session::routeToDrag); false where none moves it.
    bool begin(Session& session, const std::optional<Session::ParamTarget>& target);
    /// `delta` of the control's travel: as much again of the route's amount.
    void drag(Session& session, float delta);
    void end();

private:
    std::optional<Session::EditGroup> group_;
    std::uint64_t modulator_ = 0;
    Session::ParamTarget target_;
    float amount_ = 0.0f;
};
} // namespace fd::engine
