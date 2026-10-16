#pragma once

#include <fd/device/Presets.h>
#include <span>

namespace fd::model
{
/// The factory presets of the DAW's own modules, from app/presets/<type>/: the modulators' (about
/// eight each, modulators.md §14, ADR 0044) and the Drum Kit's kits (drum-kit.md §27). The
/// devices' own are fd::factoryPresetFiles().
[[nodiscard]] std::span<const FactoryPresetFile> dawFactoryPresets();
} // namespace fd::model
