fd_add_device(ID granular DSP_SOURCES dsp/Granular.cpp dsp/GrainSource.cpp)
target_link_libraries(fd_device_granular_dsp PUBLIC fd::audiofile)

fd_add_test(NAME fd_granular_tests
    SOURCES tests/GrainWindowTests.cpp
    LIBS fd_device_granular_dsp fd::testkit_device)

fd_add_device_ui(ID granular SOURCES ui/GranularPanel.cpp ui/GranularUi.cpp)
