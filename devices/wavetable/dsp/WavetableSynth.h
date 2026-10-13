#pragma once

#include "FactoryTables.h"
#include "fd/device/AssetHandoff.h"
#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/device/VoiceManager.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/IirOversampler.h"
#include "fd/dsp/Oscillator.h"
#include "fd/dsp/Smoother.h"
#include "fd/dsp/SynthBlocks.h"
#include "fd/dsp/VoiceModules.h"
#include "fd/dsp/WavetableOscillator.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace fd::devices
{
/// Three wavetable oscillators with two warps each and up to 16 unison copies, a sub and a noise
/// source, two filters from serial to parallel, four envelopes, eight drawn LFOs, two random
/// sources, eight macros and a 32-route modulation matrix; MPE. Every warp of an oscillator's
/// own cycle is free of aliasing: each voice renders its cycles for its pitch. See
/// devices/wavetable/SPEC.md.
class WavetableSynth final : public Device, private VoiceManager::Listener
{
public:
    static constexpr int numOscs = 3, numFilters = 2, numEnvs = 4, numLfos = 8, numRandoms = 2, numMacros = 8,
                         numRoutes = 32;
    static constexpr int maxVoices = VoiceManager::maxVoices;

    // ---- parameters (the SPEC's order: never reordered) ----------------------------------------
    enum OscParam : size_t
    {
        OscOn,
        OscPos,
        OscOct,
        OscSemi,
        OscFine,
        OscLevel,
        OscPan,
        OscPhase,
        OscRand,
        OscWarp1,
        OscWarp1Amt,
        OscWarp1Src,
        OscWarp2,
        OscWarp2Amt,
        OscWarp2Src,
        OscUni,
        OscDetune,
        OscUniMode,
        OscStack,
        OscBlend,
        OscWidth,
        OscSpread,
        OscWarpSpread,
        OscRoute,
        OscParamCount
    };
    [[nodiscard]] static constexpr size_t osc(int n, OscParam p) noexcept
    {
        return static_cast<size_t>(n) * OscParamCount + p;
    }

    static constexpr size_t SubOn = numOscs * OscParamCount, SubOct = SubOn + 1, SubShape = SubOn + 2,
                            SubLevel = SubOn + 3, SubPan = SubOn + 4, SubRoute = SubOn + 5;
    static constexpr size_t NoiseOn = SubOn + 6, NoiseType = NoiseOn + 1, NoiseColor = NoiseOn + 2,
                            NoiseLevel = NoiseOn + 3, NoisePan = NoiseOn + 4, NoiseRoute = NoiseOn + 5;

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
    static constexpr size_t FilterBase = NoiseRoute + 1;
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
                            VoiceBend = VoiceMode + 4, VoiceDrift = VoiceMode + 5, KeySnap = VoiceMode + 6,
                            OutVolume = VoiceMode + 7, Quality = VoiceMode + 8, NumParams = VoiceMode + 9;

    // ---- modulation ----------------------------------------------------------------------------
    /// Mod n Source's choices, in order.
    enum class Source : std::uint8_t
    {
        None,
        AmpEnv,
        FilterEnv,
        Env3,
        Env4,
        Lfo1,
        Random1 = Lfo1 + numLfos,
        Macro1 = Random1 + numRandoms,
        Velocity = Macro1 + numMacros,
        ReleaseVelocity,
        Note,
        Pressure,
        Slide,
        PitchBend,
        ModWheel,
        NoteRandom,
        Count
    };
    /// What an oscillator's routes can change, in the target list's order.
    enum OscTarget : int
    {
        TPosition,
        TPitch,
        TFine,
        TLevel,
        TPan,
        TWarp1,
        TWarp2,
        TDetune,
        TBlend,
        TWidth,
        TPositionSpread,
        TWarpSpread,
        OscTargetCount
    };
    enum FilterTarget : int
    {
        TCutoff,
        TResonance,
        TDrive,
        TShape,
        FilterTargetCount
    };
    // Mod n Target's choices: None, Pitch, then each oscillator's, sub, noise, filters, ...
    static constexpr int TargetNone = 0, TargetPitch = 1, TargetOsc = 2;
    [[nodiscard]] static constexpr int oscTarget(int n, OscTarget t) noexcept
    {
        return TargetOsc + n * OscTargetCount + t;
    }
    static constexpr int TargetSubLevel = TargetOsc + numOscs * OscTargetCount,
                         TargetSubPan = TargetSubLevel + 1, TargetNoiseLevel = TargetSubLevel + 2,
                         TargetNoiseColor = TargetSubLevel + 3, TargetNoisePan = TargetSubLevel + 4,
                         TargetFilter = TargetSubLevel + 5;
    [[nodiscard]] static constexpr int filterTarget(int n, FilterTarget t) noexcept
    {
        return TargetFilter + n * FilterTargetCount + t;
    }
    static constexpr int TargetRouting = TargetFilter + numFilters * FilterTargetCount,
                         TargetAmp = TargetRouting + 1, TargetPan = TargetRouting + 2,
                         TargetLfoRate = TargetRouting + 3, TargetRandomRate = TargetLfoRate + numLfos,
                         NumTargets = TargetRandomRate + numRandoms;

    /// The parameter a target moves (none for Pitch and Amp, which have none).
    [[nodiscard]] static std::optional<size_t> targetParam(int target) noexcept;
    /// The target a parameter is, for drag-to-modulate (none if it can't be modulated).
    [[nodiscard]] static std::optional<int> targetOf(size_t param) noexcept;

    enum Analysis : int
    {
        PeakOut = 0,
        ActiveVoices = 1,
        CpuLoad = 2,                        // the share of real time process() takes (0..1, and over)
        Osc1Position = 3,                   // where the newest note plays each oscillator's table (0..1)
        Lfo1Phase = Osc1Position + numOscs, // each LFO's phase (the newest note's when per note)
        NumAnalysis = Lfo1Phase + numLfos
    };

    /// A drawn curve (an LFO's shape, a Remap warp's): a named shape, or points (x, y, bend).
    struct Curve
    {
        std::string shape = "sine"; // when there are no points
        std::vector<std::array<float, 3>> points;
        [[nodiscard]] dsp::CurveTable table() const;
        [[nodiscard]] bool operator==(const Curve&) const = default;
    };
    /// The named shapes an LFO offers, in its menu's order.
    [[nodiscard]] static std::span<const std::string_view> namedShapes() noexcept;

    WavetableSynth();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override;
    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

    // For the panel and assistants (message thread).
    [[nodiscard]] std::shared_ptr<const dsp::Wavetable> table(int n) const;
    [[nodiscard]] const std::string& tableName(int n) const;
    [[nodiscard]] const std::string& tableId(int n) const; // a factory table's ID, or "user"
    [[nodiscard]] const Curve& lfoShape(int n) const { return lfoShapes_[static_cast<size_t>(n)]; }
    [[nodiscard]] const Curve& remapCurve(int osc, int warp) const
    {
        return remaps_[static_cast<size_t>(osc * 2 + warp)];
    }
    [[nodiscard]] std::string macroName(int n) const;

    /// The state patch that puts a factory table on oscillator n.
    [[nodiscard]] static nlohmann::json factoryTablePatch(int n, std::string_view id);
    /// The state patch that puts a table of `frames` cycles of 2 048 samples on oscillator n.
    [[nodiscard]] static nlohmann::json userTablePatch(int n, const std::string& name,
                                                       const dsp::Wavetable& table);

    /// The output delay at a Quality (the frequency-modulation warps' oversampling).
    [[nodiscard]] static int latencyFor(int quality) noexcept;

private:
    struct Assets
    {
        std::array<std::shared_ptr<const dsp::Wavetable>, numOscs> tables;
        std::array<dsp::CurveTable, numLfos> lfoShapes{};
        std::array<dsp::CurveTable, numOscs * 2> remaps{};
    };
    struct OscTable
    {
        std::string id{wavetable::defaultTableId};
        std::string name;
        std::string pcm; // a user table's frames, base64 16-bit
        std::shared_ptr<const dsp::Wavetable> table;
    };

    // Per-voice controls, ramped per sample between control ticks.
    enum Control : size_t
    {
        CPos0,
        CIncrement0 = CPos0 + numOscs,
        CLevel0 = CIncrement0 + numOscs,
        CPanL0 = CLevel0 + numOscs,
        CPanR0 = CPanL0 + numOscs,
        CWarpA0 = CPanR0 + numOscs,
        CWarpB0 = CWarpA0 + numOscs,
        CSubIncrement = CWarpB0 + numOscs,
        CSubLevel,
        CSubPanL,
        CSubPanR,
        CNoiseLevel,
        CNoiseColor,
        CNoisePanL,
        CNoisePanR,
        CCutoff0,
        CRes0 = CCutoff0 + numFilters,
        CDrive0 = CRes0 + numFilters,
        CShape0 = CDrive0 + numFilters,
        CRouting = CShape0 + numFilters,
        CAmpL,
        CAmpR,
        NumControls
    };

    struct Voice
    {
        std::array<dsp::WavetableOscillator, numOscs> oscs;
        dsp::Oscillator sub;
        dsp::ColouredNoise noise;
        std::array<std::array<dsp::VoiceFilter, 2>, numFilters> filters; // per channel
        std::array<dsp::CurvedEnvelope, numEnvs> envs;
        std::array<dsp::ShapeLfo, numLfos> lfos;
        std::array<float, numLfos> lfoValue{}, lfoSmoothed{};
        std::array<dsp::RandomSource, numRandoms> randoms;
        std::array<float, numRandoms> randomValue{};
        dsp::Drift driftPitch, driftCutoff;
        float driftP = 0.0f, driftC = 0.0f;
        int key = 60, channel = 0;
        float velocity = 0.8f, releaseVelocity = 0.0f, noteRandom = 0.0f;
        double heldSamples = 0.0; // since the note started (LFO fade-in)
        double glidePitch = 60.0, glideTarget = 60.0, glideRate = 0.0;
        std::array<double, NumControls> from{}, to{};
        std::array<float, numOscs> lastOut{};
        float lastSub = 0.0f, lastNoise = 0.0f;
        std::array<dsp::WavetableOscillator::Unison, numOscs> unison{};
        std::array<std::array<dsp::HalfbandDecimator, 2>, 6>
            decimators{}; // the buses, back down from 2x or 4x
        std::array<double, numOscs> maxIncrement{};
        std::uint32_t seed = 1;
        bool sounding = false;
    };

    // VoiceManager::Listener
    void voiceStart(int v, int key, float velocity, int channel, std::int32_t noteId,
                    bool retrigger) override;
    void voiceLegato(int v, int key, float velocity) override;
    void voiceRelease(int v) override;

    void handleEvent(const Event& e) noexcept;
    void tick(const TransportInfo& transport, bool renderNow) noexcept;
    void tickVoice(Voice& v, const TransportInfo& transport, bool renderNow) noexcept;
    void renderVoice(Voice& v, float* left, float* right, int from, int to, int phase) noexcept;
    [[nodiscard]] float sourceValue(const Voice& v, int source) const noexcept;
    [[nodiscard]] dsp::CurvedEnvelope::Settings envelopeSettings(int n) const noexcept;
    void applyQuality() noexcept;
    void publishAssets();

    double sampleRate_ = 48000.0;
    dsp::ControlGrid grid_{32};
    VoiceManager voices_;
    std::array<Voice, maxVoices> voice_{};
    dsp::CycleRenderer renderer_;
    std::array<float, NumParams> smoothed_{}; // continuous parameters, smoothed at the control rate
    bool smoothedPrimed_ = false;

    // Shared (not per note) LFOs and random sources.
    std::array<dsp::ShapeLfo, numLfos> lfos_;
    std::array<float, numLfos> lfoValue_{};
    std::array<dsp::RandomSource, numRandoms> randoms_;
    std::array<float, numRandoms> randomValue_{};

    // Controllers (MPE: per channel).
    std::array<float, 16> bend_{}, pressure_{}, slide_{};
    float modWheel_ = 0.0f;
    std::uint32_t notes_ = 0; // counts notes, for seeds
    double lastPitch_ = 60.0;
    int newest_ = -1;

    dsp::LinearSmoother volume_;
    int quality_ = -1, renderInterval_ = 128;
    std::vector<float> delayL_, delayR_; // the output's delay at Normal and High, without FM
    std::vector<float> spareRight_;      // a mono output's right channel
    int delayWrite_ = 0;

    // Tables and drawn curves: kept here (message thread), handed to the audio thread.
    std::array<OscTable, numOscs> tables_{};
    std::array<Curve, numLfos> lfoShapes_{};
    std::array<Curve, numOscs * 2> remaps_{};
    std::array<std::string, numMacros> macroNames_{};
    AssetHandoff<Assets> assets_;
    AssetHandoff<Assets>::Ref current_;

    double cpuLoad_ = 0.0;
};

const DeviceFactory& wavetableFactory();
} // namespace fd::devices
