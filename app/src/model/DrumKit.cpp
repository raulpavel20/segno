#include "model/DrumKit.h"

#include <algorithm>
#include <fd/music/Key.h>

namespace fd::model::kit
{
int shownFrom(int from) noexcept
{
    return std::clamp(from - ((from % columns) + columns) % columns, 0, padCount - shown);
}

const RackChain* padOn(const RackInfo& kit, int note) noexcept
{
    for (const auto& c : kit.chains)
        if (c.note == note)
            return &c;
    return nullptr;
}

std::bitset<padCount> padsWithSounds(const RackInfo& kit) noexcept
{
    std::bitset<padCount> notes;
    for (const auto& c : kit.chains)
        if (c.note >= 0 && c.note < padCount)
            notes.set(static_cast<size_t>(c.note));
    return notes;
}

const DeviceSlot* trackKit(const std::vector<DeviceSlot>& devices) noexcept
{
    for (const auto& d : devices)
        if (d.rack != nullptr && d.rack->kit)
            return &d;
    return nullptr;
}

std::string noteName(int note)
{
    return music::noteName(nullptr, std::clamp(note, 0, padCount - 1));
}

std::string contents(const RackInfo& kit)
{
    const auto n = padsWithSounds(kit).count();
    if (n == 0)
        return "No pads";
    return n == 1 ? std::string("1 pad") : std::to_string(n) + " pads";
}

std::string frameLabel(const RackInfo& kit)
{
    const auto note = noteName(kit.selectedPad);
    const auto* pad = padOn(kit, kit.selectedPad);
    if (pad == nullptr || pad->name.empty() || pad->name == note)
        return "Pad " + note;
    return "Pad " + note + " \xc2\xb7 " + pad->name;
}

std::string chokeName(int group)
{
    return group <= 0 ? std::string("None") : std::to_string(std::min(group, chokeGroups));
}
} // namespace fd::model::kit
