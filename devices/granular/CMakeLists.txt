fd_add_device(ID granular DSP_SOURCES dsp/Granular.cpp dsp/GrainSource.cpp dsp/GrainKernel.cpp)
# The Sampler's SFZ reader: a multisample (the Library's instruments) as grain sources.
target_link_libraries(fd_device_granular_dsp PUBLIC fd::audiofile fd_device_sampler_dsp)
# The source loader (the pyramid's filters over every zone of a multisample) is optimised in Debug
# too: unoptimised, a Library instrument takes ten seconds to load.
set_source_files_properties(dsp/GrainSource.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")

fd_add_test(NAME fd_granular_tests
    SOURCES tests/GrainWindowTests.cpp tests/GranularTests.cpp
    LIBS fd_device_granular_dsp fd::testkit_device)

fd_add_device_ui(ID granular SOURCES ui/GranularPanel.cpp ui/GranularUi.cpp)
