fd_add_device(ID reverb DSP_SOURCES dsp/Reverb.cpp)

fd_add_test(NAME fd_reverb_tests
    SOURCES tests/ReverbTests.cpp
    LIBS fd_device_reverb_dsp fd::testkit_device)
target_compile_definitions(fd_reverb_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID reverb SOURCES ui/ReverbPanel.cpp)
