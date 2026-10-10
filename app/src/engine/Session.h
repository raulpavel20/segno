#pragma once

#include "model/Bounce.h"
#include "model/Editing.h"
#include "model/Snapshot.h"

#include <fd_device_ui/fd_device_ui.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <functional>
#include <memory>

namespace tracktion::inline engine
{
class Engine;
class Edit;
}

namespace fd::engine
{
/// UI access to one of our devices in a chain (C6): the context its panel binds to. Edits go
/// through the engine (automation and undo work). Keep it no longer than the panel.
class DeviceHandle
{
public:
    virtual ~DeviceHandle() = default;
    [[nodiscard]] virtual ui::DeviceUiContext& context() = 0;
    [[nodiscard]] virtual std::string_view deviceId() const = 0;
};

/// Something that can be added to a chain: one of our devices or a scanned plugin.
struct AvailableDevice
{
    std::string key;      // "fd:eq" or "ext:<plugin identifier>"
    std::string name;
    std::string category; // "Instruments", "Audio effects", or the plugin's manufacturer
    bool native = true;
    bool instrument = false;
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
    /// Inserts `file` on `trackId` at `seconds`; returns the clip id, or 0 if the file is not
    /// readable audio.
    std::uint64_t insertAudioFile(std::uint64_t trackId, const juce::File& file, double seconds);

    /// Moves and trims clips (C4): each edit gives a clip its track and placement.
    void applyClipEdits(const std::vector<model::ClipEdit>& edits);
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
    /// Adds `key` (see AvailableDevice) to the track's chain at `index` (-1 = the end);
    /// returns the device id, 0 if it couldn't be created.
    std::uint64_t addDevice(std::uint64_t trackId, const std::string& key, int index = -1);
    void removeDevice(std::uint64_t deviceId);
    /// Moves a device to `index` within its chain.
    void moveDevice(std::uint64_t deviceId, int index);
    void setDeviceEnabled(std::uint64_t deviceId, bool enabled);

    /// Sidechains (A2, ADR 0025): host routing, never a parameter. The tracks that can feed a
    /// module's sidechain, in track order: every other track except those that already depend
    /// on the module's track through sidechains, so routing never loops. Empty if the module
    /// takes no sidechain.
    [[nodiscard]] std::vector<std::uint64_t> sidechainSources(std::uint64_t deviceId) const;
    /// Feeds a module's sidechain from a track (heard after its modules and fader, before its
    /// mute), or from none: it hears its input. One undo step; false if the module takes no
    /// sidechain or the track can't feed it.
    bool setSidechainSource(std::uint64_t deviceId, std::optional<std::uint64_t> trackId);

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
    /// Names a lane (empty: after its assignments). One undo step.
    void renameAutomationLane(std::uint64_t lane, const std::string& name);
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

    // ---- takes (C5b, ADRs 0031 and 0032, docs/design/recording.md §6, §8) --------------------
    // Every pass and loop lap is kept as a take of the clip it made; the clip is plain clips made
    // from them (several parts when it plays several takes), edited with its takes as one. Take
    // edits are undo steps; showing a clip's takes is view state.

    /// Opens (or closes) a clip's takes under it (by its stack, TakeStackInfo::id); opening one
    /// closes any it overlaps on its track. View state, never an undo step.
    void setTakesOpen(std::uint64_t stackId, bool open);
    /// Uses a take: the clip plays it across its range, cutting what's there (ADR 0019). One undo
    /// step, "Use take". False if there's no such take.
    bool useTake(std::uint64_t trackId, std::uint64_t takeId);
    /// The clip plays its next (+1) or previous (-1) take that covers its range (Alt+↓ / Alt+↑).
    bool stepTake(std::uint64_t clipId, int direction);
    void renameTake(std::uint64_t trackId, std::uint64_t takeId, const std::string& name);
    /// Deletes takes (their files stay): a clip playing one plays another of its takes. One undo step.
    void deleteTakes(std::uint64_t trackId, const std::vector<std::uint64_t>& takeIds);
    /// Deletes the takes the clips don't play.
    void deleteUnusedTakes(std::uint64_t trackId);
    /// The clip plays this take, and its other takes are deleted (files stay). One undo step.
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
        std::vector<std::string> cutTails; // clips whose audible tail a clip that isn't bounced cut
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

    /// A module panel's view state (collapsed sections, folded; ADR 0022), stored with the
    /// project on the module. Never an undo step or an unsaved change.
    [[nodiscard]] std::string deviceViewState(std::uint64_t deviceId) const;
    void setDeviceViewState(std::uint64_t deviceId, const std::string& json);
    /// The panel context of one of our devices; null for third-party plugins.
    [[nodiscard]] std::unique_ptr<DeviceHandle> openDevice(std::uint64_t deviceId);
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

    std::unique_ptr<Impl> impl_;
};
} // namespace fd::engine
