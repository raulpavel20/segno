fd_add_device(ID compressor DSP_SOURCES dsp/Compressor.cpp)

fd_add_test(NAME fd_compressor_tests
    SOURCES tests/CompressorTests.cpp
    LIBS fd_device_compressor_dsp fd::testkit_device)
target_compile_definitions(fd_compressor_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID compressor SOURCES ui/CompressorPanel.cpp)
