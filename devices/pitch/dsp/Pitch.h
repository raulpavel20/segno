#pragma once

#include "../../midi-shared/Harmony.h"

#include <fd/device/Device.h>
#include <fd/device/Registry.h>

namespace fd::devices
{
/// Pitch (devices/pitch/SPEC.md): moves, fits, mirrors, varies and limits notes. Drawn in step 0;
/// its processing comes with step 1.
class Pitch final : public Device
{
public:
    enum Param : std::size_t
    {
        Semitones,
        Steps,
        Mirror,
        Axis,
        Fit,
        Toward,
        Passing,
        Change,
        Chance,
        Spread,
        Way,
        Lowest,
        Highest,
        Outside,
        HarmonyFirst, // then harmony::Offset
        Count = HarmonyFirst + harmony::Count
    };

    Pitch();
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(ProcessContext& context) noexcept override;
};

const DeviceFactory& pitchFactory();
} // namespace fd::devices
