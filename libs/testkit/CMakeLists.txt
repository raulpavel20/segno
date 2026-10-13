add_library(fd_testkit STATIC src/Signals.cpp src/Metrics.cpp src/WavIO.cpp src/Golden.cpp src/Allocations.cpp)
add_library(fd::testkit ALIAS fd_testkit)
target_include_directories(fd_testkit PUBLIC include)
target_link_libraries(fd_testkit PUBLIC nlohmann_json::nlohmann_json PRIVATE fd::audiofile fd::dsp fd::warnings)

fd_add_test(NAME fd_testkit_tests
    SOURCES tests/SignalsTests.cpp tests/MetricsTests.cpp tests/WavGoldenTests.cpp
    LIBS fd::testkit)

# Device-aware part of the test kit (offline rendering).
add_library(fd_testkit_device STATIC src/DeviceRender.cpp)
add_library(fd::testkit_device ALIAS fd_testkit_device)
target_link_libraries(fd_testkit_device PUBLIC fd::testkit fd::device PRIVATE fd::warnings)

fd_add_test(NAME fd_testkit_device_tests
    SOURCES tests/DeviceRenderTests.cpp
    LIBS fd::testkit_device)
