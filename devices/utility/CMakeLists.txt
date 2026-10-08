fd_add_device(ID utility DSP_SOURCES dsp/Utility.cpp)

fd_add_test(NAME fd_utility_tests
    SOURCES tests/UtilityTests.cpp
    LIBS fd_device_utility_dsp fd::testkit_device)

fd_add_device_ui(ID utility SOURCES ui/UtilityPanel.cpp)
