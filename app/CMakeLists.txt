# The DAW (ADR 0002, 0004). tracktion_engine API use stays in src/engine/.

juce_add_modules(ALIAS_NAMESPACE tracktion
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_core"
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_graph"
    "${tracktion_engine_SOURCE_DIR}/modules/tracktion_engine")

# fd_app_model: JUCE-free view models of the DAW (ADR 0018), tested as [fast].
add_library(fd_app_model STATIC src/model/Timeline.cpp src/model/Grid.cpp src/model/TrackLayout.cpp src/model/Peaks.cpp
    src/model/Editing.cpp src/model/Fuzzy.cpp src/model/NoteEditing.cpp src/model/NoteTools.cpp src/model/PianoRollOptions.cpp
    src/model/Chance.cpp src/model/Controllers.cpp src/model/NoteToolkit.cpp
    src/model/TrackHeaderLayout.cpp src/model/AudioMeasure.cpp src/model/Taper.cpp src/model/ChannelStripLayout.cpp src/model/Selection.cpp src/model/ModuleFacts.cpp src/model/RoundTrip.cpp src/model/MidiMap.cpp
    src/model/Automation.cpp src/model/ArrangementRows.cpp src/model/Snapshot.cpp src/model/Recording.cpp src/model/Takes.cpp
    src/model/TopBarLayout.cpp src/model/StackEdits.cpp src/model/Capture.cpp src/model/Bounce.cpp src/model/Fades.cpp src/model/FadeEditing.cpp src/model/AudioSound.cpp src/model/AudioToolkit.cpp src/model/Warp.cpp src/model/AudioAnalysis.cpp src/model/Export.cpp
    src/model/MenuBar.cpp src/model/AppSettings.cpp src/model/SettingsCatalogue.cpp src/model/Browser.cpp src/model/TrackEdits.cpp src/model/ClipLoops.cpp src/model/MusicalTime.cpp src/model/BraceDrag.cpp src/model/Sections.cpp src/model/TimeEdits.cpp src/model/SongKey.cpp src/model/Modulators.cpp src/model/DrumKit.cpp)
target_include_directories(fd_app_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fd_app_model PUBLIC fd::audiofile fd::music fd::device fd::dsp nlohmann_json::nlohmann_json PRIVATE fd::warnings)
# The DAW's own modules' factory presets, from presets/<type>/: the modulators' (modulators.md §14,
# ADR 0044) and the Drum Kit's kits (drum-kit.md §27).
fd_generate_presets_from("${CMAKE_CURRENT_SOURCE_DIR}/presets" dawFactoryPresets fd_app_model)

if(FD_BUILD_TESTS)
    fd_add_test(NAME fd_app_model_tests SOURCES tests/ModelTests.cpp tests/NoteEditingTests.cpp tests/NoteToolsTests.cpp tests/AudioMeasureTests.cpp
        tests/SelectionTests.cpp tests/TimeEditsTests.cpp tests/AutomationTests.cpp
        tests/ArrangementRowsTests.cpp tests/RecordingTests.cpp tests/TakeStackTests.cpp tests/CaptureTests.cpp tests/BounceTests.cpp tests/FadesTests.cpp tests/ExportTests.cpp
        tests/MenuBarTests.cpp tests/TrackEditsTests.cpp tests/SettingsTests.cpp tests/BrowserModelTests.cpp tests/ClipLoopsTests.cpp tests/MusicalTimeTests.cpp tests/BraceDragTests.cpp tests/SectionsTests.cpp
        tests/ChanceTests.cpp tests/NoteToolkitTests.cpp tests/RackModelTests.cpp tests/ModulatorPresetsTests.cpp tests/DrumKitModelTests.cpp
        tests/ModuleFactsTests.cpp tests/RoundTripTests.cpp tests/MidiMapTests.cpp
        tests/AudioSoundTests.cpp tests/WarpTests.cpp tests/AudioAnalysisTests.cpp
        LIBS fd_app_model)
endif()

# fd_mcp: the MCP server's protocol layer and transports (ADR 0021, docs/design/mcp.md). JUCE-free,
# tested as [fast]; the tools over the Session live in fd_app_mcp below.
add_library(fd_mcp STATIC src/mcp/protocol/Dispatcher.cpp src/mcp/protocol/Schema.cpp src/mcp/protocol/Encoding.cpp
    src/mcp/Conventions.cpp src/mcp/transport/HttpServer.cpp src/mcp/transport/StdioRelay.cpp)
target_include_directories(fd_mcp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fd_mcp PUBLIC fd_app_model nlohmann_json::nlohmann_json PRIVATE fd_dep_httplib fd::warnings)

if(FD_BUILD_TESTS)
    fd_add_test(NAME fd_mcp_tests SOURCES tests/McpProtocolTests.cpp tests/McpConventionsTests.cpp tests/McpHttpTests.cpp
        LIBS fd_mcp fd_dep_httplib)
endif()

# fd_engine: the tracktion adapter, compiled into each consumer (the app and its tests).
add_library(fd_engine INTERFACE)
target_sources(fd_engine INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/DevicePlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Modulator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/PadInlet.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionModulators.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/EngineBehaviour.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Session.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionAutomation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionRecording.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionTakes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionBounce.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionExport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ClipFades.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ClipNotes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ClipChance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/WarpPlayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/FileAnalyser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionFades.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionSound.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionWarp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionTracks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionSections.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionTime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionKey.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionRacks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionKits.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SongKey.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionNotes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionPresets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionSettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionBrowser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionWavetables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionGranular.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/FileIndexer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/PreviewPlayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/PresetPreview.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/MidiTap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/PluginWindow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/PluginScan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Mp3Writer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/MonitoringLatency.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/RoundTrip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/MidiMapper.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SessionMidiMap.cpp")
target_include_directories(fd_engine INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fd_engine INTERFACE
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    fd_panel_registry fd::registry fd::fd_device_ui fd::fd_host_juce fd_app_model
    juce::juce_audio_utils juce::juce_dsp fd_dep_lame)
target_compile_definitions(fd_engine INTERFACE
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LV2=1>
    $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
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
    PRODUCT_NAME "Segno"
    COMPANY_NAME "Segno"
    BUNDLE_ID "org.segnoaudio.segno"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)
# Installing on Linux: the app, its launcher entry and its icon at every size (packaging/linux;
# tools/install-desktop-entry.sh does the same for a build of yours, without copying the app).
# A component of its own, since dependencies (JUCE) bring install rules too:
#   cmake --install build/rel --component Segno [--prefix ...]
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(GNUInstallDirs)
    install(TARGETS fd-daw RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Segno)
    set(SEGNO_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/Segno")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/org.segnoaudio.segno.desktop.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/org.segnoaudio.segno.desktop" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.segnoaudio.segno.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT Segno)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/packaging/linux/icons/" DESTINATION ${CMAKE_INSTALL_DATADIR}/icons
            COMPONENT Segno)
endif()
# fd_app_mcp: the MCP tools over the Session, and the server that owns them (ADR 0021). Each
# module's purpose comes from its SPEC, for the module reference.
include(${PROJECT_SOURCE_DIR}/cmake/ModulePurposes.cmake)
fd_generate_module_purposes("${CMAKE_CURRENT_BINARY_DIR}/generated/ModulePurposes.h")
add_library(fd_app_mcp INTERFACE)
target_sources(fd_app_mcp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/McpServer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/MainThread.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/Tools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ReadTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/TransportTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ClipTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/AudioTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/NoteTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ChordTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ControllerTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/MidiTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ToolkitTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ModuleTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/WavetableTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/PresetTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/RenderTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/MixTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/AutomationTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/RecordingTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/SettingsTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/BrowserTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/GridTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ProjectTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/TrackTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/SectionTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/KeyTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/RackTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/KitTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/ModulatorTools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mcp/tools/Resources.cpp")
target_include_directories(fd_app_mcp INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(fd_app_mcp INTERFACE fd_engine fd_mcp)

# fd_app_ui: the shell's components, compiled into the app and its tests.
add_library(fd_app_ui INTERFACE)
target_sources(fd_app_ui INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Shell.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Panels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/GridMenu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementFades.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ArrangementSongRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/CrossfadeSums.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AutomationLane.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LaneControls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LaneAssignments.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TrackInputs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AutomationMenu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LanePicker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/KeyPopover.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ChordPopover.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ToolForm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ToolsPane.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MakeChordsForm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ChannelControls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ChannelInspector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MixerView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ActionRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WaveformCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WaveformTiles.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DeviceChain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MacroPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/RackPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DrumKitPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ModulatorPanels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MacroEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/CommandPalette.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PianoRoll.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ClipLoopBrace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AudioClipView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AudioClipViewWarp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AssistantsPopover.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ExportPopover.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/AboutPopover.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/RoundTripPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/BrowserPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/BrowserDrops.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ShellMenus.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ShellSong.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ShellModulators.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ShellMidi.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiMapView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MidiMappingList.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ShellNotes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ShellAudio.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/TitleBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WindowContent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/time/TimeView.cpp")
target_link_libraries(fd_app_ui INTERFACE fd_engine fd_app_mcp juce::juce_opengl)

target_sources(fd-daw PRIVATE src/Main.cpp)
target_link_libraries(fd-daw PRIVATE fd_app_ui juce::juce_opengl juce::juce_recommended_config_flags fd::warnings_juce)

# ADR 0018: the UI and the view models never include tracktion; the models never include JUCE.
if(FD_BUILD_TESTS)
    add_test(NAME app_ui_does_not_include_tracktion
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/src -P ${PROJECT_SOURCE_DIR}/cmake/CheckAppBoundaries.cmake)
    set_tests_properties(app_ui_does_not_include_tracktion PROPERTIES LABELS fast)
    add_test(NAME mcp_only_in_the_daw
        COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckMcpBoundary.cmake)
    set_tests_properties(mcp_only_in_the_daw PROPERTIES LABELS fast)
endif()
set_target_properties(fd-daw PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
