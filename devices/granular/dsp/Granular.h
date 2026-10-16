#pragma once

#include "GrainSource.h"
#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/DrawnCurve.h"

#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <string>
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
/// grain engine is built in step 1; until then process() plays silence.
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
    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

    // ---- the panel and assistants (message thread) ---------------------------------------------
    /// Source n's audio as loaded (null while none is, or while it's being read).
    [[nodiscard]] std::shared_ptr<const GrainSource> sourceData(int n) const;
    /// Puts audio built off the audio thread into source n; null empties it.
    void setSourceData(int n, std::shared_ptr<const GrainSource> data);
    /// What the live input buffer holds, for the displays: its newest moment last (null while
    /// there's no input). Made from the buffer on the message thread (step 2).
    [[nodiscard]] std::shared_ptr<const GrainSource> liveView() const { return liveView_; }
    void setLiveView(std::shared_ptr<const GrainSource> view) { liveView_ = std::move(view); }
    [[nodiscard]] const Curve& lfoShape(int n) const { return lfoShapes_[static_cast<size_t>(n)]; }
    [[nodiscard]] const Curve& drawnWindow() const { return window_; }
    [[nodiscard]] std::string macroName(int n) const;
    /// The grains playing now, as the audio thread last published them (at most maxGrains).
    [[nodiscard]] std::vector<GrainMark> grains() const;

    // ---- the audio thread's side of the display ------------------------------------------------
    /// Publishes the grains playing now for the displays (real-time safe: no allocation, no lock).
    void publishGrains(std::span<const GrainMark> marks) noexcept;

private:
    std::array<std::shared_ptr<const GrainSource>, numSources> sources_;
    std::shared_ptr<const GrainSource> liveView_;
    std::array<Curve, numLfos> lfoShapes_{};
    Curve window_{};
    std::array<std::string, numMacros> macroNames_{};

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
