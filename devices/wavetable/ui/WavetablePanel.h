#pragma once

#include <fd_device_ui/fd_device_ui.h>

namespace fd::devices
{
/// The Wavetable Synth's panel (devices/wavetable/SPEC.md, "UI layout").
const ui::PanelFactory& wavetablePanelFactory();
} // namespace fd::devices
