fd_add_device(ID phaser-flanger DSP_SOURCES dsp/PhaserFlanger.cpp)

fd_add_test(NAME fd_phaser_flanger_tests
    SOURCES tests/PhaserFlangerTests.cpp
    LIBS fd_device_phaser_flanger_dsp fd::testkit_device)
target_compile_definitions(fd_phaser_flanger_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID phaser-flanger SOURCES ui/PhaserFlangerPanel.cpp)
