add_library(fd_device STATIC src/Param.cpp src/ParamStore.cpp src/State.cpp src/Realtime.cpp src/VoiceManager.cpp src/Presets.cpp
    src/Library.cpp)
add_library(fd::device ALIAS fd_device)
target_include_directories(fd_device PUBLIC include)
target_link_libraries(fd_device PUBLIC fd::dsp nlohmann_json::nlohmann_json PRIVATE fd::warnings)
# A debug build finds the Library the segno-library target makes in the source tree (ADR 0055).
target_compile_definitions(fd_device PRIVATE $<$<CONFIG:Debug>:FD_DEV_LIBRARY="${PROJECT_SOURCE_DIR}/.cache/library">)

fd_add_test(NAME fd_device_tests
    SOURCES tests/ParamTests.cpp tests/StateTests.cpp tests/EventTests.cpp tests/SampleFifoTests.cpp tests/VoiceManagerTests.cpp
        tests/AssetHandoffTests.cpp tests/PresetTests.cpp tests/LibraryTests.cpp
    LIBS fd::device)
