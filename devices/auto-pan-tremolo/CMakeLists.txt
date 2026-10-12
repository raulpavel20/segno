fd_add_device(ID auto-pan-tremolo DSP_SOURCES dsp/AutoPanTremolo.cpp)

fd_add_test(NAME fd_auto_pan_tremolo_tests
    SOURCES tests/AutoPanTremoloTests.cpp
    LIBS fd_device_auto_pan_tremolo_dsp fd::testkit_device)
target_compile_definitions(fd_auto_pan_tremolo_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID auto-pan-tremolo SOURCES ui/AutoPanTremoloPanel.cpp)
