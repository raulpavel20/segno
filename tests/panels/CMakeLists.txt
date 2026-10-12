# The module panel layout lint (ADR 0022, docs/design/device-panels.md): every registered panel
# on the module grid. JUCE line; needs neither the plugins nor the DAW.
fd_add_test(NAME fd_panel_tests
    SOURCES PanelLayoutTests.cpp SpectrumPanelTests.cpp DelayPanelTests.cpp ChorusPanelTests.cpp PhaserFlangerPanelTests.cpp AutoPanTremoloPanelTests.cpp ReverbPanelTests.cpp FilterPanelTests.cpp
            LoudnessMeterPanelTests.cpp
    LIBS fd_panel_registry fd::registry fd::fd_ui fd::fd_device_ui juce::juce_gui_basics fd::testkit
         juce::juce_recommended_config_flags
    JUCE)
target_compile_definitions(fd_panel_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1) # the Spectrum test runs the message loop while sound plays
