# Segno's Library (ADR 0055, docs/design/factory-library.md §6): the tool that builds it, and a
# target of its own that fetches the sources and builds it into .cache/library, where debug builds
# find it. Not part of a plain build: the sources are about 2 GB.
add_library(fd_library_build STATIC LibraryBuild.cpp RackRecipes.cpp)
target_include_directories(fd_library_build PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(fd_library_build PUBLIC fd::audiofile fd::registry nlohmann_json::nlohmann_json
    PRIVATE FLAC::FLAC fd::warnings)

add_executable(fd-library main.cpp)
target_link_libraries(fd-library PRIVATE fd_library_build fd_device_wavetable_dsp fd::warnings)

fd_add_test(NAME fd_library_tests
    SOURCES LibraryBuildTests.cpp RackRecipesTests.cpp
    LIBS fd_library_build fd_device_sampler_dsp)
target_compile_definitions(fd_library_tests PRIVATE FD_RACK_RECIPES="${CMAKE_CURRENT_SOURCE_DIR}/racks.json")

# The recipe names every source file with its SHA-256; they're fetched once into the CPM cache.
set(recipe "${CMAKE_CURRENT_SOURCE_DIR}/recipe.json")
if(CPM_SOURCE_CACHE)
    set(sources "${CPM_SOURCE_CACHE}/segno-library")
else()
    set(sources "${CMAKE_BINARY_DIR}/_deps/segno-library")
endif()
add_custom_target(segno-library
    COMMAND "${CMAKE_COMMAND}" "-DRECIPE=${recipe}" "-DSOURCES=${sources}"
            -P "${PROJECT_SOURCE_DIR}/cmake/FdLibrarySources.cmake"
    COMMAND fd-library build "${PROJECT_SOURCE_DIR}/.cache/library" --recipe "${recipe}" --sources "${sources}"
            --racks "${CMAKE_CURRENT_SOURCE_DIR}/racks.json"
    DEPENDS fd-library
    COMMENT "Building Segno's Library into .cache/library"
    VERBATIM USES_TERMINAL)
