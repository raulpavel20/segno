juce_add_gui_app(fd-gallery PRODUCT_NAME "fd-gallery")
target_sources(fd-gallery PRIVATE main.cpp Gallery.cpp)
target_compile_definitions(fd-gallery PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(fd-gallery PRIVATE
    fd_panel_registry fd::registry fd::fd_ui fd::fd_device_ui
    juce::juce_gui_basics juce::juce_recommended_config_flags fd::warnings_juce)
