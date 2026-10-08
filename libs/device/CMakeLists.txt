add_library(fd_device STATIC src/Param.cpp src/ParamStore.cpp src/State.cpp src/Realtime.cpp src/VoiceManager.cpp)
add_library(fd::device ALIAS fd_device)
target_include_directories(fd_device PUBLIC include)
target_link_libraries(fd_device PUBLIC fd::dsp nlohmann_json::nlohmann_json PRIVATE fd::warnings)

fd_add_test(NAME fd_device_tests
    SOURCES tests/ParamTests.cpp tests/StateTests.cpp tests/EventTests.cpp tests/SampleFifoTests.cpp tests/VoiceManagerTests.cpp
    LIBS fd::device)
