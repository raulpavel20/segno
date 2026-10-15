#include "fd/dsp/Modulators.h"

#include "fd/dsp/VoiceModules.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fd::dsp::mod
{
namespace
{
// Each note value's length in beats; negative: bars.
constexpr double kSyncBeats[] = {0.0,     1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3,
                                 3.0 / 8, 1.0 / 2,  2.0 / 3,  3.0 / 4, 1.0,     4.0 / 3, 3.0 / 2,
                                 2.0,     3.0,      -1.0,     -2.0,    -4.0,    -8.0};
static_assert(std::size(kSyncBeats) == numSyncValues && std::size(syncValues) == numSyncValues);

double fraction(double x) noexcept
{
    return x - std::floor(x);
}

double plain(Shape shape, double p, const CurveTable* drawn) noexcept
{
    switch (shape)
    {
    case Shape::Sine:
        return std::sin(2.0 * std::numbers::pi * p);
    case Shape::Triangle:
        return p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0;
    case Shape::SawUp:
        return 2.0 * p - 1.0;
    case Shape::SawDown:
        return 1.0 - 2.0 * p;
    case Shape::Square:
        return p < 0.5 ? 1.0 : -1.0;
    case Shape::Drawn:
        return drawn != nullptr ? 2.0 * lookup(*drawn, p) - 1.0 : std::sin(2.0 * std::numbers::pi * p);
    }
    return 0.0;
}

/// Half a cosine from 0 to 1: how a glide moves.
double eased(double u) noexcept
{
    return 0.5 - 0.5 * std::cos(std::numbers::pi * std::clamp(u, 0.0, 1.0));
}

/// Where step `n` starts, moved by up to 45 % of a step: neighbours stay at least a tenth apart.
double stepStart(std::uint64_t seed, std::uint64_t copy, std::int64_t n, double jitter) noexcept
{
    constexpr std::uint64_t kTimingDice = 0x5EEDull; // apart from the values' dice
    return static_cast<double>(n) + 0.45 * std::clamp(jitter, 0.0, 1.0) *
                                        dice(seed ^ kTimingDice, copy, static_cast<std::uint64_t>(n));
}

/// Random's value for step `n`: the dice, pulled toward up-and-down in turn by Cyclic.
double stepValue(std::uint64_t seed, std::uint64_t copy, std::int64_t n, double cyclic) noexcept
{
    const double r = dice(seed, copy, static_cast<std::uint64_t>(n));
    const double turn = (n % 2 != 0 ? 1.0 : -1.0) * (0.5 + 0.5 * std::abs(r));
    const double c = std::clamp(cyclic, 0.0, 1.0);
    return (1.0 - c) * r + c * turn;
}
} // namespace

double syncBeats(int index, double beatsPerBar) noexcept
{
    const double b = kSyncBeats[std::clamp(index, 0, numSyncValues - 1)];
    return b < 0.0 ? -b * beatsPerBar : b;
}

float dice(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept
{
    std::uint64_t z = a * 0x9E3779B97F4A7C15ull + b * 0xBF58476D1CE4E5B9ull + c * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(static_cast<double>(z >> 11) / static_cast<double>(1ull << 53) * 2.0 - 1.0);
}

double slanted(double phase, double slant) noexcept
{
    const double p = fraction(phase);
    const double middle = std::clamp(0.5 * (1.0 + slant), 0.001, 0.999);
    return p < middle ? 0.5 * p / middle : 0.5 + 0.5 * (p - middle) / (1.0 - middle);
}

float lfo(Shape shape, double phase, double slant, double smooth, const CurveTable* drawn) noexcept
{
    const double s = std::clamp(smooth, 0.0, 1.0);
    if (s <= 0.0)
        return static_cast<float>(plain(shape, slanted(phase, slant), drawn));
    // Smooth: the shape's average over a window up to half a cycle wide, centred on the phase.
    constexpr int kTaps = 24;
    const double window = 0.5 * s;
    double sum = 0.0;
    for (int i = 0; i < kTaps; ++i)
    {
        const double offset = window * ((i + 0.5) / kTaps - 0.5);
        sum += plain(shape, slanted(phase + offset, slant), drawn);
    }
    return static_cast<float>(sum / kTaps);
}

double spread(int copy, int copies) noexcept
{
    return copies > 1 ? static_cast<double>(copy % copies) / copies : 0.0;
}

std::int64_t stepAt(std::uint64_t seed, std::uint64_t copy, double x, double jitter, double& within) noexcept
{
    auto n = static_cast<std::int64_t>(std::floor(x));
    if (x < stepStart(seed, copy, n, jitter))
        --n;
    else if (x >= stepStart(seed, copy, n + 1, jitter))
        ++n;
    const double start = stepStart(seed, copy, n, jitter), end = stepStart(seed, copy, n + 1, jitter);
    within = std::clamp((x - start) / (end - start), 0.0, 1.0);
    return n;
}

float random(std::uint64_t seed, std::uint64_t copy, double x, const Wander& wander) noexcept
{
    double within = 0.0;
    const auto n = stepAt(seed, copy, x, wander.jitter, within);
    const double to = stepValue(seed, copy, n, wander.cyclic),
                 from = stepValue(seed, copy, n - 1, wander.cyclic);
    // Smooth is the share of the step spent gliding; Slant shortens rises (toward -1) or falls.
    const double slant = std::clamp(wander.slant, -1.0, 1.0);
    const double share = std::clamp(wander.smooth, 0.0, 1.0) * (to > from ? 1.0 + slant : 1.0 - slant);
    if (share <= 0.0)
        return static_cast<float>(to);
    return static_cast<float>(from + (to - from) * eased(within / std::min(share, 1.0)));
}

bool stepPlays(std::uint64_t seed, std::uint64_t copy, std::int64_t n, double chance) noexcept
{
    if (chance >= 1.0)
        return true;
    if (chance <= 0.0)
        return false;
    constexpr std::uint64_t kChanceDice = 0xC4A9CEull;
    return 0.5 * (1.0 + dice(seed ^ kChanceDice, copy, static_cast<std::uint64_t>(n))) < chance;
}

float steps(std::span<const float> values, std::span<const float> chances, std::uint64_t seed,
            std::uint64_t copy, double x, double smooth, double jitter) noexcept
{
    if (values.empty())
        return 0.0f;
    const auto count = static_cast<std::int64_t>(values.size());
    const auto at = [&](std::int64_t n)
    {
        const auto i = static_cast<size_t>(((n % count) + count) % count);
        const double chance = i < chances.size() ? chances[i] : 1.0;
        return stepPlays(seed, copy, n, chance) ? static_cast<double>(values[i]) : 0.0;
    };
    double within = 0.0;
    const auto n = stepAt(seed, copy, x, jitter, within);
    const double to = at(n);
    const double share = std::clamp(smooth, 0.0, 1.0);
    if (share <= 0.0)
        return static_cast<float>(to);
    const double from = at(n - 1);
    return static_cast<float>(from + (to - from) * eased(within / share));
}
namespace
{
double bent(double from, double to, double u, double curve) noexcept
{
    return from + (to - from) * CurvedEnvelope::shape(std::clamp(u, 0.0, 1.0), curve);
}

/// Before any release: the delay, the rise, the hold, the fall to Sustain, then Sustain (Loop:
/// attack to decay again and again; One-shot: no Sustain held, see `oneShot`).
double held(const EnvelopeShape& s, double ms) noexcept
{
    double t = ms - s.delayMs;
    if (t < 0.0)
        return 0.0;
    const double a = std::max(0.0, s.attackMs), h = std::max(0.0, s.holdMs), d = std::max(1.0e-3, s.decayMs);
    if (s.mode == EnvelopeShape::Mode::Loop && t >= a + h + d)
        t = std::fmod(t, a + h + d);
    if (t < a)
        return bent(0.0, 1.0, t / a, s.attackCurve);
    if (t < a + h)
        return 1.0;
    if (t < a + h + d)
        return bent(1.0, s.sustain, (t - a - h) / d, s.decayCurve);
    return s.sustain;
}

/// One firing played through (One-shot, and each of Repeat's): delay, rise, hold, fall, release.
double cycleLength(const EnvelopeShape& s) noexcept
{
    return std::max(0.0, s.delayMs) + std::max(0.0, s.attackMs) + std::max(0.0, s.holdMs) +
           std::max(1.0e-3, s.decayMs) + std::max(1.0e-3, s.releaseMs);
}

double released(const EnvelopeShape& s, double ms, double at) noexcept
{
    const double level = held(s, at);
    const double r = std::max(1.0e-3, s.releaseMs);
    return ms - at >= r ? 0.0 : bent(level, 0.0, (ms - at) / r, s.releaseCurve);
}

double oneShot(const EnvelopeShape& s, double ms) noexcept
{
    const double end = cycleLength(s) - std::max(1.0e-3, s.releaseMs); // where it falls to Sustain
    return ms < end ? held(s, ms) : released(s, ms, end);
}
} // namespace

double envelope(const EnvelopeShape& shape, double ms, double heldMs) noexcept
{
    if (ms < 0.0)
        return 0.0;
    if (shape.repeats > 0 || shape.mode == EnvelopeShape::Mode::OneShot)
    {
        // Played through, again after itself Repeat more times.
        const double length = cycleLength(shape);
        const auto lap = static_cast<int>(std::floor(ms / length));
        return lap > std::max(0, shape.repeats) ? 0.0 : oneShot(shape, ms - lap * length);
    }
    return heldMs < 0.0 || ms < heldMs ? held(shape, ms) : released(shape, ms, heldMs);
}

bool envelopeDone(const EnvelopeShape& shape, double ms, double heldMs) noexcept
{
    if (shape.repeats > 0 || shape.mode == EnvelopeShape::Mode::OneShot)
        return ms >= cycleLength(shape) * (std::max(0, shape.repeats) + 1);
    return heldMs >= 0.0 && ms >= heldMs + std::max(1.0e-3, shape.releaseMs);
}
} // namespace fd::dsp::mod
