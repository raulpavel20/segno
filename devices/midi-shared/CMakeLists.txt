# What the MIDI effects share (docs/design/midi-effects.md §15): the HARMONY parameters, the
# follower, and the base every module plays its notes through. JUCE-free, as the devices' DSP.
add_library(fd_midi_shared STATIC dsp/Harmony.cpp dsp/Follower.cpp dsp/NoteEffect.cpp dsp/NoteRun.cpp)
target_include_directories(fd_midi_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/dsp")
target_link_libraries(fd_midi_shared PUBLIC fd::device fd::music PRIVATE fd::warnings)

fd_add_test(NAME fd_midi_shared_tests
    SOURCES tests/NoteRunTests.cpp
    LIBS fd_midi_shared fd_device_pitch_dsp fd::testkit_device)
