/*
BEGIN_JUCE_MODULE_DECLARATION

  ID:                 fd_host_juce
  vendor:             segno
  version:            0.0.1
  name:               Segno JUCE host helpers
  description:        Conversions between JUCE and the JUCE-free device API (MIDI, state)
  license:            AGPLv3
  minimumCppStandard: 20
  dependencies:       juce_audio_basics, juce_core

END_JUCE_MODULE_DECLARATION
*/

#pragma once
#define FD_HOST_JUCE_H_INCLUDED

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <fd/device/Device.h>
#include <fd/device/State.h>

#include "host/Conversions.h"
