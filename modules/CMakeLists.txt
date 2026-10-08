# JUCE modules (the JUCE line, ADR 0006). Packaged as JUCE modules so every consumer compiles
# them with its own consistent JUCE configuration.

juce_add_binary_data(fd_ui_assets
    HEADER_NAME FdUiAssets.h
    NAMESPACE fd_assets
    SOURCES "${inter_font_SOURCE_DIR}/InterVariable.ttf")
set_target_properties(fd_ui_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_modules(ALIAS_NAMESPACE fd fd_ui fd_device_ui fd_host_juce)

target_link_libraries(fd_ui INTERFACE fd_ui_assets)
target_link_libraries(fd_device_ui INTERFACE fd::device)
target_link_libraries(fd_host_juce INTERFACE fd::device)
