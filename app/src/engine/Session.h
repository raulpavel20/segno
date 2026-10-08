#pragma once

#include "model/Editing.h"
#include "model/Snapshot.h"

#include <fd_device_ui/fd_device_ui.h>
#include <juce_audio_devices/juce_audio_devices.h>

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

    /// A new, empty track (any track takes audio and MIDI clips, ADR 0019).
    std::uint64_t addTrack();
    /// A new track with the instrument `deviceKey` (see AvailableDevice) as its first device.
    std::uint64_t addInstrumentTrack(const std::string& deviceKey = "fd:subtractive-synth");
    /// Inserts a MIDI file as a clip at `seconds` (all of its tracks merged); returns the clip id.
    /// A track without an instrument gets the default one (`addedInstrument` says so).
    std::uint64_t insertMidiFile(std::uint64_t trackId, const juce::File& file, double seconds,
                                 bool* addedInstrument = nullptr);
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
    /// The computer keyboard as a MIDI input: a note on (velocity > 0) or off.
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
    /// The panel context of one of our devices; null for third-party plugins.
    [[nodiscard]] std::unique_ptr<DeviceHandle> openDevice(std::uint64_t deviceId);
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
    static constexpr const char* fileExtension = ".fdproj";

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
