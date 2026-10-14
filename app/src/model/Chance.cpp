#include "model/Chance.h"

#include <algorithm>
#include <cmath>

namespace fd::model
{
namespace
{
/// SplitMix64's finaliser: every bit of the input moves every bit of the output.
constexpr std::uint64_t mix(std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

/// A number in [0, 1) from a hash.
constexpr double unit(std::uint64_t h) noexcept
{
    return static_cast<double>(h >> 11U) * 0x1.0p-53;
}

constexpr double kTicksPerBeat = 960.0; // a note's place, to a tick: the same however it's stored
} // namespace

NoteFate noteFate(const Note& note, double place, int lap, std::uint32_t seed, std::uint64_t clip) noexcept
{
    NoteFate fate{.plays = true, .velocity = note.velocity};
    if (note.chance >= 1.0f && note.velocitySpread <= 0.0f)
        return fate;
    const auto ticks = static_cast<std::uint64_t>(std::llround(std::max(0.0, place) * kTicksPerBeat));
    const std::uint64_t h = mix(seed ^ mix(clip ^ mix(static_cast<std::uint64_t>(note.pitch) ^
                                                      mix(ticks ^ mix(static_cast<std::uint64_t>(lap))))));
    if (note.chance < 1.0f)
        fate.plays = unit(mix(h ^ 1U)) < static_cast<double>(std::max(0.0f, note.chance));
    if (note.velocitySpread > 0.0f)
    {
        const double by = (unit(mix(h ^ 2U)) * 2.0 - 1.0) * static_cast<double>(note.velocitySpread);
        const auto steps = std::lround((static_cast<double>(note.velocity) + by) * 127.0);
        fate.velocity = static_cast<float>(std::clamp<long>(steps, 1, 127)) / 127.0f;
    }
    return fate;
}

bool usesChance(const std::vector<Note>& notes) noexcept
{
    return std::any_of(notes.begin(), notes.end(),
                       [](const Note& n) { return n.chance < 1.0f || n.velocitySpread > 0.0f; });
}
} // namespace fd::model
