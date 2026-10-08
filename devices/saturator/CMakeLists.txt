fd_add_device(ID saturator DSP_SOURCES dsp/Saturator.cpp)

fd_add_test(NAME fd_saturator_tests
    SOURCES tests/SaturatorTests.cpp
    LIBS fd_device_saturator_dsp fd::testkit_device)
target_compile_definitions(fd_saturator_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID saturator SOURCES ui/SaturatorPanel.cpp)
