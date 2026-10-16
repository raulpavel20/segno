#pragma once

#include "fd/dsp/SynthBlocks.h"
#include "fd/dsp/Wavetable.h"

#include <cstdint>
#include <vector>

namespace fd::dsp
{
/// A synth voice's filter with fourteen types (the Wavetable Synth's, devices/wavetable/SPEC.md):
/// state-variable low, high and band passes at 12 and 24 dB, notch and peak; the ladder; a
/// diode ladder (three poles, a rounder resonance); positive and negative combs with damping;
/// a formant filter morphing through vowels; a six-stage phaser. Drive saturates before it.
/// Every type has an exact (small-signal) magnitude for displays. Real-time safe after prepare().
class VoiceFilter
{
public:
    enum class Type : std::uint8_t
    {
        Lp12,
        Lp24,
        Ladder,
        Diode,
        Hp12,
        Hp24,
        Bp12,
        Bp24,
        Notch,
        Peak,
        CombPlus,
        CombMinus,
        Formant,
        Phaser
    };
    static constexpr int numTypes = 14;

    /// May allocate: the comb's delay for 20 Hz at `sampleRate`.
    void prepare(double sampleRate);
    void reset() noexcept;

    /// One sample. `cutoff` in Hz; `resonance`, `drive` and `shape` 0..1 (shape: the formant's
    /// vowel, the comb's damping, the phaser's spread).
    float process(float x, Type type, double cutoff, double resonance, double drive, double shape) noexcept;

    /// The magnitude process() has at `freq` Hz, small signals and no drive (what the panel draws).
    [[nodiscard]] static double magnitude(Type type, double freq, double cutoff, double resonance,
                                          double shape, double sampleRate) noexcept;

private:
    struct Svf
    {
        double s1 = 0.0, s2 = 0.0;
        double lp = 0.0, bp = 0.0, hp = 0.0;
        void tick(double x, double g, double k) noexcept;
    };

    // The coefficients of the last cutoff and resonance, kept: they're the same sample after
    // sample unless something moves them (a tan and a pow saved per sample).
    [[nodiscard]] double cutoffG(double cutoff) noexcept;
    [[nodiscard]] double damping(double resonance) noexcept;
    [[nodiscard]] double secondDamping(double resonance) noexcept;

    double sampleRate_ = 48000.0;
    double gCutoff_ = -1.0, g_ = 0.0, kResonance_ = -1.0, k_ = 0.0, k2Resonance_ = -1.0, k2_ = 0.0;
    Svf a_, b_;
    std::array<Svf, 3> formants_{};
    LadderFilter ladder_;
    std::array<double, 3> diode_{};
    std::array<double, 6> phaser_{};
    double phaserLast_ = 0.0;
    std::vector<float> comb_;
    int combMask_ = 0, combWrite_ = 0;
    double combDamp_ = 0.0;
};

/// An envelope with attack, hold, decay, sustain and release, each fall and rise bent by its
/// curve (below zero it starts slowly, above zero quickly), and three modes: Normal; Loop
/// (attack to decay again while the key is held); One-shot (plays through, the release ignored
/// until it's done). Restarts from where it is (no clicks). Real-time safe.
class CurvedEnvelope
{
public:
    enum class Mode : std::uint8_t
    {
        Normal,
        Loop,
        OneShot
    };
    enum class Stage : std::uint8_t
    {
        Idle,
        Attack,
        Hold,
        Decay,
        Sustain,
        Release
    };
    struct Settings
    {
        double attackMs = 2.0, holdMs = 0.0, decayMs = 300.0, sustain = 0.8, releaseMs = 200.0;
        double attackCurve = 0.3, decayCurve = 0.6, releaseCurve = 0.6; // -1 ... +1
        Mode mode = Mode::Normal;
    };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    /// New times take effect at the next segment; the sustain level at once.
    void set(const Settings& s) noexcept { settings_ = s; }
    void noteOn() noexcept;
    void noteOff() noexcept;
    /// Falls straight to silence within `ms`, released or not, whatever its release: a sound
    /// stopped (All sound off, a Drum Kit's choke group).
    void stop(double ms) noexcept;
    void reset() noexcept;

    /// Advances one sample and returns the level (0..1).
    float next() noexcept;
    [[nodiscard]] float level() const noexcept { return static_cast<float>(level_); }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] bool isActive() const noexcept { return stage_ != Stage::Idle; }

    /// Where a segment bent by `curve` (-1 ... +1) is at `u` (0..1 of its time), 0..1: what next()
    /// plays and what the panel draws.
    [[nodiscard]] static double shape(double u, double curve) noexcept;

private:
    void enter(Stage stage) noexcept;

    double sampleRate_ = 48000.0;
    Settings settings_{};
    Stage stage_ = Stage::Idle;
    double level_ = 0.0, from_ = 0.0, to_ = 0.0;
    double u_ = 0.0, du_ = 0.0;
    // The bend, followed by one multiply a sample: e = exp(-a u).
    double a_ = 0.0, e_ = 1.0, step_ = 1.0, norm_ = 1.0;
    bool released_ = false;
};

/// An LFO that plays a drawn shape (a CurveTable, 0..1, played as -1 ... +1): free running,
/// restarted with each note, or once per note as an envelope. The rate is the caller's (hertz,
/// or from the song's tempo); a free LFO synced to a playing song takes its phase from the song.
class ShapeLfo
{
public:
    enum class Mode : std::uint8_t
    {
        Free,
        Retrigger,
        Envelope
    };
    void reset(double phase = 0.0) noexcept;
    /// A note started: Retrigger and Envelope restart from `phase` (0..1).
    void noteOn(double phase) noexcept;
    /// Advances `samples` at `cyclesPerSample` and returns the value there (-1 ... +1). With
    /// `songPhase` (0 or more), the phase is the song's instead.
    float advance(const CurveTable& shape, Mode mode, int samples, double cyclesPerSample,
                  double songPhase = -1.0) noexcept;
    [[nodiscard]] double phase() const noexcept { return phase_; }

private:
    double phase_ = 0.0;
    bool done_ = false; // an Envelope-mode LFO that has played its shape once
};

/// The random sources: Steps holds each new value; Smooth glides between them; Chaos follows a
/// Lorenz attractor; Per Note picks one value for each note. Seeded, so a render repeats.
class RandomSource
{
public:
    enum class Mode : std::uint8_t
    {
        Steps,
        Smooth,
        Chaos,
        PerNote
    };
    void reset(std::uint32_t seed) noexcept;
    void noteOn() noexcept;
    /// Advances `samples` at `cyclesPerSample` (new values per sample) and returns the value
    /// (-1 ... +1).
    float advance(Mode mode, int samples, double cyclesPerSample) noexcept;

private:
    double random() noexcept;
    std::uint32_t state_ = 1;
    double phase_ = 0.0, from_ = 0.0, to_ = 0.0, note_ = 0.0;
    double x_ = 0.1, y_ = 0.0, z_ = 0.0; // Lorenz
};
} // namespace fd::dsp
