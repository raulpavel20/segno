fd_add_device(ID chorus DSP_SOURCES dsp/Chorus.cpp)

fd_add_test(NAME fd_chorus_tests
    SOURCES tests/ChorusTests.cpp
    LIBS fd_device_chorus_dsp fd::testkit_device)
target_compile_definitions(fd_chorus_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID chorus SOURCES ui/ChorusPanel.cpp)
