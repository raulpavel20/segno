#include "model/MidiEffects.h"

#include <algorithm>

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
} // namespace fd::model
