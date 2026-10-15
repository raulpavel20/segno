fd_add_device(ID tuner DSP_SOURCES dsp/Tuner.cpp)

fd_add_test(NAME fd_tuner_tests
    SOURCES tests/TunerTests.cpp
    LIBS fd_device_tuner_dsp fd::testkit_device)

fd_add_device_ui(ID tuner SOURCES ui/TunerPanel.cpp)
if(TARGET fd_device_tuner_ui)
    # Names spelled in the song's key (key-and-scale.md §4).
    target_link_libraries(fd_device_tuner_ui INTERFACE fd::music)
endif()
