fd_add_device(ID loudness-meter DSP_SOURCES dsp/LoudnessMeter.cpp)

fd_add_test(NAME fd_loudness_meter_tests
    SOURCES tests/LoudnessMeterTests.cpp
    LIBS fd_device_loudness_meter_dsp fd::testkit_device)

fd_add_device_ui(ID loudness-meter SOURCES ui/LoudnessMeterPanel.cpp)
