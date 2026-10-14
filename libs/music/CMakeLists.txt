# JUCE-free music theory shared by the DAW, the Push support and devices (ADR 0057): the scale
# catalogue, keys and their spelling, and finding a key from notes.
add_library(fd_music STATIC src/Scales.cpp src/Key.cpp src/Detect.cpp)
add_library(fd::music ALIAS fd_music)
target_include_directories(fd_music PUBLIC include)
target_link_libraries(fd_music PRIVATE fd::warnings)

fd_add_test(NAME fd_music_tests
    SOURCES tests/ScaleTests.cpp tests/KeyTests.cpp tests/DetectTests.cpp
    LIBS fd::music)
