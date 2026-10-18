# JUCE modules (the JUCE line, ADR 0006). Packaged as JUCE modules so every consumer compiles
# them with its own consistent JUCE configuration.

# The mono font's file name has brackets ("JetBrainsMono[wght].ttf"); copy it to a name that
# makes a plain symbol.
configure_file("${jetbrains_mono_font_SOURCE_DIR}/fonts/variable/JetBrainsMono[wght].ttf"
    "${CMAKE_CURRENT_BINARY_DIR}/JetBrainsMonoVariable.ttf" COPYONLY)
juce_add_binary_data(fd_ui_assets
    HEADER_NAME FdUiAssets.h
    NAMESPACE fd_assets
    SOURCES "${inter_font_SOURCE_DIR}/InterVariable.ttf" "${CMAKE_CURRENT_BINARY_DIR}/JetBrainsMonoVariable.ttf")
set_target_properties(fd_ui_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_modules(ALIAS_NAMESPACE fd fd_ui fd_device_ui fd_host_juce)

target_link_libraries(fd_ui INTERFACE fd_ui_assets)
target_link_libraries(fd_device_ui INTERFACE fd::device fd::music)
target_link_libraries(fd_host_juce INTERFACE fd::device)

# Warning flags for targets that compile JUCE/tracktion sources (plugins, app, tools, JUCE-line
# tests). JUCE's recommended set is strict and JUCE/tracktion compile cleanly with it; exact
# float comparisons are intentional in our DSP (e.g. smoother targets), so allow them.
add_library(fd_warnings_juce INTERFACE)
add_library(fd::warnings_juce ALIAS fd_warnings_juce)
# Overrides live in their own target, linked after JUCE's, so they come later on the command line.
add_library(fd_warnings_juce_overrides INTERFACE)
if(NOT MSVC)
    target_compile_options(fd_warnings_juce_overrides INTERFACE
        -Wno-float-equal                      # exact comparisons are intentional in our DSP
        -Wno-missing-field-initializers)      # designated initializers omit defaulted fields
    if(FD_WARNINGS_AS_ERRORS)
        target_compile_options(fd_warnings_juce_overrides INTERFACE -Werror)
    endif()
endif()
target_link_libraries(fd_warnings_juce INTERFACE juce::juce_recommended_warning_flags fd_warnings_juce_overrides)
