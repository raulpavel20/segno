#pragma once

#include "GrainWindow.h"

#include <cstdint>
#include <span>
#include <vector>

namespace fd::devices::granular
{
/// How a grain reads its level between samples (SPEC, "Performance": Quality).
enum class Interp : std::uint8_t
{
    Linear, // Eco
    Opt4,   // Normal: 4 points, 3rd order, optimal for 2x oversampled data
    Opt6,   // High: 6 points, 5th order, the same
    Sinc8   // the plain (compact) pyramid's, at Normal and High: an 8-tap polyphase sinc
};

/// One grain, its settings fixed at its start (SPEC, "Performance": nothing per sample that can be
/// per grain). Rendered over each block by renderGrain(); real-time safe.
struct Grain
{
    // Where it reads: a level of its source, interleaved, past the padding.
    const float* f32 = nullptr;
    const std::int16_t* i16 = nullptr;
    std::int8_t channels = 1; // the level's
    Interp interp = Interp::Opt4;
    std::int64_t pos = 0;            // 32.32 fixed point, in the level's frames
    std::int64_t inc = 0;            // per output sample (negative: reversed)
    std::int64_t dinc = 0;           // the change of inc per sample (Grain Glide)
    std::int64_t pos0 = 0, inc0 = 0; // where it started, and how fast

    /// Where it reads at `age` samples, exactly (any block split gives the same).
    [[nodiscard]] std::int64_t posAt(std::int64_t a) const noexcept
    {
        return pos0 + inc0 * a + dinc * (a * (a - 1) / 2);
    }

    // Its life.
    std::int32_t length = 0; // samples
    std::int32_t age = 0;
    std::int32_t delay = 0;    // samples into the next block before it starts
    std::int32_t fadeLeft = 0; // > 0: stolen, fading out over this many samples
    std::int32_t fadeLength = 0;
    std::int32_t repeatsLeft = 0; // Repeats: it plays again from its start this many more times
    GrainWindow window{};

    // Out: a 2 x 2 pan (and level) matrix: left out from left in, from right in, and so on.
    float gLL = 0.0f, gRL = 0.0f, gLR = 0.0f, gRR = 0.0f;

    // Colour: crush (a held sample every `crushHold`, quantised to `crushStep`), ring (a sine at
    // `ringInc` cycles a sample, mixed in by `ringMix`).
    std::int32_t crushHold = 1;
    float crushStep = 0.0f; // 0: no bit reduction
    float ringMix = 0.0f, ringInc = 0.0f;

    // The grain filter: the state-variable filter's coefficients, fixed at the start; 0: off.
    std::uint8_t filterType = 0; // 1 LP, 2 BP, 3 HP, 4 Resonator
    float fA1 = 0.0f, fA2 = 0.0f, fA3 = 0.0f, fK = 0.0f, fGain = 1.0f;
    float fIc1[2]{}, fIc2[2]{};

    // For the displays.
    float shownPitch = 0.0f, shownLevel = 0.0f, shownPan = 0.0f;
    double levelFrames = 1.0; // its level's length (for where it reads, 0 … 1)
    std::uint8_t source = 0;
    const void* owner = nullptr; // the source set it reads (its memory)

    [[nodiscard]] bool finished() const noexcept
    {
        return age >= length || (fadeLength > 0 && fadeLeft <= 0);
    }
};

/// Working memory for renderGrain(): its taps and fractions for one block. Allocated off the
/// audio thread.
struct GrainScratch
{
    static constexpr int laneCount = 8; // filtered grains rendered together
    void prepare(int maxBlock);
    int capacity = 0;
    std::vector<float> taps; // [tap][channel][sample], 8 taps x 2 channels x capacity
    std::vector<float> frac, y0, y1;
    std::vector<float> lanes; // [channel][sample][lane], the filtered grains'
};

/// The grain filter's damping (1 / Q) for a type (1 LP, 2 BP, 3 HP, 4 Resonator) and a resonance (0 … 1).
struct GrainFilterShape
{
    double k = 2.0;
};
[[nodiscard]] GrainFilterShape grainFilterShape(int type, double resonance) noexcept;
/// Sets the grain filter's coefficients for `cutoff` (Hz) and `resonance` (0 … 1) at `sampleRate`;
/// type 0 turns it off.
void setGrainFilter(Grain& g, int type, double cutoff, double resonance, double sampleRate) noexcept;
/// The grain filter's response at `hz`, exactly as each grain's filter has it (what the panel draws).
[[nodiscard]] double grainFilterMagnitude(int type, double hz, double cutoff, double resonance,
                                          double sampleRate) noexcept;

/// Renders `grains` into `left` and `right` over `n` samples (each from where its delay ends, its
/// repeats restarting back to back), and moves them on: those without a grain filter one by one,
/// those with one eight at a time, their filters run across the grains. Picks the build for the
/// machine (AVX2 where it has it). Real-time safe.
void renderGrains(std::span<Grain> grains, float* left, float* right, int n, GrainScratch& scratch) noexcept;
/// One grain, the same way.
void renderGrain(Grain& g, float* left, float* right, int n, GrainScratch& scratch) noexcept;

/// The interpolator's reach, for planning reads inside a level: taps before and after a frame.
[[nodiscard]] constexpr int tapsBefore(Interp i) noexcept
{
    return i == Interp::Linear ? 0 : i == Interp::Opt4 ? 1 : i == Interp::Opt6 ? 2 : 3;
}
[[nodiscard]] constexpr int tapsAfter(Interp i) noexcept
{
    return i == Interp::Linear ? 1 : i == Interp::Opt4 ? 2 : i == Interp::Opt6 ? 3 : 4;
}
} // namespace fd::devices::granular
