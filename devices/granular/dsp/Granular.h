#pragma once

#include "GrainKernel.h"
#include "GrainSource.h"
#include "fd/device/AssetHandoff.h"
#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/device/VoiceManager.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/DrawnCurve.h"
#include "fd/dsp/Smoother.h"
#include "fd/dsp/VoiceModules.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fd::devices
{
/// Granular: grains of up to two sources (files, the Library's instruments, clips, the live
/// input) in six modes, a filter and colour on every grain, pitch on the song's key, two voice
/// filters, three envelopes, four drawn LFOs, two random sources, eight macros and a 32-route
/// matrix; MPE. See devices/granular/SPEC.md and ADR 0065.
///
/// The parameters, their order and the state are the SPEC's (approved 2026-10-16), laid out with
/// the Wavetable's families and names, so the shared synth parts (ui::synth) serve both. The
/// engine (step 1) schedules grains per mode, renders each with granular::renderGrain, then each
/// note's two filters and amp; the live input (step 2) and Spectral (step 3) come later.
class Granular final : public Device
{
public:
    static constexpr int numSources = 2, numFilters = 2, numEnvs = 3, numLfos = 4, numRandoms = 2,
                         numMacros = 8, numRoutes = 32;
    /// The most grains a note plays, and an instance (SPEC, "Performance").
    static constexpr int maxGrainsPerVoice = 256, maxGrains = 1024;

    // ---- parameters (the SPEC's order: never reordered) ----------------------------------------
    enum SourceParam : size_t
    {
        SrcOn,
        SrcInput,
        SrcStart,
        SrcEnd,
        SrcGain,
        SrcTune,
        SrcFine,
        SourceParamCount
    };
    [[nodiscard]] static constexpr size_t source(int n, SourceParam p) noexcept
    {
        return static_cast<size_t>(n) * SourceParamCount + p;
    }
    static constexpr size_t SrcMix = numSources * SourceParamCount, SrcPick = SrcMix + 1;
    static constexpr size_t LiveMode = SrcPick + 1, LiveLength = LiveMode + 1, LiveGain = LiveMode + 2,
                            LiveFeedback = LiveMode + 3;
    static constexpr size_t Pos = LiveFeedback + 1, PosSpray = Pos + 1, PosDist = Pos + 2, PosKey = Pos + 3,
                            PosSnap = Pos + 4, ScanRate = Pos + 5, ScanSync = Pos + 6, ScanEnd = Pos + 7,
                            ScanFrom = Pos + 8;
    static constexpr size_t GrainMode = ScanFrom + 1, GrainSize = GrainMode + 1,
                            GrainSizeSync = GrainMode + 2, GrainOverlap = GrainMode + 3,
                            GrainDensity = GrainMode + 4, GrainRate = GrainMode + 5,
                            GrainPeriods = GrainMode + 6, GrainFormant = GrainMode + 7,
                            GrainXfade = GrainMode + 8, GrainJitter = GrainMode + 9,
                            GrainChance = GrainMode + 10, GrainRepeats = GrainMode + 11,
                            GrainSwing = GrainMode + 12, GrainFire = GrainMode + 13,
                            GrainWindow = GrainMode + 14, GrainSkew = GrainMode + 15,
                            GrainFlat = GrainMode + 16, GrainReverse = GrainMode + 17,
                            GrainSizeSpray = GrainMode + 18, GrainLevelSpray = GrainMode + 19,
                            GrainEven = GrainMode + 20, GrainComp = GrainMode + 21, GrainMax = GrainMode + 22;
    static constexpr size_t PitchTranspose = GrainMax + 1, PitchFine = PitchTranspose + 1,
                            PitchKey = PitchTranspose + 2, PitchSpray = PitchTranspose + 3,
                            PitchDir = PitchTranspose + 4, PitchQuant = PitchTranspose + 5,
                            PitchChord = PitchTranspose + 6, PitchGlide = PitchTranspose + 7;
    static constexpr size_t PanSpray = PitchGlide + 1, PanMode = PanSpray + 1, OutWidth = PanSpray + 2;
    static constexpr size_t GFilterOn = OutWidth + 1, GFilterType = GFilterOn + 1,
                            GFilterCutoff = GFilterOn + 2, GFilterRes = GFilterOn + 3,
                            GFilterSpray = GFilterOn + 4, GFilterKey = GFilterOn + 5,
                            GFilterSnap = GFilterOn + 6;
    static constexpr size_t GColorCrush = GFilterSnap + 1, GColorCrushSpray = GColorCrush + 1,
                            GColorRing = GColorCrush + 2, GColorRatio = GColorCrush + 3,
                            GColorRingSpray = GColorCrush + 4;

    enum FilterParam : size_t
    {
        FilterOn,
        FilterType,
        FilterCutoff,
        FilterRes,
        FilterDrive,
        FilterShape,
        FilterKey,
        FilterEnv,
        FilterParamCount
    };
    static constexpr size_t FilterBase = GColorRingSpray + 1;
    [[nodiscard]] static constexpr size_t filter(int n, FilterParam p) noexcept
    {
        return FilterBase + static_cast<size_t>(n) * FilterParamCount + p;
    }
    static constexpr size_t FiltersRouting = FilterBase + numFilters * FilterParamCount;
    static constexpr size_t AmpVelocity = FiltersRouting + 1;

    enum EnvParam : size_t
    {
        EnvAttack,
        EnvHold,
        EnvDecay,
        EnvSustain,
        EnvRelease,
        EnvAttackCurve,
        EnvDecayCurve,
        EnvReleaseCurve,
        EnvMode,
        EnvParamCount
    };
    static constexpr size_t EnvBase = AmpVelocity + 1;
    [[nodiscard]] static constexpr size_t env(int n, EnvParam p) noexcept
    {
        return EnvBase + static_cast<size_t>(n) * EnvParamCount + p;
    }

    enum LfoParam : size_t
    {
        LfoRate,
        LfoSync,
        LfoMode,
        LfoPhase,
        LfoSmooth,
        LfoFade,
        LfoPoly,
        LfoParamCount
    };
    static constexpr size_t LfoBase = EnvBase + numEnvs * EnvParamCount;
    [[nodiscard]] static constexpr size_t lfo(int n, LfoParam p) noexcept
    {
        return LfoBase + static_cast<size_t>(n) * LfoParamCount + p;
    }

    enum RandomParam : size_t
    {
        RandomMode,
        RandomRate,
        RandomSync,
        RandomPoly,
        RandomParamCount
    };
    static constexpr size_t RandomBase = LfoBase + numLfos * LfoParamCount;
    [[nodiscard]] static constexpr size_t random(int n, RandomParam p) noexcept
    {
        return RandomBase + static_cast<size_t>(n) * RandomParamCount + p;
    }

    static constexpr size_t MacroBase = RandomBase + numRandoms * RandomParamCount;
    [[nodiscard]] static constexpr size_t macro(int n) noexcept { return MacroBase + static_cast<size_t>(n); }

    enum RouteParam : size_t
    {
        RouteSource,
        RouteTarget,
        RouteAmount,
        RouteCurve,
        RouteScale,
        RouteParamCount
    };
    static constexpr size_t RouteBase = MacroBase + numMacros;
    [[nodiscard]] static constexpr size_t route(int n, RouteParam p) noexcept
    {
        return RouteBase + static_cast<size_t>(n) * RouteParamCount + p;
    }

    static constexpr size_t VoiceMode = RouteBase + numRoutes * RouteParamCount, VoiceCount = VoiceMode + 1,
                            VoiceGlide = VoiceMode + 2, VoiceGlideMode = VoiceMode + 3,
                            VoiceBend = VoiceMode + 4, VoiceLatch = VoiceMode + 5, VoiceTails = VoiceMode + 6,
                            OutVolume = VoiceMode + 7, Quality = VoiceMode + 8;
    static constexpr size_t SpecFft = Quality + 1, SpecSmear = SpecFft + 1, SpecHarm = SpecFft + 2,
                            SpecClean = SpecFft + 3, NumParams = SpecFft + 4;

    /// Mode's choices, in order.
    enum class Mode : int
    {
        Classic,
        Loop,
        Cloud,
        Pitched,
        Rhythm,
        Spectral
    };
    /// Random n Mode's choices: the Wavetable's four, then Per Grain.
    static constexpr int RandomPerGrain = 4;

    // ---- modulation ----------------------------------------------------------------------------
    /// Mod n Source's choices, in order (permanent).
    enum class Source : std::uint8_t
    {
        None,
        AmpEnv,
        Env2,
        Env3,
        Lfo1,
        Random1 = Lfo1 + numLfos,
        Macro1 = Random1 + numRandoms,
        Velocity = Macro1 + numMacros,
        Note,
        Pressure,
        Slide,
        PitchBend,
        ModWheel,
        GrainRandom,
        GrainAlternate,
        Scan,
        InputLevel,
        Count
    };
    /// Mod n Target's choices (permanent): None, Pitch, then the grain targets, the sources', the
    /// grain filter's and colour's, the voice filters', and the rest.
    enum Target : int
    {
        TargetNone,
        TargetPitch,
        TargetPosition,
        TargetPositionSpray,
        TargetScan,
        TargetSize,
        TargetSizeSpray,
        TargetOverlap,
        TargetDensity,
        TargetPeriods,
        TargetFormant,
        TargetCrossfade,
        TargetJitter,
        TargetChance,
        TargetSwing,
        TargetFire,
        TargetSkew,
        TargetFlatness,
        TargetReverse,
        TargetLevelSpray,
        TargetEven,
        TargetPitchSpray,
        TargetGlide,
        TargetPan,
        TargetPanSpray,
        TargetWidth,
        TargetSourceMix,
        TargetSource1, // Start, End, Gain, Tune for each source
        TargetGrainCutoff = TargetSource1 + numSources * 4,
        TargetGrainRes,
        TargetCutoffSpray,
        TargetCrush,
        TargetRing,
        TargetRingRatio,
        TargetFilter1, // Cutoff, Resonance, Drive, Shape for each filter
        TargetRouting = TargetFilter1 + numFilters * 4,
        TargetAmp,
        TargetVolume,
        TargetLiveGain,
        TargetFeedback,
        TargetLfoRate,
        TargetRandomRate = TargetLfoRate + numLfos,
        TargetSmear = TargetRandomRate + numRandoms,
        TargetHarmonics,
        TargetClean,
        NumTargets
    };
    /// The parameter a target moves (none for Pitch, Pan and Amp, which have none).
    [[nodiscard]] static std::optional<size_t> targetParam(int target) noexcept;
    /// The target a parameter is, for drag-to-modulate (none if it can't be modulated).
    [[nodiscard]] static std::optional<int> targetOf(size_t param) noexcept;

    enum Analysis : int
    {
        PeakOut = 0,
        ActiveVoices = 1,
        CpuLoad = 2, // the share of real time process() takes (0..1, and over)
        GrainsPlaying = 3,
        Scan1 = 4, // where the newest note's playhead is in each source's part (0..1, -1: none)
        Lfo1Phase = Scan1 + numSources,
        InputLevelNow = Lfo1Phase + numLfos, // the live input's level, linear
        NumAnalysis
    };

    /// A drawn curve (an LFO's shape, the Drawn window): a named shape, or points (x, y, bend).
    using Curve = dsp::DrawnCurve;
    /// The named shapes an LFO offers, in its menu's order.
    [[nodiscard]] static std::span<const std::string_view> namedShapes() noexcept;

    /// A grain as the displays draw it: where it reads in its source's file (0..1), its pitch
    /// against the source's root (semitones), its level and pan, its source.
    struct GrainMark
    {
        float position = 0.0f, pitch = 0.0f, level = 0.0f, pan = 0.0f;
        std::uint8_t source = 0;
    };

    Granular();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;
    void startPass(std::uint64_t roll) noexcept override;
    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

    // ---- the panel and assistants (message thread) ---------------------------------------------
    /// What the displays draw of source n: its audio, or a multisample's middle-C zone (null while
    /// there's none).
    [[nodiscard]] std::shared_ptr<const GrainSource> sourceData(int n) const;
    /// What source n holds: a file's audio or a multisample's zones (null while empty).
    [[nodiscard]] std::shared_ptr<const GrainSourceSet> sourceSet(int n) const;
    /// Puts audio built off the audio thread into source n (tests, generated sounds); null empties it.
    void setSourceData(int n, std::shared_ptr<const GrainSource> data);
    void setSourceSet(int n, std::shared_ptr<const GrainSourceSet> set);
    /// Why source n's file didn't load (empty when it did).
    [[nodiscard]] std::string loadError(int n) const;
    /// What the live input buffer holds, for the displays: its newest moment last (null while
    /// there's no input). Made from the buffer on the message thread (step 2).
    [[nodiscard]] std::shared_ptr<const GrainSource> liveView() const { return liveView_; }
    void setLiveView(std::shared_ptr<const GrainSource> view) { liveView_ = std::move(view); }
    [[nodiscard]] const Curve& lfoShape(int n) const { return lfoShapes_[static_cast<size_t>(n)]; }
    [[nodiscard]] const Curve& drawnWindow() const { return window_; }
    [[nodiscard]] std::string macroName(int n) const;
    /// The grains playing now, as the audio thread last published them (at most maxGrains).
    [[nodiscard]] std::vector<GrainMark> grains() const;
    /// The roll its randomness comes from (ADR 0062): drawn again each time the song starts
    /// playing, unless kept. Module ▸ Keep as Played stores this one.
    [[nodiscard]] std::uint32_t currentRoll() const noexcept { return roll_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool rollKept() const noexcept { return rollKept_.load(std::memory_order_relaxed); }
    /// Keep as Played holds the roll playing now (or last played), so every pass plays it; off,
    /// it rolls again each time (Let It Roll). Stored with the state.
    void keepRoll(bool keep) noexcept { rollKept_.store(keep, std::memory_order_relaxed); }
    /// The same as a state patch, for the host's undo (Module ▸ Keep as Played).
    [[nodiscard]] nlohmann::json rollPatch(bool keep) const;

    /// Loading `stored` (a file's path, or a `library:` reference) into source n, as the panel,
    /// the browser and the assistants all do it: the state's patch (the file, its root found
    /// again) and the settings that go with it (the source on, playing the file, all of it).
    [[nodiscard]] static nlohmann::json sourcePatch(int n, const std::string& stored);
    [[nodiscard]] static std::vector<std::pair<size_t, float>> sourceSettings(int n);
    /// Swap Sources: each source's file, root and settings in the other's place.
    [[nodiscard]] nlohmann::json swappedPatch() const;
    [[nodiscard]] std::vector<std::pair<size_t, float>> swappedSettings() const;

    // ---- the audio thread's side of the display ------------------------------------------------
    /// Publishes the grains playing now for the displays (real-time safe: no allocation, no lock).
    void publishGrains(std::span<const GrainMark> marks) noexcept;

private:
    // What the audio thread plays from, handed over whole (ADR 0017).
    struct Assets
    {
        std::array<std::shared_ptr<const GrainSourceSet>, numSources> sources;
        std::array<std::optional<double>, numSources> roots; // set by hand, over what's found
        std::array<dsp::CurveTable, numLfos> lfoShapes{};
        dsp::CurveTable window{};
    };

    // The voice stage's controls at a tick (filters, the amp), ramped over the tick.
    enum Control : size_t
    {
        CCutoff0,
        CRes0 = CCutoff0 + numFilters,
        CDrive0 = CRes0 + numFilters,
        CShape0 = CDrive0 + numFilters,
        CRouting = CShape0 + numFilters,
        CAmp,   // velocity, modulation (Amp, Volume) and Density Gain
        CWidth, // the note's width (mid and side), as Width and its modulation set
        NumControls
    };
    /// Where the voice stage's controls change in a block: at each tick, and where a note starts.
    /// It runs to the next one's start (or the block's end).
    struct Segment
    {
        int from = 0;                            // the block's sample
        int phase0 = 0;                          // samples into its tick at `from`
        std::array<float, NumControls> a{}, b{}; // the controls at the tick's start and end
        bool reset = false;                      // a new note from silence: the filters cleared first
        bool silent = false;                     // the voice has finished
    };

    /// A note. Its grains are rendered over the whole block at its end, counted where each new
    /// one starts (so the budget is exact, whatever the block size); its envelopes run sample by
    /// sample up to each event, grain and tick for the same reason.
    struct Voice
    {
        std::array<dsp::CurvedEnvelope, numEnvs> envs;
        std::array<dsp::ShapeLfo, numLfos> lfos;
        std::array<float, numLfos> lfoSmoothed{};
        std::array<dsp::RandomSource, numRandoms> randoms;
        std::array<float, numRandoms> randomValue{};
        std::array<std::array<dsp::VoiceFilter, 2>, numFilters> filters; // per channel
        std::array<float, 2> dcIn{}, dcOut{}; // the DC blocker's (Pitched grains can carry DC)
        int key = 60, channel = 0, velocity127 = 100;
        float velocity = 0.8f;
        double heldSamples = 0.0;
        double glidePitch = 60.0, glideTarget = 60.0, glideRate = 0.0;
        std::uint32_t seed = 1, grainCount = 0;
        std::int64_t born = 0; // the sample its note started (the device's count)
        // The scheduler: the next grain's start and the grid it keeps (samples since `born`), the
        // Rhythm grid's next step, the playhead's travel, Fire's last value.
        double nextOnset = 0.0, nextBase = 0.0;
        std::int64_t step = 0;
        bool findStep = true; // Rhythm on the song: the next step is found again
        double travel = 0.0;
        int direction = 1;
        float lastFire = 0.0f;
        bool spawning = true;
        std::array<const GrainSourceSet::Zone*, numSources> zones{};
        std::array<float, numSources> place{}; // where its playhead is in each part (0 … 1, past 1 wrapping)
        std::array<float, NumControls> ctrlFrom{}, ctrlTo{};
        // Modulation at the last tick: every target's offset (in its normalised range).
        std::array<float, NumTargets> mod{};
        std::vector<granular::Grain> grains; // capacity 2 x maxGrainsPerVoice, set in prepare()
        int active = 0;                      // grains[0, active) are playing, in the order they started
        // This block's: the grains summed, the amp envelope, the voice stage's segments; how far
        // the grains and the envelopes have got.
        std::vector<float> busL, busR, amp;
        std::vector<Segment> segments;
        int segmentCount = 0, rendered = 0, envAt = 0;
        // Grain Tails: from the release, each grain keeps the level it started at.
        bool frozen = false;
        float releaseLevel = 1.0f;
        // Where its amp envelope ended, and where it ends (its last grain's end, and its filters'
        // ring), in the device's count; -1: not yet.
        std::int64_t envEnd = -1, tailEnd = -1;
        bool sounding = false, released = false;
    };

    // VoiceManager::Listener
    struct Listener final : VoiceManager::Listener
    {
        Granular& g;
        explicit Listener(Granular& owner) : g(owner) {}
        void voiceStart(int v, int key, float velocity, int channel, std::int32_t noteId,
                        bool retrigger) override;
        void voiceLegato(int v, int key, float velocity) override;
        void voiceRelease(int v) override;
    };

    void handleEvent(const Event& e, int at) noexcept;
    void tick(const TransportInfo& transport, int offset) noexcept;
    void tickVoice(Voice& v, const TransportInfo& transport, int offset) noexcept;
    void schedule(Voice& v, const TransportInfo& transport, int from, int to) noexcept;
    void spawn(Voice& v, const TransportInfo& transport, double at) noexcept;
    void advanceEnvs(Voice& v, int to) noexcept;
    void renderGrains(Voice& v, int to) noexcept;
    void renderVoiceStage(Voice& v, float* outL, float* outR, int n) noexcept;
    void addSegment(Voice& v, int from, int phase0, bool reset, bool silent = false) noexcept;
    void finishVoice(int index, int at) noexcept;
    void release(Voice& v, int at) noexcept;
    void updateZones(Voice& v) noexcept;
    void publishDisplay(int n) noexcept;
    [[nodiscard]] float sourceValue(const Voice& v, int source) const noexcept;
    [[nodiscard]] dsp::CurvedEnvelope::Settings envelopeSettings(int n) const noexcept;
    [[nodiscard]] float moved(size_t param, int target,
                              const std::array<float, NumTargets>& mod) const noexcept;
    [[nodiscard]] double notePitch(const Voice& v, const std::array<float, NumTargets>& mod) const noexcept;
    [[nodiscard]] int alive(const Voice& v, int at) const noexcept;
    [[nodiscard]] int grainCap() const noexcept;
    [[nodiscard]] double expectedOverlap(const Voice& v) const noexcept;
    void publishAssets();

    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    dsp::ControlGrid grid_{32};
    VoiceManager voices_;
    Listener listener_{*this};
    std::array<Voice, 16> voice_{};
    granular::GrainScratch scratch_;
    std::array<float, NumParams> smoothed_{};
    bool smoothedPrimed_ = false;
    std::array<dsp::ShapeLfo, numLfos> lfos_;
    std::array<float, numLfos> lfoValue_{};
    std::array<dsp::RandomSource, numRandoms> randoms_;
    std::array<float, numRandoms> randomValue_{};
    std::array<float, 16> bend_{}, pressure_{}, slide_{};
    float modWheel_ = 0.0f;
    std::uint32_t notes_ = 0;
    double lastPitch_ = 60.0;
    int newest_ = -1;
    double sharedTravel_ = 0.0; // the Shared playhead's travel
    bool wasPlaying_ = false;
    bool allOff_ = false; // releasing every note: Latch lets go too
    // Where the block is: its first sample (counting from reset), the song's sample there, the
    // event being handled, the tick it's in.
    std::int64_t blockStart_ = 0, blockSong_ = 0;
    int eventAt_ = 0, tickOffset_ = 0, tickPhase_ = 0;
    TransportInfo tickTransport_{};
    int sinceDisplay_ = 0;
    std::array<GrainMark, maxGrains> shownMarks_{}; // the audio thread's, before publishing
    dsp::LinearSmoother volume_;
    std::vector<float> spareRight_;
    double cpuLoad_ = 0.0;

    // The roll (ADR 0062): kept, or drawn again at each start of playback. Counted from a fixed
    // base, so two new instances play the same.
    static constexpr std::uint32_t kRollBase = 0x5E6A0D1u;
    std::atomic<std::uint32_t> roll_{1};
    std::atomic<bool> rollKept_{false};
    std::atomic<std::uint32_t> hostRoll_{0}; // the host's roll for the next pass (0: none)
    std::uint32_t rollStarts_ = 0;

    // Sources and drawn curves: kept here (message thread), handed to the audio thread.
    mutable std::mutex loaderMutex_;
    std::array<std::shared_ptr<const GrainSourceSet>, numSources> sources_;
    std::array<std::string, numSources> paths_, errors_;
    std::array<std::optional<double>, numSources> roots_; // set by hand
    std::shared_ptr<const GrainSource> liveView_;
    std::array<Curve, numLfos> lfoShapes_{};
    Curve window_{};
    std::array<std::string, numMacros> macroNames_{};
    AssetHandoff<Assets> assets_;
    AssetHandoff<Assets>::Ref current_;
    // The assets before the last swap, held while the grains of a source that changed fade out.
    AssetHandoff<Assets>::Ref previous_;
    int previousLeft_ = 0; // samples until it's let go

    // The displays' grains: a triple buffer, wait-free on both sides. The audio thread fills its
    // back buffer and swaps it with the middle one, marked new; the panel swaps the middle one with
    // its front when it's new. No buffer is ever read and written at once.
    struct Marks
    {
        std::array<GrainMark, maxGrains> marks{};
        int count = 0;
    };
    static constexpr int kNewMarks = 4; // in middle_: the middle buffer holds marks not yet read
    std::array<Marks, 3> markBuffers_{};
    int backMarks_ = 0;          // the audio thread's
    mutable int frontMarks_ = 2; // the panel's
    mutable std::atomic<int> middleMarks_{1};
};

[[nodiscard]] const DeviceFactory& granularFactory();
} // namespace fd::devices
