#pragma once

#include "SampleMap.h"
#include "fd/device/AssetHandoff.h"
#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/device/VoiceManager.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/SynthBlocks.h"

#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace fd::devices
{
/// Plays a sample across the keyboard, or an SFZ file's zones by key and velocity: Classic
/// (sustain, loop) and One-shot modes, amp and filter envelopes, band-limited playback at any
/// pitch. See devices/sampler/SPEC.md.
class Sampler final : public Device, private VoiceManager::Listener
{
public:
    enum Param : size_t
    {
        Mode, Start, End, Loop, LoopStart, LoopXfade,
        Root, Transpose, Detune, Gain, VelAmp,
        AmpA, AmpD, AmpS, AmpR, OneShotGate, FadeIn, FadeOut,
        FilterOn, FilterType, FilterCutoff, FilterRes, FilterEnv, FilterKey,
        FenvA, FenvD, FenvS, FenvR,
        VoiceMode, VoiceCount, VoiceGlide, VoiceBend,
        NumParams
    };
    enum class PlayMode { Classic, OneShot };
    /// Analysis slots: the output, the voices playing, up to eight voices' playheads (0..1 of
    /// the sample they play, -1 unused) and the zone each plays (-1 unused), and the zone the
    /// last note started.
    enum Analysis : int
    {
        PeakOut = 0,
        ActiveVoices = 1,
        FirstPlayhead = 2,
        FirstPlayheadZone = 10,
        LastZone = 18
    };
    static constexpr int numPlayheads = 8;
    static constexpr int maxVoices = VoiceManager::maxVoices;
    /// A note plays every zone it falls in, up to this many (SPEC, S4).
    static constexpr int maxLayers = SampleMap::maxChosen;
    /// Playback pitch is limited to this many semitones from the sample's own pitch.
    static constexpr double maxShiftSemitones = 60.0;

    Sampler();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    /// Extra state: {"sample.path", "sample.rate", "sample.frames"} for a sample, {"sample.path",
    /// "sample.zones"} for an SFZ file; a path into the Library is a `library:` reference
    /// (fd/device/Library.h). Loading is synchronous (ADR 0017): every zone is ready for the audio
    /// thread when readExtraState returns.
    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

    /// For displays (message thread): what's loaded (null when nothing is), the sample when it's
    /// one, and the last error.
    [[nodiscard]] std::shared_ptr<const SampleMap> map() const;
    [[nodiscard]] std::shared_ptr<const SampleData> sample() const;
    [[nodiscard]] std::string loadError() const;

    /// What a loaded file sets on the Sampler's controls besides itself, as plain values: a
    /// sample's root note; an SFZ file's Mode, Loop, Velocity to Volume and Amp envelope. The
    /// host sets them after loading, as an undo step of their own (SPEC, S6).
    [[nodiscard]] static std::vector<std::pair<size_t, float>> settingsFor(const SampleMap& map);

    /// A key the loaded sound plays, for hearing it (a preset's preview, the preset lint): `key`,
    /// or moved by octaves into its zones' keys (the nearest end if no octave fits), then to the
    /// nearest key a zone plays. `key` as it is when `device` isn't a Sampler or plays every key.
    [[nodiscard]] static int playableKey(const Device& device, int key);

    /// Playback region in frames for a sample of `frames` frames (from Start/End).
    struct Region
    {
        double start = 0.0, end = 0.0, loopStart = 0.0, loopEnd = 0.0, crossfade = 0.0;
        [[nodiscard]] double loopLength() const noexcept { return loopEnd - loopStart; }
    };
    [[nodiscard]] static Region regionFor(const ParamStore& p, int frames) noexcept;
    /// The same within a zone: Start and End in proportion to its own length; its own loop when
    /// it has one, else Loop Start's.
    [[nodiscard]] static Region regionFor(const ParamStore& p, const SampleMap::Zone& zone) noexcept;

private:
    using MapRef = AssetHandoff<SampleMap>::Ref;

    struct PendingNote
    {
        bool active = false;
        bool released = false;
        int key = 60, channel = 0;
        float velocity = 0.8f;
    };

    /// One zone a voice plays.
    struct Layer
    {
        const SampleMap::Zone* zone = nullptr;
        int zoneIndex = -1;
        double pos = 0.0; // in level-0 frames
        // Per-tick, ramped per sample.
        double ratioFrom = 1.0, ratioTo = 1.0; // level-0 frames per output sample
        int level = 0;
        const dsp::SincKernels::Table* table = nullptr;
        bool active = false;
    };

    struct Voice
    {
        MapRef map;
        std::array<Layer, maxLayers> layers{};
        int layerCount = 0;
        std::int64_t elapsed = 0; // output samples since the note started (fade in)
        int key = 60, channel = 0;
        float velocity = 0.8f;
        double glidePitch = 60.0, glideTarget = 60.0, glideRate = 0.0;
        dsp::Adsr amp, fenv;
        std::array<dsp::SynthFilter, 2> filter; // per channel
        // Per-tick controls, ramped per sample.
        double cutoffFrom = 10.0, cutoffTo = 10.0; // log2 Hz, before the filter envelope
        float gainFrom = 0.0f, gainTo = 0.0f;
        int fadeRemaining = 0; // > 0: fading out (steal or sample swap), then `pending` starts
        PendingNote pending;
        bool sounding = false;
    };

    // VoiceManager::Listener
    void voiceStart(int v, int key, float velocity, int channel, std::int32_t noteId, bool retrigger) override;
    void voiceLegato(int v, int key, float velocity) override;
    void voiceRelease(int v) override;

    void startNote(Voice& v, int key, float velocity, int channel, bool released) noexcept;
    /// The zones a note plays, into the voice's layers (round robin and random choice applied).
    void chooseZones(Voice& v, int key, float velocity) noexcept;
    void releaseVoice(Voice& v) noexcept;
    /// Ends the current sound: starts the pending note if there is one (returns true), else
    /// frees the voice.
    bool endSound(Voice& v) noexcept;
    void tick() noexcept;
    void updateControls(Voice& v, bool snap) noexcept;
    void handleEvent(const Event& e) noexcept;
    void takeNewMap() noexcept;
    void renderVoice(Voice& v, float* outL, float* outR, int from, int to, int phase) noexcept;

    double sampleRate_ = 48000.0;
    dsp::ControlGrid grid_{32};
    VoiceManager voices_;
    std::array<Voice, maxVoices> voice_{};
    std::array<float, 16> bend_{};
    int fadeSamples_ = 64; // steal / swap fade length
    double lastPitch_ = 60.0; // for poly glide
    bool forceRelease_ = false; // all-notes-off releases one-shots too
    SampleMap::Choice choice_;  // round robins and random choice, the same after every reset
    int lastZone_ = -1;

    // Map handoff (ADR 0017): `current_` plays new notes; `old_` waits for its voices to fade.
    AssetHandoff<SampleMap> maps_;
    MapRef current_, old_;

    // Loader side (non-real-time), guarded by loaderMutex_.
    mutable std::mutex loaderMutex_;
    std::string path_, error_;
    std::atomic<double> sampleSeconds_{0.0}; // the longest sample, for tailSeconds()
};

const DeviceFactory& samplerFactory();
} // namespace fd::devices
