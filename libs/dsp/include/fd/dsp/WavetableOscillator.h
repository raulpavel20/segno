#pragma once

#include "fd/dsp/Wavetable.h"

#include <array>
#include <cstdint>

namespace fd::dsp
{
/// One oscillator of one voice of the Wavetable Synth: plays the cycles a CycleRenderer makes for
/// it, as up to 16 unison copies (detuned by a law, stacked at intervals, spread in stereo, each
/// pair reading its own place in the table when spread). New cycles crossfade in over a render
/// interval; a cycle is rendered again only when what it's made from changed.
class WavetableOscillator
{
public:
    static constexpr int maxUnison = 16;
    static constexpr int maxCycles = 4; // distinct renders when copies spread

    enum class UnisonMode : std::uint8_t
    {
        Even,
        Super,
        Exponential,
        Random
    };
    enum class Stack : std::uint8_t
    {
        Off,
        Octave,
        TwoOctaves,
        Fifth,
        FifthOctave,
        Major,
        Minor,
        KeyChord
    };

    struct Unison
    {
        int count = 1;
        float detune = 0.25f; // 0..1: the outer copies up to 2 semitones apart at full
        UnisonMode mode = UnisonMode::Super;
        Stack stack = Stack::Off;
        std::array<int, 3> keyChord{0, 4, 7}; // Stack::KeyChord's intervals (from the project's key)
        float blend = 0.75f;                  // 0: the middle copy alone; 1: all alike
        float width = 0.8f;                   // 0..1
        float positionSpread = 0.0f, warpSpread = 0.0f; // 0..1
    };

    /// A new note: each copy's phase from `phase` (0..1) plus a share `phaseRandom` (0..1) of a
    /// random one, from `seed` (so a render repeats exactly). Forgets the cycles.
    void start(std::uint32_t seed, float phase, float phaseRandom) noexcept;
    /// The unison: each copy's pitch ratio, pan and level (levels keep the power even).
    void setUnison(const Unison& unison) noexcept;

    /// Renders the cycles for the next stretch (called on the render grid): the table at
    /// `position` with `warps`, for copies playing at most `maxIncrement` cycles per sample. The
    /// new cycles crossfade in over `fadeSamples`. Renders nothing new if nothing changed.
    /// Under frequency or phase modulation, reads run up to `reach` times faster at moments, at
    /// `oversampling` times the rate: the harmonics are cut so they fold back only above 20 kHz.
    void render(CycleRenderer& renderer, const Wavetable& table, double position,
                const std::array<CycleRenderer::WarpSetting, 2>& warps, double maxIncrement,
                double sampleRate, int fadeSamples, double reach = 1.0, int oversampling = 1) noexcept;

    /// The highest pitch ratio of any copy (for the band limit).
    [[nodiscard]] double widestRatio() const noexcept { return widest_; }

    /// One sample, added to `left` and `right`: the middle copy plays `increment` cycles per
    /// sample (times `fm`, which may go below zero: through-zero FM), read `phaseOffset` cycles
    /// ahead (PM). Returns the copies' sum before panning (what this oscillator is as a source).
    float next(double increment, double fm, double phaseOffset, float& left, float& right) noexcept;

    [[nodiscard]] bool hasCycle() const noexcept { return rendered_; }
    /// 8-point reads (High quality) instead of cubic.
    void setFineReads(bool fine) noexcept { fine_ = fine; }

private:
    struct Cycle
    {
        std::array<float, CycleRenderer::bufferSize> now{}, before{};
        int size = 0, sizeBefore = 0;
        double position = -1.0;
        std::array<float, 2> amounts{-1.0f, -1.0f};
        std::array<Warp, 2> warps{};
        int harmonics = 0;
    };

    std::array<Cycle, maxCycles> cycles_{};
    int numCycles_ = 1;
    std::array<double, maxUnison> phase_{}, ratio_{};
    std::array<float, maxUnison> gainL_{}, gainR_{}, level_{};
    std::array<std::uint8_t, maxUnison> cycleOf_{};
    std::array<float, maxUnison> randomDetune_{};
    int count_ = 1;
    double widest_ = 1.0;
    int fade_ = 0, fadeLength_ = 1;
    bool rendered_ = false, fine_ = false;
    std::uint32_t seed_ = 1;
    const Wavetable* table_ = nullptr;
    Unison unison_{};
};

/// The noises of the Wavetable Synth's noise source, in its Noise Type order.
class ColouredNoise
{
public:
    enum class Type : std::uint8_t
    {
        White,
        Pink,
        Brown,
        Digital,
        Crackle,
        Breath,
        Metallic
    };
    void reset(std::uint32_t seed) noexcept;
    /// One sample at `sampleRate`, tilted by `colour` (-1 darker ... +1 brighter).
    float next(Type type, float colour, double sampleRate) noexcept;

private:
    float white() noexcept;
    std::uint32_t state_ = 1;
    std::array<double, 7> pink_{};
    double brown_ = 0.0, held_ = 0.0, holdPhase_ = 0.0, breathEnv_ = 0.0, breathLp_ = 0.0, breathBp_ = 0.0;
    std::array<double, 6> metalPhase_{};
    double tiltLow_ = 0.0;
};
} // namespace fd::dsp
