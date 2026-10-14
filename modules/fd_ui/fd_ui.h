/*
BEGIN_JUCE_MODULE_DECLARATION

  ID:                 fd_ui
  vendor:             segno
  version:            0.0.1
  name:               Segno UI
  description:        Design tokens, fonts, LookAndFeel and controls (docs/design/ui-principles.md)
  license:            AGPLv3
  minimumCppStandard: 20
  dependencies:       juce_gui_basics

END_JUCE_MODULE_DECLARATION
*/

#pragma once
#define FD_UI_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "theme/Theme.h"
#include "theme/Fonts.h"
#include "theme/LookAndFeel.h"
#include "controls/Automation.h"
#include "controls/Knob.h"
#include "controls/Toggle.h"
#include "controls/ViewOption.h"
#include "controls/ViewChoice.h"
#include "controls/MenuButton.h"
#include "controls/Segments.h"
#include "controls/Meter.h"
#include "controls/Icons.h"
#include "controls/NumberField.h"
#include "controls/ChoiceField.h"
#include "controls/OctaveKeys.h"
#include "controls/Labels.h"
#include "shell/ShellParts.h"
#include "shell/Brand.h"
#include "shell/WindowButtons.h"
#include "shell/NameEditor.h"
#include "shell/ColourGrid.h"
#include "shell/Focus.h"
