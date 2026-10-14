fd_add_device(ID sampler DSP_SOURCES dsp/Sampler.cpp dsp/SampleData.cpp dsp/SampleMap.cpp dsp/Sfz.cpp)
target_link_libraries(fd_device_sampler_dsp PUBLIC fd::audiofile)

fd_add_test(NAME fd_sampler_tests
    SOURCES tests/SamplerTests.cpp
    LIBS fd_device_sampler_dsp fd::testkit_device)
target_compile_definitions(fd_sampler_tests PRIVATE FD_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden"
    FD_SAMPLER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/vsco-2-ce")

fd_add_device_ui(ID sampler SOURCES ui/SamplerPanel.cpp)
