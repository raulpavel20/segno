fd_add_device(ID chord DSP_SOURCES dsp/Chord.cpp)
target_link_libraries(fd_device_chord_dsp PUBLIC fd_midi_shared)

fd_add_test(NAME fd_chord_tests
    SOURCES tests/ChordTests.cpp
    LIBS fd_device_chord_dsp fd::testkit_device)

fd_add_device_ui(ID chord SOURCES ui/ChordPanel.cpp)
