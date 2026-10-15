add_library(fd_dsp STATIC src/Fft.cpp src/Oscillator.cpp src/SynthBlocks.cpp src/Filters.cpp src/MatchedFilters.cpp src/Oversampler.cpp src/IirOversampler.cpp src/Waveshapers.cpp
    src/Resampling.cpp src/Dynamics.cpp src/Loudness.cpp src/Spectrum.cpp src/Modulation.cpp src/SpectralFilter.cpp
    src/SpectralDynamics.cpp
    src/Wavetable.cpp src/WavetableOscillator.cpp
    src/VoiceModules.cpp src/Modulators.cpp)
add_library(fd::dsp ALIAS fd_dsp)
target_include_directories(fd_dsp PUBLIC include)
target_link_libraries(fd_dsp PUBLIC fd_dep_elliptic_blep PRIVATE fd::warnings)

fd_add_test(NAME fd_dsp_tests
    SOURCES tests/MathTests.cpp tests/SmootherTests.cpp tests/ControlGridTests.cpp tests/FilterTests.cpp
        tests/OversamplerTests.cpp tests/IirOversamplerTests.cpp tests/WaveshaperTests.cpp tests/MatchedFilterTests.cpp tests/FftTests.cpp tests/OscillatorTests.cpp tests/SynthBlockTests.cpp
        tests/ResamplingTests.cpp tests/DynamicsTests.cpp tests/LoudnessTests.cpp tests/SpectrumTests.cpp tests/SpectralFilterTests.cpp
        tests/SpectralDynamicsTests.cpp
        tests/WavetableTests.cpp
        tests/VoiceModulesTests.cpp
        tests/ModulationTests.cpp
        tests/ModulatorsTests.cpp
    LIBS fd::dsp fd::testkit)
