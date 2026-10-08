# Rendering spike (ADR 0012, M0 step 10). Throwaway measurement tool; not part of the product.
juce_add_gui_app(fd-spike PRODUCT_NAME "fd-spike")
target_sources(fd-spike PRIVATE main.cpp Spike.cpp)
target_compile_definitions(fd-spike PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(fd-spike PRIVATE
    fd::fd_ui juce::juce_gui_basics juce::juce_opengl
    juce::juce_recommended_config_flags fd::warnings_juce)
