#pragma once

#include "model/AppSettings.h"
#include "model/AudioMeasure.h"
#include "model/Bounce.h"
#include "model/Browser.h"
#include "model/Editing.h"
#include "model/Export.h"
#include "model/Grid.h"
#include "model/Snapshot.h"

#include <array>
#include <fd/music/Detect.h>
#include <fd_device_ui/fd_device_ui.h>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <optional>
#include <set>

namespace tracktion::inline engine
{
class Engine;
class Edit;
}

namespace fd::engine
{
class PreviewPlayer;

/// UI access to one of our devices in a chain (C6): the context its panel binds to. Edits go
/// through the engine (automation and undo work). Keep it no longer than the panel.
class DeviceHandle
{
public:
    virtual ~DeviceHandle() = default;
    [[nodiscard]] virtual ui::DeviceUiContext& context() = 0;
    [[nodiscard]] virtual std::string_view deviceId() const = 0;
};

/// UI access to a modulator (docs/design/modulators.md): its panel's context (its parameters,
/// its Listen for a Follower) and what isn't a parameter (its routes, where it is, its roll).
class ModulatorHandle
{
public:
    virtual ~ModulatorHandle() = default;
    [[nodiscard]] virtual ui::DeviceUiContext& context() = 0;
    [[nodiscard]] virtual model::ModulatorHost& host() = 0;
    [[nodiscard]] virtual model::ModulatorKind kind() const = 0;
};

/// Something that can be added to a chain: one of our devices or a scanned plugin.
struct AvailableDevice
{
    std::string key;      // "fd:eq" or "ext:<plugin identifier>"
    std::string name;
    std::string category; // "Instruments", "Audio effects", "Modulators", or the plugin's manufacturer
    bool native = true;
    bool instrument = false;
    bool modulator = false; // "fd:mod-lfo": it moves parameters on its track (modulators.md, ADR 0061)
};

/// One open project (ADR 0018): owns tracktion's Edit, runs every command on it (undoable),
/// and publishes immutable snapshots for the views. The UI uses only this header, which has no
/// tracktion types. Message thread only.
class Session
{
public:
    explicit Session(tracktion::Engine& engine);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- snapshots ----------------------------------------------------------------------

    /// The latest snapshot (never null).
    [[nodiscard]] std::shared_ptr<const model::Snapshot> snapshot() const;
    /// Called after a new snapshot is published (coalesced after edits).
    std::function<void()> onSnapshotChanged;
    /// Rebuilds the snapshot now (tests; the UI waits for the coalesced update).
    void updateSnapshotNow();
    /// Publishes a pending snapshot now, if edits are waiting for one (tools that read right
    /// after editing).
    void flushSnapshot();

    /// Changes beyond the snapshot callback, for any number of listeners (the MCP server, the
    /// module chain). Message thread.
    class Listener
    {
    public:
        virtual ~Listener() = default;
        /// A new snapshot was published.
        virtual void snapshotChanged() {}
        /// A module's parameters or state changed (often, while a knob turns: coalesce).
        virtual void deviceStateChanged(std::uint64_t /*deviceId*/) {}
        /// A module's view state was set (setDeviceViewState).
        virtual void deviceViewChanged(std::uint64_t /*deviceId*/) {}
        /// An editor's grid was set (setGrid).
        virtual void gridChanged() {}
        /// The project was replaced (new or open).
        virtual void projectReplaced() {}
    };
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    // ---- transport ----------------------------------------------------------------------

    void play();
    void stop();
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double position() const; // seconds
    void setPosition(double seconds);

    [[nodiscard]] bool isLooping() const;
    void setLooping(bool on);
    /// Loop range in seconds (start, end); empty when never set.
    [[nodiscard]] std::pair<double, double> loopRange() const;
    void setLoopRange(double start, double end);

    [[nodiscard]] bool isMetronomeOn() const;
    void setMetronome(bool on);

    /// Tempo and time signature (each change is one undoable step).
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);

    // ---- editing (each call is one undoable step) --------------------------------------

    /// A new, empty track (any track takes audio and MIDI clips, ADR 0019), named "N Track"
    /// unless `name` is given.
    std::uint64_t addTrack(const std::string& name = {});
    /// Inserts a MIDI file as a clip at `seconds` (all of its tracks merged); returns the clip id.
    /// No instrument is added (ADR 0033): without one the clip is silent.
    std::uint64_t insertMidiFile(std::uint64_t trackId, const juce::File& file, double seconds);
    /// An empty MIDI clip on `trackId` over [seconds, seconds + lengthSeconds), named after the
    /// track; returns its id. No instrument is added, as above.
    std::uint64_t createMidiClip(std::uint64_t trackId, double seconds, double lengthSeconds);
    /// Replaces a MIDI clip's notes (C2, the piano roll). Notes are matched by value: the ones
    /// that stay are untouched; changed ones keep their other properties.
    void setNotes(std::uint64_t clipId, const std::vector<model::Note>& notes);
    /// Replaces a MIDI clip's controller lanes (piano-roll.md §11), each by its controller: its
    /// events become the lane's (an empty lane removes them). One undo step named `undoName`.
    void setControllers(std::uint64_t clipId, const std::vector<model::ControllerLane>& lanes,
                        const std::string& undoName = "Edit controllers");
    /// Re-roll (§10): a new seed for a MIDI clip's chance, `seed` or a random one; returns it (0:
    /// no such clip). One undo step; playback follows, undo too.
    std::uint32_t rerollChance(std::uint64_t clipId, std::optional<std::uint32_t> seed = std::nullopt);
    /// Keep chance as played (keep) or Let chance roll (§26, ADR 0062): the clip plays the roll it
    /// was heard with (the lap playing, or the last one) every time, or rolls again each pass.
    /// One undo step; false if nothing changed (no such MIDI clip, or already so).
    bool keepChance(std::uint64_t clipId, bool keep);
    /// Inserts `file` on `trackId` at `seconds`; returns the clip id, or 0 if the file is not
    /// readable audio.
    std::uint64_t insertAudioFile(std::uint64_t trackId, const juce::File& file, double seconds);
    /// An audio clip's sound mixed to mono, once through its source (not looped, at the source's
    /// own speed), from `from` to `to` seconds on the timeline within the clip (the whole clip
    /// without them): what Make Wavetable takes (docs/design/table-editor.md §5). Nothing for a
    /// MIDI clip or a source that can't be read.
    struct ClipSound
    {
        std::vector<float> samples;
        double sampleRate = 0.0;
        std::string name;
    };
    [[nodiscard]] std::optional<ClipSound> clipSound(std::uint64_t clipId, std::optional<double> from = {},
                                                     std::optional<double> to = {}) const;

    /// Moves and trims clips (C4): each edit gives a clip its track and placement (and a new loop,
    /// ADR 0048). One undo step named `undoName`.
    void applyClipEdits(const std::vector<model::ClipEdit>& edits, const std::string& undoName = "Move clips");
    /// Copies each `clip` to its placement; returns the new clip ids.
    std::vector<std::uint64_t> duplicateClips(const std::vector<model::ClipEdit>& placements);
    void deleteClips(const std::vector<std::uint64_t>& ids);
    /// Splits the clips that contain `seconds`; returns the ids of the new right-hand parts.
    std::vector<std::uint64_t> splitClips(const std::vector<std::uint64_t>& ids, double seconds);

    // ---- live MIDI input (C7) -----------------------------------------------------------

    /// Every MIDI input (devices and the computer keyboard) plays this track's instrument, live;
    /// nullopt routes them nowhere. Not an edit (no undo step).
    void setMidiInputTarget(std::optional<std::uint64_t> trackId);
    [[nodiscard]] std::optional<std::uint64_t> midiInputTarget() const;
    /// True while any MIDI input (a device or the computer keyboard) holds a note: the
    /// transport bar's activity light.
    [[nodiscard]] bool midiNotesHeld() const;
    /// The computer keyboard as a MIDI input: a note on (velocity > 0) or off. The piano roll
    /// previews notes through it too.
    void computerKeyboardNote(int note, float velocity);
    static constexpr const char* computerKeyboardName = "Computer keyboard";

    // ---- device chains (C6; each call is one undoable step) -----------------------------

    [[nodiscard]] std::vector<AvailableDevice> availableDevices() const;
    /// What one of ours is ("fd:eq", "fd:mod-lfo"): its parameters and presets; null for anything
    /// else.
    [[nodiscard]] static const DeviceInfo* nativeInfo(std::string_view key);
    /// Adds `key` (see AvailableDevice) to the track's chain at `index` (-1 = the end); a
    /// modulator's key adds it (addModulator). Returns the device id, 0 if it couldn't be created.
    std::uint64_t addDevice(std::uint64_t trackId, const std::string& key, int index = -1);
    void removeDevice(std::uint64_t deviceId);
    /// Moves a device to `index` within its chain.
    void moveDevice(std::uint64_t deviceId, int index);
    void setDeviceEnabled(std::uint64_t deviceId, bool enabled);
    /// How late a module plays its track (ms): a delay the engine lines the tracks up around (a
    /// linear-phase EQ's, a lookahead). 0 if it adds none.
    [[nodiscard]] double moduleLatencyMs(std::uint64_t deviceId) const;
    /// A module adding a delay you'd notice playing live, while the project hasn't been told of one
    /// (the DAW says so once per project, devices/eq/SPEC.md "Latency"): the first such module, its
    /// track and its delay. Nothing once told.
    struct DelayNotice
    {
        std::string module, track;
        double ms = 0.0;
    };
    [[nodiscard]] std::optional<DelayNotice> untoldDelay(double minMs) const;
    /// The project has been told of a delay: never again (saved with it, no undo step).
    void markDelayTold();

    /// Sidechains (A2, ADR 0025): host routing, never a parameter. The tracks that can feed a
    /// module's sidechain, in track order: every other track except those that already depend
    /// on the module's track through sidechains, so routing never loops. Empty if the module
    /// takes no sidechain.
    [[nodiscard]] std::vector<std::uint64_t> sidechainSources(std::uint64_t deviceId) const;
    /// Feeds a module's sidechain from a track (heard after its modules and fader, before its
    /// mute), or from none: it hears its input. One undo step; false if the module takes no
    /// sidechain or the track can't feed it.
    bool setSidechainSource(std::uint64_t deviceId, std::optional<std::uint64_t> trackId);

    // ---- racks (docs/design/racks.md, ADR 0056; each edit one undoable step) ------------------
    // A rack is a module holding chains of modules side by side, summed, each with a mixer row,
    // and eight macros. Modules in a rack keep their ids: every module call above reaches them.

    static constexpr int maxRackChains = 4; // the rows its panel's CHAINS has (racks.md §5a)
    /// Where a module goes: along a channel's chain, or a chain of a rack in it.
    struct ChainPlace
    {
        std::uint64_t channel = 0; // the channel (with a rack: its own, which may be left 0)
        std::uint64_t rack = 0;    // 0: the channel's own chain
        int chain = 0;             // the rack's chain
        /// In that chain, counted as a drag's gaps are, with a module being moved still in its
        /// place: 0 before the first, the count after the last; -1 its end.
        int index = -1;
    };
    /// Wraps modules of one channel's chain into a new rack, one chain holding them in their order,
    /// where the first of them was ("Group into rack"). Their automation, sidechains and settings
    /// stay with them. The rack's id; 0 if they can't be (none, not one chain's, a rack among them).
    std::uint64_t groupIntoRack(const std::vector<std::uint64_t>& modules);
    /// Puts the modules of a rack's shown chain back in its channel's chain where the rack was;
    /// its other chains and its macros' mappings go with it ("Ungroup rack"). The modules put back.
    std::vector<std::uint64_t> ungroupRack(std::uint64_t rack);
    /// An empty rack, one chain passing its input through, at `index` of a channel's chain.
    std::uint64_t addRack(std::uint64_t channel, int index = -1);
    /// Adds `key` (see AvailableDevice) at a place, a rack's chain too ("Add module"). Its id; 0 if
    /// it couldn't be made.
    std::uint64_t addDevice(const ChainPlace& place, const std::string& key);
    /// Moves a module within its channel: along a chain, into a rack's chain or out of one. A rack
    /// never goes into a rack. False if it can't.
    bool moveDevice(std::uint64_t deviceId, const ChainPlace& place);
    /// Where a module is: its channel, and its rack and chain if it's in one.
    [[nodiscard]] std::optional<ChainPlace> placeOf(std::uint64_t deviceId) const;
    /// A chain beside a rack's others, empty (it passes the rack's input through), shown in its
    /// frame. Its index; -1 at maxRackChains.
    int addRackChain(std::uint64_t rack);
    /// Removes a chain and its modules; a rack keeps one chain at least.
    bool removeRackChain(std::uint64_t rack, int chain);
    /// A chain's mixer row, each part set if given: its name, on, solo (heard alone while you set
    /// the rack up; an undo step, as it rewires the rack, and never kept in a rack's file), volume
    /// (dB, -inf .. +6) and pan (-1 .. 1).
    struct RackChainEdit
    {
        std::optional<std::string> name;
        std::optional<bool> on, solo;
        std::optional<float> volumeDb, pan;
    };
    bool setRackChain(std::uint64_t rack, int chain, const RackChainEdit& edit);
    /// The chain whose modules the rack's frame shows: view state, never an undo step.
    void showRackChain(std::uint64_t rack, int chain);
    /// Renames a rack ("Rename rack").
    bool renameRack(std::uint64_t rack, const std::string& name);
    /// A macro's position (0 .. 1) and name, each set if given.
    bool setMacro(std::uint64_t rack, int macro, std::optional<float> value,
                  std::optional<std::string> name = {});
    /// Maps a macro to a parameter of a module in its rack (racks.md §4): where its 0 and 100 %
    /// put the parameter (normalised; To below From turns it the other way) and its Curve (-1 ..
    /// 1, 0 straight). A parameter another macro set is taken from it. False if the module isn't
    /// in the rack or the parameter can't be automated.
    bool mapMacro(std::uint64_t rack, int macro, std::uint64_t module, const std::string& param, float from,
                  float to, float curve = 0.0f);
    /// The parameter is set by hand again, where its macro has it now.
    bool unmapMacro(std::uint64_t module, const std::string& param);

    // Snapshots, Morph, locks and Vary (racks.md §18; each one undo step). A snapshot is where the
    // eight macros are, nothing else; a saved rack is the whole rack.
    /// Stores where the macros are as a new snapshot ("Snapshot n"), or over snapshot `over`; it
    /// becomes the current one, and the first two become Morph's A and B. Its index; -1 if it
    /// can't (eight already, or no such snapshot).
    int storeSnapshot(std::uint64_t rack, std::optional<int> over = {});
    /// Puts the macros where a snapshot has them, all but locked ones and those following a lane;
    /// Morph turns off first. It becomes the current one.
    bool recallSnapshot(std::uint64_t rack, int snapshot);
    /// Removes a snapshot; removing Morph's A or B turns Morph off and clears that choice.
    bool removeSnapshot(std::uint64_t rack, int snapshot);
    /// A snapshot's name, or its eight positions (0 .. 1), set directly.
    bool setSnapshot(std::uint64_t rack, int snapshot, std::optional<std::string> name,
                     std::optional<std::array<float, 8>> macros = {});
    /// Morph (racks.md §18.3): on or off, the snapshots it goes between, and its position (0 ..
    /// 1, the rack's parameter `morph`). On needs A and B. Each set if given.
    struct MorphEdit
    {
        std::optional<bool> on;
        std::optional<int> a, b;
        std::optional<float> position;
    };
    bool setMorph(std::uint64_t rack, const MorphEdit& edit);
    /// Locks a macro, so recall, Morph and Vary leave it alone, or unlocks it.
    bool lockMacro(std::uint64_t rack, int macro, bool locked);
    /// Vary (racks.md §18.5): each mapped macro that isn't held moves from where it is by a random
    /// amount up to ±`amount` of its travel. False while Morph is on, or when nothing can move.
    bool varyRack(std::uint64_t rack, float amount);
    static constexpr float varyAmount = 0.15f, varyMoreAmount = 0.4f; // Vary, Vary more
    /// A rack's panel context: its eight macros as parameters "macro1" .. "macro8", 0 .. 100 %,
    /// automated like any module's. Null if it isn't a rack.
    [[nodiscard]] std::unique_ptr<DeviceHandle> openRack(std::uint64_t rack);
    /// The rack's macros as a module's parameters (the panel's DeviceInfo).
    [[nodiscard]] static const DeviceInfo& rackMacros();
    /// What the pointer shows of a rack's macros (racks.md §5a), for the knobs of the modules in
    /// it: a macro hovered lights the knobs it sets and dims the other macros' rings; one dragged
    /// marks the knobs that can take it, and lights the one under the pointer. Never saved.
    struct MacroPointer
    {
        std::uint64_t rack = 0;
        int macro = -1;
        bool dragging = false;
        std::uint64_t module = 0; // the knob under a dragged macro
        std::string param;
        bool operator==(const MacroPointer&) const = default;
    };
    void setMacroPointer(const MacroPointer& pointer);
    [[nodiscard]] const MacroPointer& macroPointer() const;
    /// A control's menu in a rack: the app adds Map to macro and Unmap (racks.md §4).
    std::function<void(std::uint64_t module, const std::string& param, juce::PopupMenu& menu)> macroMenu;

    // Rack files (racks.md §7, docs/ids.md): apart from module presets, which never hold a rack.
    static constexpr const char* rackFileExtension = ".segnorack";
    /// Your racks: the User library's Racks folder (Documents/Segno/Racks).
    [[nodiscard]] juce::File racksFolder() const;
    /// Writes a rack as a file: its chains with their mixer rows and their modules' states (ours as
    /// a preset holds them, plugins' their own), its macros and mappings; never automation,
    /// routing, solo or view state. False, with why in `error`, if it can't.
    bool saveRack(std::uint64_t rack, const juce::File& file, std::string* error = nullptr);
    /// A rack file put at a place in a channel's chain (an instrument rack too: the browser
    /// replaces the track's instrument with it). Plugins not installed are left out and named in
    /// `missing`; a module newer than this Segno refuses it whole, named in `error`. One undo step.
    struct LoadedRack
    {
        std::uint64_t rack = 0;
        std::vector<std::string> missing;
        std::string error;
    };
    LoadedRack loadRack(const juce::File& file, const ChainPlace& place);
    /// What a rack file says of itself, without loading it (the browser): nullopt if it isn't one.
    struct RackFileInfo
    {
        std::string name;
        bool instrument = false;
        std::vector<std::string> tags;
        std::string description, author;
    };
    [[nodiscard]] static std::optional<RackFileInfo> readRackFile(const juce::File& file);

    // ---- mixer (C3, ADR 0026, docs/design/mixer.md) -----------------------------------------
    // A channel is a track, a return track or the master (the snapshot lists them apart). Each
    // change is one undo step unless an EditGroup is open (a drag is one gesture), except solo.

    /// Adds a return track after the others ("A Return", "B Return"..., or `name`); every track
    /// can then send to it. Returns its id.
    std::uint64_t addReturnTrack(const std::string& name = {});
    /// Volume, -inf (minusInfinityDb or less) .. +6 dB, clamped. Any channel. "Volume".
    void setVolume(std::uint64_t channelId, float db);
    /// Pan, -1 (left) .. 1 (right): a balance, 0 dB at centre. Any channel. "Pan".
    void setPan(std::uint64_t channelId, float pan);
    /// Mute: tracks and returns. "Mute".
    void setMute(std::uint64_t channelId, bool mute);
    /// Solo in place: tracks and returns; `exclusive` clears every other solo first. Never an
    /// undo step, and never an unsaved change on its own.
    void setSolo(std::uint64_t channelId, bool solo, bool exclusive);
    /// A track's send to a return: its level (-inf .. 0 dB, clamped) and whether it is taken
    /// before the volume. False if either isn't a track or a return. "Send".
    bool setSend(std::uint64_t trackId, std::uint64_t returnId, std::optional<float> levelDb,
                 std::optional<bool> preFader);

    /// A channel's level meter: its peaks after volume and pan, silent while it isn't heard.
    /// Each reader has its own peaks, so one view never takes another's. Message thread.
    class LevelReader
    {
    public:
        virtual ~LevelReader() = default;
        /// The loudest peak of each channel (left, right) since the last call, linear.
        [[nodiscard]] virtual std::array<float, 2> takePeaks() = 0;
    };
    /// Null for an unknown channel.
    [[nodiscard]] std::unique_ptr<LevelReader> levelReader(std::uint64_t channelId);
    // ---- automation (C4, ADRs 0028 and 0029, docs/design/automation.md) ----------------------
    // An automation lane is a row of its own among the tracks (model::AutomationLaneInfo): a curve
    // in 0..1 that drives the parameters assigned to it, each through its own range of the
    // control's travel. A parameter is on at most one lane. Times are seconds. Lane edits are undo
    // steps (their parameters follow in the same step); an assignment's on switch is a listening
    // state (saved, never undoable).

    /// A parameter that can be automated: a channel and the parameter's key there ("volume",
    /// "pan", "send/<return id>", "m<module id>/<parameter id>").
    struct ParamTarget
    {
        std::uint64_t channel = 0; // a track, a return track or the master
        std::string key;

        friend bool operator==(const ParamTarget&, const ParamTarget&) = default;
    };
    /// A parameter that can be assigned (the parameter picker).
    struct ParamChoice
    {
        std::string key, name, module, valueText;
        std::uint64_t lane = 0; // the lane it's on (0: none)
    };
    /// Every parameter of a channel that can be automated, the mixer's first, then each module's.
    [[nodiscard]] std::vector<ParamChoice> automatableParameters(std::uint64_t channelId) const;

    /// A new lane, added like a track: at the end of the tracks, with the next palette colour (the
    /// same rotation as new tracks). With `first`, it drives that parameter over its full range
    /// (moved from its lane, if on one, bringing its points). One undo step, "Add automation lane".
    /// Returns its id; 0 if `first` can't be automated.
    std::uint64_t addAutomationLane(const std::optional<ParamTarget>& first = std::nullopt);
    /// The same, placed after `after` (a track, or a lane: right after it; 0: at the top; a return
    /// or the master: after the last track). For assistants.
    std::uint64_t addAutomationLaneAfter(std::uint64_t after, const std::optional<ParamTarget>& first = std::nullopt);
    /// Removes a lane; its parameters keep their values now. One undo step.
    void removeAutomationLane(std::uint64_t lane);
    /// Names a lane (trimmed; never empty). One undo step. False: no such lane, an empty name or
    /// the name it has.
    bool renameAutomationLane(std::uint64_t lane, const std::string& name);
    /// Replaces a lane's points: all of them, or those in [from, to] when `range` is given; its
    /// parameters follow. One undo step named `undoName`, unless an EditGroup names it. False if
    /// there's no such lane.
    bool setLanePoints(std::uint64_t lane, const model::AutomationPoints& points,
                       std::optional<std::pair<double, double>> range = std::nullopt,
                       const std::string& undoName = "Edit automation");
    /// Assigns a parameter to a lane through `range` (moving it from its lane, if on another).
    /// One undo step, "Assign parameter". False if there's no such lane or it can't be automated.
    bool assign(std::uint64_t lane, const ParamTarget& target, model::AssignmentRange range = {});
    /// Takes a parameter off its lane; it keeps its value now. One undo step.
    void unassign(const ParamTarget& target);
    /// An assignment's range. One undo step, "Assignment range".
    void setAssignmentRange(const ParamTarget& target, model::AssignmentRange range);
    /// The lane a parameter is on.
    [[nodiscard]] std::optional<std::uint64_t> laneOf(const ParamTarget& target) const;
    /// A new lane for a parameter (at the end of the tracks), moving it there; one undo step. 0 if
    /// it can't be automated.
    std::uint64_t automateInNewLane(const ParamTarget& target);

    /// On: the assignment plays. Off: the parameter keeps the value set by hand. Moving an
    /// assigned control turns its assignment off. Saved, never an undo step or an unsaved change.
    void setAssignmentOn(const ParamTarget& target, bool on);
    /// Every assignment of a lane.
    void setLaneOn(std::uint64_t lane, bool on);
    /// Turns every assignment back on.
    void setAllLanesOn();
    /// True while any assignment of a lane with points is off (the transport bar's button lights).
    [[nodiscard]] bool anyLaneOff() const;

    /// A lane's value now: its curve at the playhead, or without points its first assignment's
    /// value mapped back through its range; nullopt if neither.
    [[nodiscard]] std::optional<float> laneValue(std::uint64_t lane) const;
    /// A lane value as its one assignment's control shows it ("1.20 kHz"), else a percentage.
    [[nodiscard]] std::string laneText(std::uint64_t lane, float value) const;
    [[nodiscard]] std::optional<float> laneFromText(std::uint64_t lane, const std::string& text) const;

    /// Sets a parameter by hand, in its control's travel (0..1), as its control would: one undo
    /// step named after the parameter (unless a group is open). Its assignment turns off (§7).
    void setParamValue(const ParamTarget& target, float travel);
    /// The parameter's value now (played or set by hand), in its control's travel.
    [[nodiscard]] std::optional<float> paramValue(const ParamTarget& target) const;
    /// A travel as the control shows it ("1.20 kHz", "-6.0 dB", "25L").
    [[nodiscard]] std::string paramText(const ParamTarget& target, float travel) const;
    /// A travel as a plain value in the parameter's unit (dB, Hz, -1..1 for pan; 0..1 for a
    /// third-party plugin's parameter), and back (clamped). For assistants (ADR 0021).
    [[nodiscard]] std::optional<double> paramPlain(const ParamTarget& target, float travel) const;
    [[nodiscard]] std::optional<float> paramFromPlain(const ParamTarget& target, double plain) const;
    [[nodiscard]] std::optional<float> paramFromText(const ParamTarget& target, const std::string& text) const;
    /// A parameter's name and module ("Cutoff", "Synth"); empty if unknown.
    [[nodiscard]] std::pair<std::string, std::string> paramName(const ParamTarget& target) const;

    /// Automation arm (§8): while armed and playing, moving a control writes its lane (touch).
    /// Each pass is one undo step, "Record automation".
    void setAutomationArmed(bool armed);
    [[nodiscard]] bool isAutomationArmed() const;
    /// The parameter the user moved last, if it can be automated.
    [[nodiscard]] std::optional<ParamTarget> lastTouchedParameter() const;
    /// A module's parameter (by the module's id and the parameter's id).
    [[nodiscard]] std::optional<ParamTarget> moduleParameter(std::uint64_t deviceId, const std::string& paramId) const;
    /// Asks the views to bring a lane into sight and select it (a control's "Show automation
    /// lane"; onLaneRevealed).
    void revealLane(std::uint64_t lane);
    std::function<void(std::uint64_t lane)> onLaneRevealed;
    /// Adds a parameter's automation items to a control's menu (set by the UI, which owns the
    /// menu's wording; module panels reach it through their binding).
    std::function<void(const ParamTarget& target, juce::PopupMenu& menu)> automationMenu;

    /// A gesture on a channel's mix parameter (a drag of a volume, pan or send control): one undo
    /// step named `name` and, while recording, a touch on its parameter.
    class ParamGesture
    {
    public:
        ParamGesture(Session& session, const ParamTarget& target, const std::string& name);
        ~ParamGesture();
        ParamGesture(const ParamGesture&) = delete;
        ParamGesture& operator=(const ParamGesture&) = delete;

    private:
        Session& session_;
        ParamTarget target_;
        bool touching_ = false;
    };

    // ---- modulators (docs/design/modulators.md, ADR 0061; each edit one undoable step) ---------
    // A modulator sits in a track's chain as a module does: the snapshot lists it among them (a
    // DeviceSlot of kind Modulator), and its place counts as theirs for the module calls above
    // (addDevice and moveDevice indices). Its id reaches removeDevice, moveDevice and
    // setDeviceEnabled. It moves parameters on its own track: its routes.

    /// Adds a modulator at `index` of a channel's chain (-1: its end): a track's, a return's or the
    /// master's. Its id; 0 where there's no such channel.
    std::uint64_t addModulator(std::uint64_t channel, model::ModulatorKind kind, int index = -1);
    /// Routes a modulator to a parameter on its track at `amount`: -2 ... +2 of its range (both
    /// ways, half of it each way at 1; below 0 the other way). A route already there takes the
    /// amount. False for another track's parameter, its own, or a loop (modulators.md §8).
    bool routeModulator(std::uint64_t modulator, const ParamTarget& target, float amount = 0.5f);
    /// A route's amount (-2 ... +2).
    bool setRouteAmount(std::uint64_t modulator, const ParamTarget& target, float amount);
    bool removeRoute(std::uint64_t modulator, const ParamTarget& target);
    /// All its routes go ("Remove routes").
    void removeRoutes(std::uint64_t modulator);
    /// Keep as played (keep) or Let it roll (ADR 0062). False for a modulator that rolls no dice.
    bool keepRoll(std::uint64_t modulator, bool keep);
    /// Keep all as played, or Let all roll: every modulator that rolls dice, and every clip with
    /// notes left to chance (piano-roll.md §26).
    void keepAllRolls(bool keep);
    /// Its panel's context and host; null if there's no such modulator.
    [[nodiscard]] std::unique_ptr<ModulatorHandle> openModulator(std::uint64_t modulator);
    /// What the pointer shows of the modulators (modulators.md §7.4), for every control on the
    /// track: a handle hovered dims the other modulators' rings; one dragged marks every control it
    /// can move, and lights the one under the pointer. Never saved.
    struct ModulatorPointer
    {
        std::uint64_t modulator = 0; // hovered or dragged; 0: none
        bool dragging = false;
        std::optional<ParamTarget> over; // the control under a dragged handle
        /// Dragging with no modulator: a new one from the browser (§14), onto this track.
        std::uint64_t channel = 0;
        bool operator==(const ModulatorPointer&) const = default;
    };
    void setModulatorPointer(const ModulatorPointer& pointer);
    [[nodiscard]] const ModulatorPointer& modulatorPointer() const;
    /// The routes the last move or grouping of modulators into a rack removed: those leaving the
    /// rack (§14). The views say so, with Undo.
    [[nodiscard]] int lastDroppedRoutes() const;
    /// Whether a modulator could move a parameter: on its track, not its own, no loop.
    [[nodiscard]] bool canRoute(std::uint64_t modulator, const ParamTarget& target) const;
    /// The route a drag of a control's ring sets (§7.2): the one of the modulator whose handle was
    /// hovered last, else the first that moves it. Its modulator and amount.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, float>> routeToDrag(const ParamTarget& target) const;
    /// A control's right-click (§15): the routes that move it in `menu`, the track's modulators in
    /// `with` (Modulate with ▸). The chain view adds them, as it adds a rack's macros (macroMenu).
    std::function<void(const ParamTarget& target, juce::PopupMenu& menu, juce::PopupMenu& with)>
        modulatorMenu;

    // ---- recording (C5, ADRs 0030 and 0031, docs/design/recording.md) -------------------------
    // A track listens to an audio input and a MIDI input, and records one kind at a time. Its
    // inputs are edits (one undo step each); arming is a listening state (saved, never an undo
    // step or an unsaved change). Recorded audio goes to the project folder's Recordings.

    /// An input a track can listen to.
    struct InputChoice
    {
        std::string id;   // what model::InputInfo stores: an audio input's name; "all", "keyboard" or a MIDI device's name
        std::string name; // as shown
    };
    [[nodiscard]] std::vector<InputChoice> audioInputs() const;
    [[nodiscard]] std::vector<InputChoice> midiInputs() const;
    /// A track's inputs ("" for none), MIDI channel (0: all), monitoring and what it records (nullopt:
    /// follows its inputs). Each one undo step, "Track input".
    void setAudioInput(std::uint64_t trackId, const std::string& input);
    void setMidiInput(std::uint64_t trackId, const std::string& input);
    void setMidiChannel(std::uint64_t trackId, int channel);
    void setMonitor(std::uint64_t trackId, model::Monitor monitor);
    void setRecordKind(std::uint64_t trackId, std::optional<model::RecordKind> kind);
    /// Arms a track by its arm button; `exclusive` disarms every other track first.
    void setArmed(std::uint64_t trackId, bool armed, bool exclusive);

    /// Record (§5): from stopped, plays from the playhead after the count-in and records the armed
    /// tracks (or, with none armed, the softly armed one); while playing, punches in; while
    /// recording, punches out and keeps playing. Each pass is one undo step, "Record".
    void record();
    /// Records as record() does, then stops playback when the playhead reaches `seconds` (an
    /// assistant's "record 8 bars").
    void recordUntil(double seconds);
    [[nodiscard]] bool isRecording() const;
    /// Where the current pass records from (seconds), until it ends: before it, the count-in.
    [[nodiscard]] std::optional<double> recordingFrom() const;
    /// A recording in progress (recording.md §5): its track, where it starts, and whether it's audio.
    struct LiveRecording
    {
        std::uint64_t track = 0;
        double start = 0.0;
        bool audio = false;
    };
    [[nodiscard]] std::vector<LiveRecording> liveRecordings() const;
    /// Draws an audio recording in progress, its first `seconds`, into `area`.
    void drawLiveWaveform(juce::Graphics& g, std::uint64_t track, juce::Rectangle<int> area, double seconds) const;
    /// The clips the last pass made, once it has ended.
    std::function<void(const std::vector<std::uint64_t>& clips)> onRecorded;
    /// The count-in before recording from stopped (a setting kept between projects).
    void setCountIn(model::CountIn countIn);
    [[nodiscard]] model::CountIn countIn() const;
    /// MIDI loop recording: each lap a take (off), or merged into one clip (on). Saved with the project.
    void setMidiOverdub(bool on);
    [[nodiscard]] bool midiOverdub() const;
    /// Manual recording offsets in ms (±500), added to the latency the interface reports.
    void setRecordingOffsets(double audioMs, double midiMs);
    [[nodiscard]] std::pair<double, double> recordingOffsets() const;
    /// Where recorded audio goes: `Recordings` in the project's folder; before the first save, in a
    /// temporary project folder under the unsaved root (ADR 0030), moved in when saved.
    [[nodiscard]] juce::File recordingsFolder() const;
    /// The folder unsaved projects record into (default: Documents/Segno/Unsaved).
    void setUnsavedRoot(const juce::File& folder);
    /// The file new recordings are written as (Settings, ADR 0045): on every audio input.
    void setRecordingFormat(model::RecordingFormat format, int bitDepth);
    [[nodiscard]] std::pair<model::RecordingFormat, int> recordingFormat() const;

    /// MIDI devices (Settings > MIDI): the keyboards and controllers connected, each on or off (an
    /// off one is ignored everywhere). The computer keyboard isn't one of them.
    struct MidiDevice
    {
        std::string name;
        bool enabled = true;
    };
    [[nodiscard]] std::vector<MidiDevice> midiDevices() const;
    void setMidiDeviceEnabled(const std::string& name, bool enabled);

    // ---- takes (C5b, ADRs 0031 and 0032, docs/design/recording.md §6, §8) --------------------
    // Every pass and loop lap is kept as a take of the clip it made; the clip is plain clips made
    // from them (several parts when it plays several takes), edited with its takes as one. Take
    // edits are undo steps; showing a clip's takes is view state.

    /// Opens (or closes) a clip's takes under it (by its stack, TakeStackInfo::id); opening one
    /// closes any it overlaps on its track. View state, never an undo step.
    void setTakesOpen(std::uint64_t stackId, bool open);
    /// Uses a take: the clip plays it alone across its range (ADR 0037), whichever take played
    /// before; where it has no audio, nothing plays. One undo step, "Use take". False if there's no
    /// such take.
    bool useTake(std::uint64_t trackId, std::uint64_t takeId);
    /// Auditions a take (ADR 0038, recording.md §7): plays it in place of its clip, across the
    /// clip's range, changing nothing, until stopAudition() or anything that changes the project (a
    /// command, undo, save, a render or bounce, recording). One take at a time. A listening aid:
    /// never an undo step or an unsaved change, never saved. False if there's no such take.
    bool auditionTake(std::uint64_t trackId, std::uint64_t takeId);
    void stopAudition();
    /// Comping (recording.md §7): the clip plays the take over [from, to] (either order, within its
    /// range; where the take has no audio, nothing changes), cutting what played there; new joins
    /// between takes get a 10 ms crossfade, and parts of one take that meet become one. One undo
    /// step, `undoName`. False if nothing changed.
    bool compTake(std::uint64_t trackId, std::uint64_t takeId, double from, double to, const std::string& undoName = "Comp");
    /// The next (+1) or previous (-1) take covering it, for the section of the clip at `at` (the
    /// first section when it's outside the clip; ADR 0038); a clip that isn't a comp is one section
    /// and plays the take alone (Alt+↓ / Alt+↑). One undo step, "Use take".
    bool stepTake(std::uint64_t clipId, int direction, std::optional<double> at = std::nullopt);
    void renameTake(std::uint64_t trackId, std::uint64_t takeId, const std::string& name);
    /// Deletes takes (their files stay): a clip playing one plays another of its takes. One undo step.
    void deleteTakes(std::uint64_t trackId, const std::vector<std::uint64_t>& takeIds);
    /// Deletes the takes the clips don't play.
    void deleteUnusedTakes(std::uint64_t trackId);
    /// The clip plays this take, and its other takes are deleted (files stay). One undo step. False,
    /// changing nothing, if there is no such take or nothing of it is in the clip's range.
    bool keepOnlyTake(std::uint64_t trackId, std::uint64_t takeId);
    /// New tracks after this one, copying its modules and mix, each playing one of the takes as a
    /// plain clip; `move` also takes them off this track. One undo step. Returns the new tracks.
    std::vector<std::uint64_t> takesToTracks(std::uint64_t trackId, const std::vector<std::uint64_t>& takeIds, bool move);

    /// Capture MIDI (§8): the last phrase played (since a pause of two bars; while playing, since
    /// it started; never what a capture or a pass used up) becomes a clip on each armed MIDI track
    /// (what its input took), else on `selected`, else on a new track; never with an instrument
    /// added (ADR 0033). Where it was played while playing, else at the playhead. One undo step.
    /// Returns the first clip made; 0 if there was nothing to capture.
    std::uint64_t captureMidi(std::optional<std::uint64_t> selected = std::nullopt);
    /// Whether there's a phrase to capture.
    [[nodiscard]] bool canCaptureMidi() const;
    /// Where a capture would go now (the button's tooltip).
    struct CaptureTarget
    {
        std::vector<std::uint64_t> tracks;
        bool newTrack = false;
    };
    [[nodiscard]] CaptureTarget captureTarget(std::optional<std::uint64_t> selected) const;

    // ---- bounce (C7, ADR 0035, docs/design/bounce.md) ------------------------------------------
    // Bounce in place renders a clip up to where it enters its track's chain (a MIDI clip through
    // its instrument) and keeps the source as a take; bounce to a new track renders the whole
    // chain before the fader onto a new track and mutes the source. Both render offline on a copy
    // of the project; bounced audio goes to the project folder's Bounces.

    class RenderJob; // a render in flight (below)
    /// Why `clips` can't be bounced in `mode`: the first refusal's text; empty if every clip can.
    [[nodiscard]] std::string bounceRefusal(const std::vector<std::uint64_t>& clips, model::BounceMode mode);
    struct BounceResult
    {
        std::vector<std::uint64_t> clips;  // the bounced clips: on their tracks, or on the new ones
        std::vector<std::uint64_t> tracks; // the new tracks (a bounce to a new track)
        /// A bounced clip whose audible tail a clip that isn't bounced cut (its name, the other's).
        struct CutTail
        {
            std::string clip, next;
        };
        std::vector<CutTail> cutTails;
        std::string error;                 // why nothing landed; empty on success
        bool cancelled = false;            // cancelled (Escape, cancel()): nothing to say
    };
    /// Bounces the clips (any part of a clip with takes means all of it; refused clips are left
    /// out). Playback and editing go on meanwhile. When it's done the result lands as one undo step,
    /// `undoName` or else "Bounce in place" / "Bounce to new track", unless a clip or a module it
    /// renders changed: then nothing changes and `error` says why. `done` runs on the message
    /// thread. Null (with `done` not called) if there's nothing to bounce.
    std::shared_ptr<RenderJob> bounce(const std::vector<std::uint64_t>& clips, model::BounceMode mode,
                                      std::function<void(const BounceResult&)> done, const std::string& undoName = {});
    /// A clip being bounced now: its bounce's progress (0..1) and how long it has been running.
    struct Bouncing
    {
        std::uint64_t clip = 0;
        float progress = 0.0f;
        double seconds = 0.0;
    };
    [[nodiscard]] std::vector<Bouncing> bouncing() const;
    /// Cancels every bounce in progress (Escape); false if there were none.
    bool cancelBounces();
    /// Mutes or unmutes clips, every part of each (ADR 0035). One undo step, "Mute clips" or
    /// "Unmute clips".
    void setClipsMuted(const std::vector<std::uint64_t>& clips, bool muted);
    /// The rate the engine runs at: bounces are written at it.
    [[nodiscard]] double sampleRate() const;
    /// Where bounced audio goes: `Bounces` in the project's folder (ADR 0030), as recordings.
    [[nodiscard]] juce::File bouncesFolder() const;

    // ---- export (C8, ADR 0039, docs/design/export.md) ------------------------------------------
    // The mix or stems of the song or the loop, rendered offline on a copy of the project in the
    // background, then measured, normalised, dithered and written as WAV, FLAC or AIFF.

    /// One exported file: where it is, whose it is (empty: the mix), and what it measures as written.
    struct ExportedFile
    {
        juce::File file;
        std::string track;
        model::AudioMeasurements measured;
        bool ceilingHeld = false; // normalising to a loudness was held by the true-peak ceiling
    };
    struct ExportResult
    {
        std::vector<ExportedFile> files;
        std::string error; // empty: done
        bool cancelled = false;
    };
    /// Why an export can't run now (empty: it can): nothing to export, no loop, no tracks, a
    /// folder that can't be made, or one already running. `tracks`: the stems for SelectedTracks.
    [[nodiscard]] std::string exportRefusal(const model::ExportSettings& settings, const std::vector<std::uint64_t>& tracks);
    /// Exports in the background (export.md §7): playback and editing go on, and the project as it
    /// was when it started is what's exported. A cancelled or failed export leaves no file. `done`
    /// runs on the message thread. Null, with `done` not called, when refused.
    std::shared_ptr<RenderJob> exportAudio(const model::ExportSettings& settings, const std::vector<std::uint64_t>& tracks,
                                           std::function<void(const ExportResult&)> done);
    [[nodiscard]] bool isExporting() const;
    /// Where exports go by default: `Exports/` in the project's folder (ADR 0030).
    [[nodiscard]] juce::File exportsFolder();
    /// The folder `settings` names: the default when empty, else absolute or relative to the
    /// project's folder.
    [[nodiscard]] juce::File exportFolder(const model::ExportSettings& settings);
    /// The project's name for its files ("Untitled" before the first save).
    [[nodiscard]] std::string projectName() const;
    /// The project's export settings (export.md §9): kept with it, never an undo step or an
    /// unsaved change. The panel and assistants export with them.
    [[nodiscard]] model::ExportSettings exportSettings() const;
    void setExportSettings(const model::ExportSettings& settings);

    // ---- fades and crossfades (C6, ADR 0036, docs/design/fades.md) ---------------------------
    // Fades belong to audio clips; a crossfade belongs to the join between two audio clips that
    // meet, stored on the earlier one, and each clip plays its spare audio past the join.

    /// Sets audio clips' fades (a clip with takes: its first part's fade-in, its last part's
    /// fade-out); nullopt leaves one as it is, a zero length removes it. One undo step, "Fades".
    void setFades(const std::vector<std::uint64_t>& clips, std::optional<model::Fade> in, std::optional<model::Fade> out);
    /// What crossfading one join came to.
    struct CrossfadeResult
    {
        std::uint64_t out = 0, in = 0;                // the clips before and after the join
        std::optional<model::Crossfade> crossfade; // as made (or kept)
        std::string why;                           // why it couldn't be made
    };
    /// Crossfades every join between the given audio clips that meet (Ctrl+Shift+X): a join gets a
    /// crossfade of `length` fitted to the spare audio, or keeps the one it has (taking `shape` if
    /// given). One undo step, "Crossfade". Empty if no two of them meet.
    std::vector<CrossfadeResult> crossfade(const std::vector<std::uint64_t>& clips, double length = model::kCrossfadeSeconds,
                                           std::optional<model::CrossfadeShape> shape = std::nullopt);
    /// Sets (fitted to the spare audio) or removes the crossfade at the end of `clip`, into the clip
    /// that starts there. One undo step named `undoName`. False if there's no such join or no spare
    /// audio for it.
    bool setCrossfade(std::uint64_t clip, std::optional<model::Crossfade> crossfade, const std::string& undoName = "Crossfade");
    /// The project's edge fades (fades.md §2). One undo step, "Edge fades".
    void setEdgeFades(bool on);
    [[nodiscard]] bool edgeFades() const;

    // ---- clip loops (C10, ADR 0048, docs/design/clip-loops.md) ------------------------------
    /// Loop on for `clips` (each loops what it shows, so nothing moves) or off (the repeats go),
    /// in one undo step, "Loop clips" or "Stop looping clips". A clip that can't loop is left as it
    /// is: the first one's reason is returned (empty if none was refused). A clip on a take stack
    /// with one take leaves it as it starts to loop (§8).
    std::string setClipsLooping(const std::vector<std::uint64_t>& clips, bool on);
    /// A clip's loop set to `loop` (the clip view's brace, the inspector, assistants), switching Loop
    /// on if it was off: placed as model::withLoop says, in one undo step named `undoName`. The
    /// reason if it can't loop (nothing changes), else empty.
    std::string setClipLoop(std::uint64_t clip, const model::ClipLoop& loop, const std::string& undoName = "Loop");

    /// A module panel's view state (collapsed sections, folded; ADR 0022), stored with the
    /// project on the module. Never an undo step or an unsaved change.
    [[nodiscard]] std::string deviceViewState(std::uint64_t deviceId) const;
    void setDeviceViewState(std::uint64_t deviceId, const std::string& json);
    /// The editors with a grid of their own (ADR 0047).
    enum class GridEditor
    {
        Arrangement,
        PianoRoll
    };
    /// An editor's grid: view state, kept with the project in the UI's view state; never an undo
    /// step or an unsaved change. A new or opened project starts at the defaults until the UI
    /// applies its saved view state.
    [[nodiscard]] model::GridSettings grid(GridEditor editor) const;
    void setGrid(GridEditor editor, const model::GridSettings& grid);
    /// The step the editor shows now ("1/16", "1/8T"; an adaptive grid's follows its zoom), as
    /// the UI publishes it for assistants; empty while it shows none.
    [[nodiscard]] std::string gridStep(GridEditor editor) const;
    void setGridStep(GridEditor editor, std::string step);

    /// The panel context of one of our devices; null for third-party plugins.
    [[nodiscard]] std::unique_ptr<DeviceHandle> openDevice(std::uint64_t deviceId);

    // ---- presets (M3 A1, ADR 0044, docs/design/presets.md) -------------------------------

    /// Every preset: the factory set and yours (Documents/Segno/Presets).
    [[nodiscard]] PresetLibrary& presetLibrary();
    /// Where yours live (tests use their own folder).
    void setPresetsFolder(const juce::File& folder);
    /// One of our modules, for presets (the Module menu, assistants); null for third-party
    /// plugins. Loading, reverting and A/B are each one undo step; what loads while browsing is
    /// one, and going back undoes it.
    [[nodiscard]] std::unique_ptr<PresetHost> presetHost(std::uint64_t deviceId);
    /// A third-party plugin's parameters (ours are read through openDevice()).
    struct PluginParameter
    {
        std::string id;
        std::string name;
        float value = 0.0f; // normalised, 0..1
        std::string text;   // the plugin's own display of the value
    };
    [[nodiscard]] std::vector<PluginParameter> pluginParameters(std::uint64_t deviceId) const;
    /// Sets a third-party plugin's parameter (normalised 0..1, clamped); false if it has no
    /// such parameter. Recorded like the user's edits.
    bool setPluginParameter(std::uint64_t deviceId, const std::string& parameterId, float value);
    /// Opens a third-party plugin's own editor window.
    void showPluginEditor(std::uint64_t deviceId);

    /// Scans the default VST3 (and LV2 on Linux) folders on a background thread. A plugin that
    /// crashes the scan is skipped next time (dead man's pedal). Callbacks run on the message
    /// thread.
    void scanPlugins(std::function<void(const juce::String& current)> onProgress,
                     std::function<void(int found)> onDone);
    [[nodiscard]] bool isScanning() const;
    /// Folders scanned besides the system's (Settings > Plugins).
    void setPluginFolders(std::vector<std::string> folders);
    [[nodiscard]] const std::vector<std::string>& pluginFolders() const;
    /// Plugins that crashed a scan, left out until tried again.
    [[nodiscard]] std::vector<std::string> skippedPlugins() const;
    void retrySkippedPlugin(const std::string& file);

    // ---- settings (C1, ADR 0045) ----------------------------------------------------------

    /// What a project's export starts from until it has its own (the last export's, Settings).
    void setExportDefaults(const model::ExportSettings& defaults);
    /// Linux, the first launch (no audio device chosen yet): JACK where a JACK server (or
    /// PipeWire's) runs, else the system's default, ALSA (settings.md §7, S11). Returns whether
    /// it chose JACK.
    bool preferAudioServerOnFirstLaunch();

    // ---- the browser (M3 C2, ADR 0046, docs/design/browser.md) -------------------------------

    /// Where the browser's index of files is kept (none, as in tests: in memory).
    void setBrowserIndexFile(const juce::File& file);
    /// Your places (§9). The project's folder and the Segno folder are indexed besides them.
    void setBrowserPlaces(const std::vector<std::string>& folders);
    [[nodiscard]] std::vector<juce::File> browserPlaces() const;
    /// Segno's Library (ADR 0055): what ships besides the modules, read-only, a place of its own in
    /// the browser; empty when it isn't installed.
    [[nodiscard]] juce::File libraryFolder() const;
    /// Where the Library is (Settings > Library); empty: where it's installed (fd::library).
    void setLibraryFolder(const juce::File& folder);
    /// The Segno folder (Settings > Library; Documents/Segno by default), with its Samples folder.
    [[nodiscard]] juce::File segnoFolder() const;
    void setSegnoFolder(const juce::File& folder);
    /// The open project's folder, if it has one on disk.
    [[nodiscard]] std::optional<juce::File> projectFolderOnDisk() const;
    /// The files of every place, as indexed so far.
    [[nodiscard]] const model::BrowserIndexFile& browserIndex() const;
    [[nodiscard]] bool isIndexing() const;
    /// Looks at the places again (or at the one holding `folder`).
    void refreshBrowser(const juce::File& folder = {});
    /// Called on the message thread when the index changed.
    std::function<void()> onBrowserIndexChanged;
    /// Waits for the index (tests).
    void waitForBrowserIndex();
    /// Everything the browser finds (§6): our modules, their presets (not Default settings),
    /// plugins, and the indexed files and their folders. `favourites`: the ids starred besides
    /// presets (whose stars are the preset library's).
    [[nodiscard]] std::vector<model::BrowserItem> browserItems(const std::set<std::string>& favourites = {});

    /// What a preview plays (§7).
    struct Preview
    {
        double seconds = 0.0, sampleRate = 0.0;
        int channels = 0;
        double tempo = 0.0;  // the file's, if known
        bool inTime = false; // a loop lined up with the song's bar, looping
    };
    /// Plays a sample: in time with the song if it's playing and the sample is a loop that fits
    /// its tempo (a whole number of beats, two or more), else once at its own speed.
    std::optional<Preview> previewFile(const juce::File& file);
    /// Plays a short phrase with one of our instruments' presets.
    bool previewPreset(const std::string& deviceId, const std::string& presetKey);
    /// An instrument rack file's phrase, through all its chains (racks.md §18.9). False for an
    /// effect rack, or one holding a plugin.
    bool previewRack(const juce::File& file);
    /// Plays a wavetable file on a held C3, through a Wavetable of its own (table-editor.md §7).
    bool previewWavetable(const juce::File& file);
    /// Plays an SFZ file's instrument: the presets' phrase (a C3) through a Sampler of its own
    /// (devices/sampler/SPEC.md, S11).
    bool previewMultisample(const juce::File& file);
    /// Plays a MIDI file on a track's instrument (0: the track live MIDI plays), as live MIDI at
    /// the song's tempo; false if that track has no instrument or the file has no notes.
    bool previewMidiFile(const juce::File& file, std::uint64_t trackId = 0);
    void stopPreview();
    [[nodiscard]] bool isPreviewing() const;
    [[nodiscard]] double previewPosition() const;
    [[nodiscard]] double previewLength() const;
    void setPreviewVolume(float db);
    /// The preview's output as the device gets it, after the engine's (tests).
    [[nodiscard]] PreviewPlayer& previewPlayer();

    /// Puts `key` in place of the track's instrument (the first, if it has one; else at the
    /// start of its chain), one undo step "Replace Sampler with Subtractive Synth". Returns the
    /// new module, 0 if it couldn't be made.
    std::uint64_t replaceInstrument(std::uint64_t trackId, const std::string& key);
    /// The track's instrument (its first), if it has one.
    [[nodiscard]] std::optional<model::DeviceSlot> instrumentOf(std::uint64_t trackId) const;
    /// What loading a sample or an SFZ file into a Sampler made.
    struct LoadedSample
    {
        double seconds = 0.0;       // the sample's length, a multisample's longest
        std::optional<int> rootKey; // a sample's, from its file
        int zones = 1;
        int missing = 0;                  // zones whose sample isn't there
        std::vector<std::string> ignored; // what the SFZ file says that isn't played
    };
    /// Loads a sample or an SFZ file into a Sampler, with what the file sets on its controls (a
    /// sample's root key; an SFZ's Mode, Loop, Velocity to Volume and Amp envelope), one undo
    /// step; nullopt if it isn't a Sampler or the file can't be played (`error` says why).
    std::optional<LoadedSample> loadSample(std::uint64_t deviceId, const juce::File& file,
                                           std::string* error = nullptr);
    /// Loads a wavetable file onto one of a Wavetable's oscillators (0-2), one undo step "Load
    /// <name>"; false if it isn't a Wavetable or the file isn't a table (`error` says why).
    bool loadWavetable(std::uint64_t deviceId, int oscillator, const juce::File& file,
                       std::string* error = nullptr);

    /// Copies every file the project uses from outside its folder into its Samples folder and
    /// points the clips and takes at the copies (§12); not an undo step. The project must have a
    /// file. Then save.
    struct Collected
    {
        int copied = 0;
        std::vector<std::string> failed;
    };
    Collected collectFiles();
    /// The files in the project's Recordings, Bounces and Samples that no clip or take uses, nor
    /// used since the project opened (so undo can't bring one back).
    [[nodiscard]] std::vector<juce::File> unusedFiles() const;
    /// Moves those of `files` that are unused to the system's trash (`remove`, tests: deletes);
    /// how many went.
    int removeUnusedFiles(const std::vector<juce::File>& files,
                          const std::function<bool(const juce::File&)>& remove = {});

    // ---- projects (C8) -------------------------------------------------------------------

    /// A new project: four audio tracks, no file.
    void newProject();
    /// Opens a project file; false (with `error`) if it can't be read.
    bool open(const juce::File& file, std::string* error = nullptr);
    /// Saves to `file` and remembers it; `save()` writes to the remembered file.
    bool saveAs(const juce::File& file);
    bool save();
    [[nodiscard]] std::optional<juce::File> file() const;
    [[nodiscard]] bool hasUnsavedChanges() const;
    static constexpr const char* fileExtension = ".segno";

    /// The UI's view state (JSON), stored with the project when saving.
    std::function<std::string()> viewStateProvider;
    /// The view state stored in the open project (empty if none).
    [[nodiscard]] std::string savedViewState() const;
    /// Before the project is replaced (new/open): release every DeviceHandle and panel.
    std::function<void()> onProjectWillChange;
    /// After the project was replaced: take the new snapshot and view state.
    std::function<void()> onProjectChanged;

    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    /// The step undo() or redo() would take back, by name ("Add track"); empty if unnamed.
    [[nodiscard]] std::string undoDescription() const;
    [[nodiscard]] std::string redoDescription() const;

    /// Makes the commands run while it exists one undo step named `name` ("Assistant: add
    /// notes"; ADR 0021: one assistant call, one step). Groups nest: the outermost names it.
    class EditGroup
    {
    public:
        EditGroup(Session& session, const std::string& name);
        ~EditGroup();
        EditGroup(const EditGroup&) = delete;
        EditGroup& operator=(const EditGroup&) = delete;

    private:
        Session& session_;
    };

    // ---- the song's sections (M3 C3, ADR 0049, docs/design/arranger.md) ----------------------

    /// Marks a section from `start` to `end` (seconds), named `name` (empty: the next letter) and
    /// coloured `colour` (none: the next colour). One undo step, "Add section". 0 if it would
    /// overlap another or is empty.
    std::uint64_t addSection(double start, double end, const std::string& name = {},
                             std::optional<int> colour = {});
    /// Renames a section (an empty name changes nothing). One undo step. False if nothing changed.
    bool renameSection(std::uint64_t section, const std::string& name);
    /// A section's colour, an index into the clip palette. One undo step.
    bool setSectionColour(std::uint64_t section, int colour);
    /// Moves a section's marks (its start and end), the music staying where it is. One undo step,
    /// "Move section mark". False if it would overlap another or be empty.
    bool setSectionMarks(std::uint64_t section, double start, double end);
    /// Moves sections (their span, from the first one's start to the last one's end) with their
    /// music to `to`, a section boundary outside the span: the rest of the song makes room
    /// (arranger.md §9). One undo step, "Move section <name>". False if nothing moves.
    bool moveSections(const std::vector<std::uint64_t>& sections, double to);
    /// Copies them with their music to `to`, the music after it moving later. One undo step,
    /// "Copy section <name>". Returns the copies' ids (empty if nothing was copied).
    std::vector<std::uint64_t> copySections(const std::vector<std::uint64_t>& sections, double to);
    /// Deletes sections with their music, the gap closing; with `keepMusic`, only their marks. One
    /// undo step, "Delete section <name>" or "Remove section mark". False if none was found.
    bool deleteSections(const std::vector<std::uint64_t>& sections, bool keepMusic = false);
    /// An empty section `length` long at `at`, the music after it moving later (named and
    /// coloured as addSection does). One undo step, "Insert section". Returns its id (0: none).
    std::uint64_t insertSection(double at, double length, const std::string& name = {});

    /// Sections copied for pasting (arranger.md §9): their span with its clips, takes and
    /// automation as they were, so a cut still pastes, into another project too. Empty when
    /// nothing was copied.
    class SectionClipboard
    {
    public:
        [[nodiscard]] bool empty() const noexcept;
        /// "Chorus", or "2 sections".
        [[nodiscard]] std::string name() const;

    private:
        friend class Session;
        struct Data;
        std::shared_ptr<const Data> data_;
    };
    /// Copies sections: their span, from the first one's start to the last one's end, with
    /// everything in it (clips crossing its edges, the part inside).
    [[nodiscard]] SectionClipboard copySectionsToClipboard(const std::vector<std::uint64_t>& sections);
    struct SectionPaste
    {
        std::vector<std::uint64_t> sections; // the pasted ones
        int rowsLeftOut = 0;                 // tracks' music with no track in its place
    };
    /// Pastes them at `at`, or at the nearer edge of a section `at` is inside
    /// (model::pasteLanding), the music after moving later; each track's music onto the track in
    /// its place. One undo step, "Paste section <name>".
    SectionPaste pasteSections(const SectionClipboard& clipboard, double at);

    // ---- the song's key (M3 P3, ADR 0057, docs/design/key-and-scale.md) ----------------------

    /// The song's key: one for the whole song, or none (std::nullopt). One undo step, "Set key to
    /// A minor" or "Remove the key"; nothing when it's already so. False for a scale this build
    /// doesn't know.
    bool setKey(const std::optional<music::Key>& key);
    /// While one exists (the Key popover is open), the key's changes are one undo step, named after
    /// the last; none if the key ends as it began (key-and-scale.md §6). Any other command in
    /// between closes the step.
    class KeyChoice
    {
    public:
        explicit KeyChoice(Session& session);
        ~KeyChoice();
        KeyChoice(const KeyChoice&) = delete;
        KeyChoice& operator=(const KeyChoice&) = delete;

    private:
        Session& session_;
    };
    /// Keys that fit the notes of `clips` (MIDI clips; none given: every MIDI clip that plays),
    /// best first, at most `count` (key-and-scale.md §11). Empty with too few notes to judge.
    [[nodiscard]] std::vector<music::KeyCandidate> detectKeys(const std::vector<std::uint64_t>& clips = {},
                                                              int count = 3) const;

    // ---- tracks and clips (C11, ADR 0041, docs/design/tracks-and-clips.md) --------------------

    /// Renames a track or return track (and its aux bus); an empty name changes nothing. One undo
    /// step, "Rename track". False if nothing changed.
    bool renameTrack(std::uint64_t channelId, const std::string& name);
    /// A track's or return track's colour, an index into the clip palette. One undo step.
    bool setTrackColour(std::uint64_t channelId, int colour);
    /// An automation lane's colour. One undo step.
    bool setLaneColour(std::uint64_t lane, int colour);
    /// Deletes a track or return track with what only made sense with it (§5): its clips and
    /// takes, the lanes that drive only it and its assignments in others (lanes after it then sit
    /// after the track above), the sidechains it feeds (back to Off), and for a return, the sends
    /// to it. One undo step. False for the master, or while recording (`why` says so).
    bool deleteTrack(std::uint64_t channelId, std::string* why = nullptr);
    /// Moves a track right above another track or a lane (0: after the last of them), or a
    /// return before another return (0: the last). Nothing else moves: the lanes that were after
    /// it stay where they are (ADR 0042). One undo step, "Move track". False if it doesn't move.
    bool moveTrack(std::uint64_t channelId, std::uint64_t before);
    /// Moves a lane right above a track or another lane (0: after the last of them); nothing else
    /// moves (ADR 0042). One undo step, "Move automation lane". False if it doesn't move.
    bool moveLane(std::uint64_t lane, std::uint64_t before);
    /// Duplicates a track or return right after it (§7): its clips and takes, modules, mix, sends,
    /// input and colour, named with the next free number, not armed or soloed; lanes that drive
    /// only it are copied for the copy. A return's copy listens to a bus of its own. Returns the
    /// copy (0: it can't, while recording or with every aux bus taken).
    std::uint64_t duplicateTrack(std::uint64_t channelId);
    /// Renames clips, all of them to `name` (empty: nothing). One undo step, "Rename clip".
    void renameClips(const std::vector<std::uint64_t>& clips, const std::string& name);
    /// Colours clips, and the takes they play from, so switching takes keeps it. A clip's colour
    /// is its own: its track's when it was made, independent after (ADR 0043). One undo step.
    void setClipColour(const std::vector<std::uint64_t>& clips, int colour);

    /// Clips copied for pasting (§9): copies of their state and of the takes they play from, so a
    /// cut still pastes. Empty when nothing was copied.
    class ClipClipboard
    {
    public:
        [[nodiscard]] bool empty() const noexcept;
        /// How many tracks' rows it spans.
        [[nodiscard]] int rows() const noexcept;

    private:
        friend class Session;
        struct Data;
        std::shared_ptr<const Data> data_;
    };
    /// The clips (with every part of a clip with takes), each on its row below the topmost track
    /// they're on, at its time after the earliest.
    [[nodiscard]] ClipClipboard copyClips(const std::vector<std::uint64_t>& clips) const;
    struct PasteResult
    {
        std::vector<std::uint64_t> clips; // the copies
        int rowsLeftOut = 0;              // rows past the last track
    };
    /// Pastes at `at` on a track, clips from several tracks on the tracks below it, cutting what
    /// they land on (ADR 0019); takes and fades come along. One undo step, "Paste".
    PasteResult pasteClips(const ClipClipboard& clipboard, std::uint64_t trackId, double at);

    // ---- offline rendering (the MCP render tool and bounce; export later) ------------------

    struct RenderRequest
    {
        double start = 0.0, end = 0.0;      // seconds
        std::optional<std::uint64_t> track; // one track through its chain; the mix when empty
        double sampleRate = 48000.0;
    };
    /// A render in flight.
    class RenderJob
    {
    public:
        virtual ~RenderJob() = default;
        [[nodiscard]] virtual float progress() const = 0; // 0..1, any thread
        virtual void cancel() = 0;                         // any thread
    };
    /// Renders a range to a stereo 32-bit float WAV, offline, on a copy of the project: the
    /// project, its undo history and its playback are untouched. `done` runs on the message
    /// thread with an empty string on success, or why it failed. Null (with `done` not called)
    /// if the request names nothing to render.
    std::shared_ptr<RenderJob> render(const RenderRequest& request, const juce::File& destination,
                                      std::function<void(const std::string& error)> done);

    // ---- engine ------------------------------------------------------------------------

    [[nodiscard]] juce::AudioDeviceManager& deviceManager();
    /// Engine CPU load 0..1.
    [[nodiscard]] double cpuUsage() const;

    struct Impl;
    /// For other engine-side code only (device chain, tests); the UI never uses it.
    [[nodiscard]] Impl& impl() noexcept { return *impl_; }

private:
    void replaceEdit(std::unique_ptr<tracktion::Edit> edit, std::optional<juce::File> file);
    // The clipboards' clips (SessionTracks.cpp).
    /// Every clip across `start` to `end`, whole, on its track's row (from the first track), its
    /// time from `from`.
    [[nodiscard]] ClipClipboard copyClipsAcross(double start, double end, double from) const;
    /// Puts a clipboard's clips on the tracks from `track` down, its time `from` at `at`, with the
    /// takes they play from; with `cutUnder`, they cut what they land on (ADR 0019).
    PasteResult placeClips(const ClipClipboard& clipboard, std::size_t track, double at, bool cutUnder);

    std::unique_ptr<Impl> impl_;
};
/// Applies what Settings keep for the engine (ADR 0045): the recording format, the Segno folder
/// (presets and unsaved projects), plugin folders and export defaults.
void applySettings(Session& session, const model::AppSettings& settings);
/// The Segno folder before Settings move it: Documents/Segno, or in tests the parent of their
/// presets folder (SEGNO_PRESETS_FOLDER), so they never touch yours.
[[nodiscard]] juce::File defaultSegnoFolder();
} // namespace fd::engine
