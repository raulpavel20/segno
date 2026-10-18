# JUCE-free music theory shared by the DAW, the Push support and devices (ADR 0057): the scale
# catalogue, keys and their spelling, finding a key from notes, and chords: their names, the key's
# chords, voicings and suggestions (ADR 0058).
add_library(fd_music STATIC src/Scales.cpp src/Key.cpp src/Detect.cpp src/Chords.cpp src/Following.cpp src/Arpeggio.cpp)
add_library(fd::music ALIAS fd_music)
target_include_directories(fd_music PUBLIC include)
target_link_libraries(fd_music PRIVATE fd::warnings)

fd_add_test(NAME fd_music_tests
    SOURCES tests/ScaleTests.cpp tests/KeyTests.cpp tests/DetectTests.cpp tests/ChordTests.cpp tests/FollowingTests.cpp
    LIBS fd::music)
