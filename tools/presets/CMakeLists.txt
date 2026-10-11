add_executable(fd-presets main.cpp)
target_link_libraries(fd-presets PRIVATE fd::registry fd::warnings)
