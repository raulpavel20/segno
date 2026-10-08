/*
BEGIN_JUCE_MODULE_DECLARATION

  ID:                 fd_ui
  vendor:             foss-daw
  version:            0.0.1
  name:               foss-daw UI
  description:        Design tokens, fonts, LookAndFeel and controls (docs/design/ui-principles.md)
  license:            AGPLv3
  minimumCppStandard: 20
  dependencies:       juce_gui_basics

END_JUCE_MODULE_DECLARATION
*/

#pragma once
#define FD_UI_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>

#include "theme/Theme.h"
#include "theme/Fonts.h"
#include "theme/LookAndFeel.h"
#include "controls/Knob.h"
#include "controls/Toggle.h"
#include "controls/Meter.h"
#include "controls/Icons.h"
