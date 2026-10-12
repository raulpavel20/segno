fd_add_device(ID filter DSP_SOURCES dsp/Filter.cpp)

fd_add_test(NAME fd_filter_tests
    SOURCES tests/FilterTests.cpp
    LIBS fd_device_filter_dsp fd::testkit_device)
target_compile_definitions(fd_filter_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID filter SOURCES ui/FilterPanel.cpp)
