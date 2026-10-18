/*
BEGIN_JUCE_MODULE_DECLARATION

  ID:                 fd_device_ui
  vendor:             segno
  version:            0.0.1
  name:               Segno device UI
  description:        DevicePanel base, host-agnostic parameter binding, shared displays, panel registry
  license:            AGPLv3
  minimumCppStandard: 20
  dependencies:       fd_ui

END_JUCE_MODULE_DECLARATION
*/

#pragma once
#define FD_DEVICE_UI_H_INCLUDED

// clang-format off: the order matters (each part needs the ones before it)
#include <fd_ui/fd_ui.h>

#include <fd/device/Device.h>
#include <fd/device/Presets.h>
#include <fd/music/Chords.h>
#include <fd/music/Following.h>
#include <fd/music/Key.h>
#include <fd/dsp/DrawnCurve.h>
#include <fd/dsp/Modulation.h>
#include <fd/dsp/Spectrum.h>
#include <fd/dsp/SynthBlocks.h>
#include <fd/dsp/VoiceModules.h>

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <set>

namespace fd::ui { class ModulationModel; class ModuleEditor; }
#include "panel/ParamBinding.h"
#include "panel/ParamControls.h"
#include "panel/PanelSection.h"
#include "panel/PresetBar.h"
#include "panel/DevicePanel.h"
#include "panel/ModuleEditor.h"
#include "panel/Modulation.h"
#include "panel/Displays.h"
#include "panel/NoteDisplay.h"
#include "panel/NoteTapFeed.h"
#include "panel/MidiParts.h"
#include "panel/CurveEditor.h"
#include "panel/SynthParts.h"
#include "panel/PanelRegistry.h"
// clang-format on
