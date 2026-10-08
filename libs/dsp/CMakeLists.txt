add_library(fd_dsp STATIC src/Filters.cpp src/Oversampler.cpp src/Waveshapers.cpp)
add_library(fd::dsp ALIAS fd_dsp)
target_include_directories(fd_dsp PUBLIC include)
target_link_libraries(fd_dsp PRIVATE fd::warnings)

fd_add_test(NAME fd_dsp_tests
    SOURCES tests/MathTests.cpp tests/SmootherTests.cpp tests/ControlGridTests.cpp tests/FilterTests.cpp
        tests/OversamplerTests.cpp tests/WaveshaperTests.cpp
    LIBS fd::dsp fd::testkit)
