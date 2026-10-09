fd_add_device(ID gate DSP_SOURCES dsp/Gate.cpp)

fd_add_test(NAME fd_gate_tests
    SOURCES tests/GateTests.cpp
    LIBS fd_device_gate_dsp fd::testkit_device)
target_compile_definitions(fd_gate_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

fd_add_device_ui(ID gate SOURCES ui/GatePanel.cpp)
