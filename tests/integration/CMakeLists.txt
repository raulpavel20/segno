# Integration tests: devices inside tracktion_engine (JUCE line, needs FD_BUILD_APP).
fd_add_test(NAME fd_integration_tests
    SOURCES EngineNullTests.cpp AppSmokeTests.cpp AudioEditorTests.cpp WarpSpikeTests.cpp WarpPlayerTests.cpp SessionTests.cpp MixerTests.cpp AutomationEngineTests.cpp RecordingTests.cpp TakeStackTests.cpp BounceTests.cpp TimeSelectionSpikeTests.cpp GroupSpikeTests.cpp GroupUiTests.cpp TrackSelectionTests.cpp TimeSelectionUiTests.cpp TimeSelectionTests.cpp FadeTests.cpp ArrangementBenchmark.cpp WaveformTilesTests.cpp CrossfadeSumsTests.cpp ExportTests.cpp TrackEditTests.cpp NoteContinuityTests.cpp PresetTests.cpp SettingsPageTests.cpp BrowserEngineTests.cpp BrowserPanelTests.cpp ClipLoopEngineTests.cpp AudioSoundEngineTests.cpp SectionTests.cpp SongKeyTests.cpp PianoRollTests.cpp ChanceEngineTests.cpp
        McpToolTests.cpp RackSpikeTests.cpp RackUiTests.cpp RackTests.cpp DrumKitSpikeTests.cpp DrumKitUiTests.cpp DrumKitTests.cpp DrumKitChainTests.cpp DrumViewTests.cpp FactoryKitTests.cpp ModulatorSpikeTests.cpp ModulatorUiTests.cpp ModulatorEngineTests.cpp ModulatorSessionTests.cpp ModulatorChainTests.cpp
        PluginScanTests.cpp RecordingLatencyTests.cpp MidiLearnTests.cpp Main.cpp
    LIBS fd_app_ui fd::testkit_device fd_dep_httplib juce::juce_recommended_config_flags
    JUCE OWN_MAIN)
target_compile_definitions(fd_integration_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 FD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # A VST3 that crashes the moment it's looked at, for the plugin scan's test (M3 C5): a bundle
    # of one library whose entry point aborts.
    add_library(fd_test_crashing_vst3 MODULE fixtures/CrashingPlugin.cpp)
    set(crashing_bundle "${CMAKE_CURRENT_BINARY_DIR}/scan-fixtures/Crashes on scan.vst3")
    set_target_properties(fd_test_crashing_vst3 PROPERTIES
        PREFIX "" OUTPUT_NAME "Crashes on scan" SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${crashing_bundle}/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux")
    add_dependencies(fd_integration_tests fd_test_crashing_vst3)
    target_compile_definitions(fd_integration_tests PRIVATE FD_CRASHING_VST3_PATH="${crashing_bundle}")
endif()
