# JUCE-free audio file decoding (WAV/W64/RF64/AIFF, FLAC, MP3) for devices and tools.
add_library(fd_audiofile STATIC src/AudioFile.cpp)
add_library(fd::audiofile ALIAS fd_audiofile)
target_include_directories(fd_audiofile PUBLIC include)
target_link_libraries(fd_audiofile PRIVATE fd_dep_dr_libs fd::warnings)

fd_add_test(NAME fd_audiofile_tests
    SOURCES tests/AudioFileTests.cpp
    LIBS fd::audiofile)
