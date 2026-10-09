# Plugin adapter tests (JUCE line). Uses the Utility plugin's shared-code library, which already
# contains the compiled JUCE modules; the test borrows its include paths and definitions so the
# JUCE headers see the same configuration. The Compressor's DSP is linked too, for a device whose
# latency follows a setting (ADR 0020).
fd_add_test(NAME fd_plugin_tests
    SOURCES PluginAdapterTests.cpp
    LIBS fd_plugin_utility fd_device_compressor_dsp
    JUCE)
target_include_directories(fd_plugin_tests PRIVATE $<TARGET_PROPERTY:fd_plugin_utility,INCLUDE_DIRECTORIES>)
target_compile_definitions(fd_plugin_tests PRIVATE $<TARGET_PROPERTY:fd_plugin_utility,COMPILE_DEFINITIONS>)
