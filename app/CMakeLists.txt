# The DAW (ADR 0002, 0004). tracktion_engine API use stays in src/engine/.

juce_add_modules(ALIAS_NAMESPACE tracktion
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_core"
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_graph"
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_engine")

# fd_app_model: JUCE-free view models of the DAW (ADR 0018), tested as [fast].
add_library(fd_app_model STATIC src/model/Timeline.cpp src/model/TrackLayout.cpp src/model/Peaks.cpp)
target_include_directories(fd_app_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fd_app_model PUBLIC fd::audiofile PRIVATE fd::warnings)

if(FD_BUILD_TESTS)
    fd_add_test(NAME fd_app_model_tests SOURCES tests/ModelTests.cpp LIBS fd_app_model)
endif()

# fd_engine: the tracktion adapter, compiled into each consumer (the app and its tests).
add_library(fd_engine INTERFACE)
target_sources(fd_engine INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/DevicePlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Session.cpp")
target_include_directories(fd_engine INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fd_engine INTERFACE
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    fd_panel_registry fd::registry fd::fd_device_ui fd::fd_host_juce fd_app_model
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
# fd_app_ui: the shell's components, compiled into the app and its tests.
add_library(fd_app_ui INTERFACE)
target_sources(fd_app_ui INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Shell.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Panels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ActionRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WaveformCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WaveformTiles.cpp")
target_link_libraries(fd_app_ui INTERFACE fd_engine juce::juce_opengl)

target_sources(fd-daw PRIVATE src/Main.cpp)
target_link_libraries(fd-daw PRIVATE fd_app_ui juce::juce_opengl juce::juce_recommended_config_flags fd::warnings_juce)

# ADR 0018: the UI and the view models never include tracktion; the models never include JUCE.
if(FD_BUILD_TESTS)
    add_test(NAME app_ui_does_not_include_tracktion
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/src -P ${PROJECT_SOURCE_DIR}/cmake/CheckAppBoundaries.cmake)
    set_tests_properties(app_ui_does_not_include_tracktion PROPERTIES LABELS fast)
endif()
set_target_properties(fd-daw PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
