fd_add_device(ID wavetable DSP_SOURCES dsp/WavetableSynth.cpp dsp/FactoryTables.cpp dsp/TableFiles.cpp
    dsp/Formula.cpp dsp/TableRecipe.cpp dsp/AudioToTable.cpp)
target_link_libraries(fd_device_wavetable_dsp PUBLIC fd::audiofile)
# The AKWF factory tables: their cycles fetched and embedded (cmake/FdAkwf.cmake).
fd_generate_akwf_tables(RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/tables/akwf.json"
                        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/AkwfTables.cpp")
target_sources(fd_device_wavetable_dsp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/AkwfTables.cpp")

fd_add_test(NAME fd_wavetable_tests
    SOURCES tests/WavetableSynthTests.cpp tests/TableToolsTests.cpp
    LIBS fd_device_wavetable_dsp fd::testkit_device)

fd_add_device_ui(ID wavetable SOURCES ui/WavetablePanel.cpp ui/WavetableUi.cpp ui/TableEditor.cpp)
