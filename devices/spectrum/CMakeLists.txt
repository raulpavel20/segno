fd_add_device(ID spectrum DSP_SOURCES dsp/Spectrum.cpp)
target_link_libraries(fd_device_spectrum_dsp PUBLIC fd::audiofile)

fd_add_test(NAME fd_spectrum_tests
    SOURCES tests/SpectrumTests.cpp
    LIBS fd_device_spectrum_dsp fd::testkit_device)

fd_add_device_ui(ID spectrum SOURCES ui/SpectrumPanel.cpp)
