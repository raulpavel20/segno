#pragma once

#include "model/Editing.h"
#include "model/Snapshot.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>

namespace tracktion::inline engine
{
class Engine;
}

namespace fd::engine
{
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

    std::uint64_t addAudioTrack();
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
    std::unique_ptr<Impl> impl_;
};
} // namespace fd::engine
