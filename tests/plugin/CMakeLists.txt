# Plugin adapter tests (JUCE line). Uses the Utility plugin's shared-code library, which already
# contains the compiled JUCE modules; the test borrows its include paths and definitions so the
# JUCE headers see the same configuration. The Compressor's DSP is linked too, for a device whose
# latency follows a setting (ADR 0020), and the Wavetable's, an instrument to hear an editor's note.
fd_add_test(NAME fd_plugin_tests
    SOURCES PluginAdapterTests.cpp
            # The Wavetable's panel and table editor, to see its editor open in a plugin's window.
            ${PROJECT_SOURCE_DIR}/devices/wavetable/ui/WavetablePanel.cpp
            ${PROJECT_SOURCE_DIR}/devices/wavetable/ui/WavetableUi.cpp
            ${PROJECT_SOURCE_DIR}/devices/wavetable/ui/TableEditor.cpp
    LIBS fd_plugin_utility fd_device_compressor_dsp fd_device_wavetable_dsp
    JUCE)
target_include_directories(fd_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/devices/wavetable/ui)
target_include_directories(fd_plugin_tests PRIVATE $<TARGET_PROPERTY:fd_plugin_utility,INCLUDE_DIRECTORIES>)
target_compile_definitions(fd_plugin_tests PRIVATE $<TARGET_PROPERTY:fd_plugin_utility,COMPILE_DEFINITIONS>)
