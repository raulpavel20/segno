fd_add_device(ID pitch DSP_SOURCES dsp/Pitch.cpp)
target_link_libraries(fd_device_pitch_dsp PUBLIC fd_midi_shared)

fd_add_test(NAME fd_pitch_tests
    SOURCES tests/PitchTests.cpp
    LIBS fd_device_pitch_dsp fd::testkit_device)

fd_add_device_ui(ID pitch SOURCES ui/PitchPanel.cpp)
