add_library(fd_dsp STATIC src/Fft.cpp src/Oscillator.cpp src/SynthBlocks.cpp src/Filters.cpp src/MatchedFilters.cpp src/Oversampler.cpp src/Waveshapers.cpp)
add_library(fd::dsp ALIAS fd_dsp)
target_include_directories(fd_dsp PUBLIC include)
target_link_libraries(fd_dsp PUBLIC fd_dep_elliptic_blep PRIVATE fd::warnings)

fd_add_test(NAME fd_dsp_tests
    SOURCES tests/MathTests.cpp tests/SmootherTests.cpp tests/ControlGridTests.cpp tests/FilterTests.cpp
        tests/OversamplerTests.cpp tests/WaveshaperTests.cpp tests/MatchedFilterTests.cpp tests/FftTests.cpp tests/OscillatorTests.cpp tests/SynthBlockTests.cpp
    LIBS fd::dsp fd::testkit)
