#pragma once

#include "fd/dsp/Filters.h"

#include <cstdint>
#include <vector>

namespace fd::dsp
{
// The modulation core (M3 A2), shared by the Delay, Chorus & Ensemble, Phaser & Flanger and Auto
// Pan & Tremolo: a delay line read at moving, fractional positions; a feedback path with filters
// and saturation; LFOs with a stereo offset, free or locked to the song. Real-time safe after
// prepare().

/// A delay line read anywhere between its samples, and while the position moves: a Kaiser-
/// windowed sinc kernel (32 taps, full band) in a table of 1024 phases, interpolated between
/// phases, so a moving delay has no zipper and no dulling. Flat within 0.1 dB to 0.39 of the
/// sample rate; whole-sample delays are exact copies.
class ModulatedDelay
{
public:
    static constexpr int taps = 32;
    static constexpr int phases = 1024;
    /// The shortest delay that can be read (half the kernel): a through-zero flanger delays its
    /// dry signal by this much too.
    static constexpr int minimumDelay = taps / 2;

    /// Room for delays up to `maxDelaySamples`. May allocate.
    void prepare(double maxDelaySamples);
    void reset() noexcept;
    [[nodiscard]] double maxDelay() const noexcept { return maxDelay_; }

    /// Writes the next sample.
    void push(float x) noexcept
    {
        write_ = (write_ + 1) & mask_;
        buffer_[static_cast<size_t>(write_)] = x;
    }
    /// The signal `delay` samples before the last one pushed (minimumDelay to maxDelay; clamped):
    /// after push(x), read(d) is x delayed by d samples.
    [[nodiscard]] float read(double delay) const noexcept;

private:
    std::vector<float> buffer_;
    int mask_ = 0, write_ = 0;
    double maxDelay_ = 0.0;
};

/// What a delay feeds back (the Delay's tone, a flanger's): a low cut and a high cut (12 dB/oct
/// each, Butterworth), then saturation that leaves small signals as they are and keeps the loop
/// bounded at any feedback, then a DC blocker. Mono; one per channel.
class FeedbackPath
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    /// `lowCutHz` (0: off), `highCutHz` (0 or above 0.45 of the rate: off), `saturation` 0..1
    /// (0: clean, linear; 1: a tape-like soft clip from about -6 dBFS). Not per sample.
    void set(double lowCutHz, double highCutHz, double saturation) noexcept;
    float process(float x) noexcept;
    /// The filters' magnitude at `freqHz` (small signals): the panel draws what process() does.
    [[nodiscard]] static double magnitude(double freqHz, double lowCutHz, double highCutHz,
                                          double sampleRate) noexcept;

private:
    double sampleRate_ = 48000.0;
    Biquad lowCut_, highCut_;
    DcBlocker dc_;
    bool lowOn_ = false, highOn_ = false;
    float drive_ = 0.0f;
};

/// A modulation LFO: the usual shapes, a stereo offset for the right side, free-running in Hz or
/// one cycle every so many beats, locked to the song's position while it plays (a jump takes it
/// along; stopped, it runs on at the tempo). Sample-and-hold and wander take their values from
/// the cycle they're in, so the same song position always gives the same value.
class ModulationLfo
{
public:
    enum class Shape
    {
        Sine,     // starts at 0, rising
        Triangle, // 0, up to 1 at a quarter, down to -1 at three quarters
        SawUp,    // -1 to 1
        SawDown,  // 1 to -1
        Square,   // 1 for the first half, -1 for the second
        SampleAndHold,
        Wander, // smooth random: from one cycle's value to the next's
    };
    struct Stereo
    {
        float left = 0.0f, right = 0.0f;
    };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    /// Back to `phase` (in cycles).
    void reset(double phase = 0.0) noexcept;
    void setShape(Shape shape) noexcept { shape_ = shape; }
    /// Where the cycle starts (0..1), added to the song's position when synced.
    void setOffset(double cycles) noexcept { offset_ = cycles; }
    /// The right side's lead over the left (0..1 of a cycle; 0.5: opposite).
    void setStereo(double cycles) noexcept { stereo_ = cycles; }
    /// Free-running.
    void setRate(double hz) noexcept;
    /// Synced, at each block's start: one cycle every `beats` at `tempoBpm`; while `playing`, the
    /// phase is the song's (`ppq` / `beats` + the offset).
    void sync(double ppq, double tempoBpm, double beats, bool playing) noexcept;

    /// The values for the next sample, and on one sample.
    Stereo next() noexcept;
    /// The left side's position in cycles (whole cycles counted).
    [[nodiscard]] double position() const noexcept { return static_cast<double>(cycle_) + phase_; }

    /// A shape's value at `position` (cycles, counted from 0): what next() plays, and what a
    /// panel draws. Pure.
    [[nodiscard]] static float value(Shape shape, double position) noexcept;

private:
    double sampleRate_ = 48000.0;
    Shape shape_ = Shape::Sine;
    double offset_ = 0.0, stereo_ = 0.0;
    double increment_ = 0.0; // cycles per sample
    std::int64_t cycle_ = 0;
    double phase_ = 0.0; // 0..1 within the cycle
};
} // namespace fd::dsp
