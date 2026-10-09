cmake_minimum_required(VERSION 3.25)

project(foss_daw VERSION 0.0.1 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options (ADR 0013: the suite and the DAW build independently)
# ---------------------------------------------------------------------------
option(FD_BUILD_TESTS "Build unit, conformance and integration tests" ON)
option(FD_BUILD_TOOLS "Build developer tools (fd-render, snapshot, gallery)" ON)
option(FD_BUILD_PLUGINS "Build the device suite as plugins (needs JUCE)" ON)
option(FD_BUILD_APP "Build the DAW (needs JUCE + tracktion_engine)" ON)
option(FD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(FD_SANITIZE "" CACHE STRING "Semicolon-separated sanitizers: address;undefined | realtime")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static libraries end up inside plugin shared objects.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Faster links on Linux when LLD is available (Apple's toolchain doesn't support it).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DEFINED CMAKE_LINKER_TYPE)
    find_program(FD_LLD ld.lld)
    if(FD_LLD)
        set(CMAKE_LINKER_TYPE LLD)
    endif()
endif()

include(FdSanitizers)

# Bounds-checked standard library containers in Debug builds (libstdc++; ignored elsewhere), so
# out-of-range indexing fails tests instead of reading garbage.
add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)
include(FdWarnings)
include(CPM)
include(Dependencies)
include(FdAddDevice)
include(FdTesting)

if(FD_BUILD_TESTS)
    enable_testing()
    # ADR 0023: UI code uses colour roles, text styles and components from fd_ui.
    add_test(NAME ui_tokens_only
        COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckUiTokens.cmake)
    set_tests_properties(ui_tokens_only PROPERTIES LABELS fast)
endif()

# ---------------------------------------------------------------------------
# JUCE-free core (ADR 0006)
# ---------------------------------------------------------------------------
add_subdirectory(libs/dsp)
add_subdirectory(libs/device)
add_subdirectory(libs/audiofile)
add_subdirectory(libs/testkit)
add_subdirectory(devices)
fd_generate_device_registry()

if(FD_BUILD_TOOLS)
    add_subdirectory(tools/render)
endif()

if(FD_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# JUCE line
# ---------------------------------------------------------------------------
if(FD_NEEDS_JUCE)
    add_subdirectory(modules)
    fd_generate_panel_registry()
    if(FD_BUILD_TESTS)
        add_subdirectory(tests/panels) # the module panel layout lint (ADR 0022)
    endif()
    if(FD_BUILD_TOOLS)
        add_subdirectory(tools/snapshot)
        add_subdirectory(tools/gallery)
        add_subdirectory(tools/spike)
    endif()
    if(FD_BUILD_PLUGINS)
        add_subdirectory(plugins)
        if(FD_BUILD_TESTS)
            add_subdirectory(tests/plugin)
        endif()
    endif()
    if(FD_BUILD_APP)
        add_subdirectory(app)
        if(FD_BUILD_TESTS)
            add_subdirectory(tests/integration)
        endif()
    endif()
endif()
