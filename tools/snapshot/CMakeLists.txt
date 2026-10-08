juce_add_console_app(fd-snapshot PRODUCT_NAME "fd-snapshot")
target_sources(fd-snapshot PRIVATE main.cpp)
target_compile_definitions(fd-snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(fd-snapshot PRIVATE
    fd_panel_registry fd::registry fd::fd_ui fd::fd_device_ui
    juce::juce_gui_basics
    juce::juce_recommended_config_flags fd::warnings)
