#pragma once

#include "../../midi-shared/Harmony.h"

#include <fd/device/Device.h>
#include <fd/device/Registry.h>

namespace fd::devices
{
/// Chord (devices/chord/SPEC.md): adds notes at intervals, plays the song's chord, or the key's
/// chord for each key; strums them. Drawn in step 0; its processing comes with step 1.
class Chord final : public Device
{
public:
    enum Param : std::size_t
    {
        Mode,
        Shift1,
        Shift6 = Shift1 + 5,
        InKey,
        Level,
        Kind,
        Voicing,
        Place,
        Change,
        Around,
        Strum,
        Curve,
        Swell,
        Direction,
        HarmonyFirst,
        Count = HarmonyFirst + harmony::Count
    };
    enum ModeChoice
    {
        Add,
        Song,
        Keys
    };

    Chord();
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(ProcessContext& context) noexcept override;
};

const DeviceFactory& chordFactory();
} // namespace fd::devices
