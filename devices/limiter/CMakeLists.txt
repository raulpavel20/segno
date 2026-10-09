fd_add_device(ID limiter DSP_SOURCES dsp/Limiter.cpp)

fd_add_test(NAME fd_limiter_tests
    SOURCES tests/LimiterTests.cpp
    LIBS fd_device_limiter_dsp fd::testkit_device)
target_compile_definitions(fd_limiter_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID limiter SOURCES ui/LimiterPanel.cpp)
