add_executable(fd-render main.cpp)
target_link_libraries(fd-render PRIVATE fd::registry fd::testkit_device fd::warnings)
