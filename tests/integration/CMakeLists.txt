# Integration tests: devices inside tracktion_engine (JUCE line, needs FD_BUILD_APP).
fd_add_test(NAME fd_integration_tests
    SOURCES EngineNullTests.cpp AppSmokeTests.cpp AudioEditorTests.cpp SessionTests.cpp MixerTests.cpp AutomationEngineTests.cpp RecordingTests.cpp TakeStackTests.cpp BounceTests.cpp FadeTests.cpp ArrangementBenchmark.cpp WaveformTilesTests.cpp CrossfadeSumsTests.cpp ExportTests.cpp TrackEditTests.cpp NoteContinuityTests.cpp PresetTests.cpp SettingsPageTests.cpp BrowserEngineTests.cpp BrowserPanelTests.cpp ClipLoopEngineTests.cpp AudioSoundEngineTests.cpp SectionTests.cpp SongKeyTests.cpp PianoRollTests.cpp ChanceEngineTests.cpp
        McpToolTests.cpp RackSpikeTests.cpp RackUiTests.cpp RackTests.cpp
    LIBS fd_app_ui fd::testkit_device fd_dep_httplib juce::juce_recommended_config_flags
    JUCE)
target_compile_definitions(fd_integration_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

if(TARGET fd_plugin_utility_VST3 AND TARGET fd_plugin_subtractive_synth_VST3)
    # Paths of our built VST3s, for the "VST3 hosted in tracktion" tests (an effect and an
    # instrument standing in for third-party plugins).
    add_dependencies(fd_integration_tests fd_plugin_utility_VST3 fd_plugin_subtractive_synth_VST3)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/fd_test_paths.h" CONTENT
        "#define FD_UTILITY_VST3_PATH \"$<GENEX_EVAL:$<TARGET_PROPERTY:fd_plugin_utility_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>\"\n#define FD_SYNTH_VST3_PATH \"$<GENEX_EVAL:$<TARGET_PROPERTY:fd_plugin_subtractive_synth_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>\"\n")
    target_include_directories(fd_integration_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    target_compile_definitions(fd_integration_tests PRIVATE FD_HAVE_TEST_PATHS=1)
endif()

if(TARGET fd_library_build)
    # The factory racks, made from their recipes (racks.md §18.8), loaded and heard in the DAW.
    target_link_libraries(fd_integration_tests PRIVATE fd_library_build)
    target_compile_definitions(fd_integration_tests PRIVATE FD_HAVE_FACTORY_RACKS=1
        FD_RACK_RECIPES="${PROJECT_SOURCE_DIR}/tools/library/racks.json")
endif()
