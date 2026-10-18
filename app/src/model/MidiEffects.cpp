#include "model/MidiEffects.h"

#include <algorithm>
#include <array>

namespace fd::model
{
int placeInChain(std::span<const ChainRole> chain, int index, ChainRole role) noexcept
{
    if (role != ChainRole::MidiEffect)
        return index;
    const auto player = std::find(chain.begin(), chain.end(), ChainRole::Player);
    if (player == chain.end())
        return index;
    const auto before = static_cast<int>(player - chain.begin());
    return index < 0 || index > before ? before : index;
}

namespace
{
constexpr std::array<std::string_view, 3> useIds{"auto", "on", "off"};
constexpr std::array<std::string_view, 4> changeIds{"bar", "half-bar", "beat", "any"};
} // namespace

std::string_view idOf(HarmonyUse use) noexcept
{
    return useIds[static_cast<std::size_t>(use)];
}

std::optional<HarmonyUse> harmonyUseOf(std::string_view id) noexcept
{
    for (std::size_t i = 0; i < useIds.size(); ++i)
        if (useIds[i] == id)
            return static_cast<HarmonyUse>(i);
    return std::nullopt;
}

std::string_view idOf(ChordChange change) noexcept
{
    return changeIds[static_cast<std::size_t>(change)];
}

std::optional<ChordChange> chordChangeOf(std::string_view id) noexcept
{
    for (std::size_t i = 0; i < changeIds.size(); ++i)
        if (changeIds[i] == id)
            return static_cast<ChordChange>(i);
    return std::nullopt;
}

double shortestChord(ChordChange change, int numerator, int denominator) noexcept
{
    const double bar = std::max(1, numerator) * 4.0 / std::max(1, denominator);
    switch (change)
    {
    case ChordChange::Bar:
        return bar;
    case ChordChange::HalfBar:
        return bar / 2.0;
    case ChordChange::Beat:
        return 1.0;
    case ChordChange::Any:
        break;
    }
    return 0.0;
}
} // namespace fd::model
