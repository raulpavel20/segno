fd_add_device(ID wavetable DSP_SOURCES dsp/WavetableSynth.cpp dsp/FactoryTables.cpp dsp/TableFiles.cpp)
target_link_libraries(fd_device_wavetable_dsp PUBLIC fd::audiofile)

fd_add_test(NAME fd_wavetable_tests
    SOURCES tests/WavetableSynthTests.cpp
    LIBS fd_device_wavetable_dsp fd::testkit_device)
