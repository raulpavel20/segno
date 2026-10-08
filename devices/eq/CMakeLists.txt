fd_add_device(ID eq DSP_SOURCES dsp/Eq.cpp)

fd_add_test(NAME fd_eq_tests
    SOURCES tests/EqTests.cpp
    LIBS fd_device_eq_dsp fd::testkit_device)

fd_add_device_ui(ID eq SOURCES ui/EqPanel.cpp)
