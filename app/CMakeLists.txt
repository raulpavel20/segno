# The DAW (ADR 0002, 0004). tracktion_engine API use stays in src/engine/.

juce_add_modules(ALIAS_NAMESPACE tracktion
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_core"
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_graph"
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_engine")

# fd_engine: the tracktion adapter, compiled into each consumer (the app and its tests).
add_library(fd_engine INTERFACE)
target_sources(fd_engine INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/DevicePlugin.cpp")
target_include_directories(fd_engine INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fd_engine INTERFACE
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    fd_panel_registry fd::registry fd::fd_device_ui fd::fd_host_juce
    juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(fd_engine INTERFACE
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LV2=1>
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(fd_engine INTERFACE atomic)
endif()

juce_add_gui_app(fd-daw
    PRODUCT_NAME "FossDAW"
    COMPANY_NAME "FossDAW"
    BUNDLE_ID "org.fossdaw.daw"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)
target_sources(fd-daw PRIVATE src/Main.cpp src/ui/MainComponent.cpp)
target_link_libraries(fd-daw PRIVATE fd_engine juce::juce_recommended_config_flags fd::warnings_juce)
set_target_properties(fd-daw PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
