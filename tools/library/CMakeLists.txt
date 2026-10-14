add_executable(fd-library main.cpp)
target_link_libraries(fd-library PRIVATE fd_device_wavetable_dsp fd::warnings)

# The Library in the source tree's .cache/library, where debug builds find it (ADR 0055): a target
# of its own, not part of a plain build (factory-library.md §6).
add_custom_target(segno-library
    COMMAND fd-library build "${PROJECT_SOURCE_DIR}/.cache/library"
    DEPENDS fd-library
    COMMENT "Building Segno's Library into .cache/library"
    VERBATIM)
