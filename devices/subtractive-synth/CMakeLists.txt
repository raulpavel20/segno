fd_add_device(ID subtractive-synth DSP_SOURCES dsp/SubtractiveSynth.cpp)

fd_add_test(NAME fd_subtractive_synth_tests
    SOURCES tests/SubtractiveSynthTests.cpp
    LIBS fd_device_subtractive_synth_dsp fd::testkit_device)
target_compile_definitions(fd_subtractive_synth_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID subtractive-synth SOURCES ui/SubtractiveSynthPanel.cpp)
