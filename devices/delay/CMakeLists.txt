fd_add_device(ID delay DSP_SOURCES dsp/Delay.cpp)

fd_add_test(NAME fd_delay_tests
    SOURCES tests/DelayTests.cpp
    LIBS fd_device_delay_dsp fd::testkit_device)
target_compile_definitions(fd_delay_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID delay SOURCES ui/DelayPanel.cpp)
