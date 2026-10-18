fd_add_device(ID arpeggiator DSP_SOURCES dsp/Arpeggiator.cpp)
target_link_libraries(fd_device_arpeggiator_dsp PUBLIC fd_midi_shared)

fd_add_test(NAME fd_arpeggiator_tests
    SOURCES tests/ArpeggiatorTests.cpp
    LIBS fd_device_arpeggiator_dsp fd::testkit_device)

fd_add_device_ui(ID arpeggiator SOURCES ui/ArpeggiatorPanel.cpp)
