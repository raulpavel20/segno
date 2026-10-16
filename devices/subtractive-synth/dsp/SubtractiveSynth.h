#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/device/VoiceManager.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Oscillator.h"
#include "fd/dsp/Smoother.h"
#include "fd/dsp/SynthBlocks.h"

#include <array>

namespace fd::devices
{
/// Two morphing oscillators + noise, state-variable/ladder filters, two envelopes, LFO, a 3-slot
/// modulation matrix, poly/mono/legato voices with drift and MPE. See devices/subtractive-synth/SPEC.md.
class SubtractiveSynth final : public Device, private VoiceManager::Listener
{
public:
    enum Param : size_t
    {
        Osc1Shape, Osc1Oct, Osc1Pw,
        Osc2Wave, Osc2Oct, Osc2Semi, Osc2Detune,
        MixOsc1, MixOsc2, MixNoise,
        FilterType, FilterCutoff, FilterRes, FilterKey, FilterEnv,
        AmpA, AmpD, AmpS, AmpR,
        ModA, ModD, ModS, ModR, ModCycle,
        LfoWave, LfoRate, LfoSync,
        M1Src, M1Dst, M1Amt, M2Src, M2Dst, M2Amt, M3Src, M3Dst, M3Amt,
        VoiceMode, VoiceCount, VoiceGlide, VoiceDrift, VoiceBend,
        OutVolume,
        NumParams
    };
    enum class Source { ModEnv, Lfo, Velocity, ModWheel, Pressure, Key, Slide };
    enum class Target { Pitch, Cutoff, Resonance, Osc1Shape, Osc1Width, Osc2Level, Amp, Pan };
    enum Analysis : int { PeakOut = 0, ActiveVoices = 1 };

    static constexpr int maxVoices = 16;

    SubtractiveSynth();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    // Pure helpers shared by the audio path and the panel's displays.
    [[nodiscard]] static dsp::Oscillator::Mix osc1Mix(double shapePercent, double widthPercent) noexcept;
    [[nodiscard]] static dsp::Oscillator::Mix osc2Mix(int wave) noexcept;
    [[nodiscard]] static dsp::Adsr::Settings envelopeSettings(double attackMs, double decayMs, double sustainPercent,
                                                              double releaseMs, bool cycle) noexcept;

private:
    // Per-voice control values, ramped per sample between control ticks.
    enum Control : size_t { CPitch, CCutoffOct, CRes, CShape, CWidth, COsc1, COsc2, CNoise, CAmp, CPan, NumControls };

    struct Voice
    {
        dsp::Oscillator osc1, osc2;
        dsp::SynthFilter filter;
        dsp::Adsr amp, mod;
        dsp::Noise noise;
        dsp::Drift driftPitch, driftCutoff;
        int key = 60;
        int channel = 0;
        float velocity = 0.8f;
        double glidePitch = 60.0;  // current (gliding) note pitch in semitones
        double glideTarget = 60.0;
        double glideRate = 0.0;    // semitones per sample
        float driftP = 0.0f, driftC = 0.0f;
        std::array<double, NumControls> from{}, to{}; // double: pitch precision
        bool sounding = false;
    };

    // VoiceManager::Listener
    void voiceStart(int v, int key, float velocity, int channel, std::int32_t noteId, bool retrigger) override;
    void voiceLegato(int v, int key, float velocity) override;
    void voiceRelease(int v) override;
    void voiceStop(int v) override;

    void tick(const TransportInfo& transport) noexcept;
    void computeControls(Voice& v, std::array<double, NumControls>& out) const noexcept;
    void handleEvent(const Event& e) noexcept;
    void startGlide(Voice& v, double fromPitch) noexcept;

    double sampleRate_ = 48000.0;
    dsp::ControlGrid grid_{32};
    VoiceManager voices_;
    std::array<Voice, maxVoices> voice_{};
    dsp::Lfo lfo_;
    float lfoValue_ = 0.0f;
    dsp::LinearSmoother volume_;
    double lastPitch_ = 60.0; // for poly glide

    // Controllers (MPE: per channel).
    std::array<float, 16> bend_{}, pressure_{}, slide_{};
    float modWheel_ = 0.0f;
};

const DeviceFactory& subtractiveSynthFactory();
} // namespace fd::devices
