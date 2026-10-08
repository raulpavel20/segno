# Integration tests: devices inside tracktion_engine (JUCE line, needs FD_BUILD_APP).
fd_add_test(NAME fd_integration_tests
    SOURCES EngineNullTests.cpp AppSmokeTests.cpp "${PROJECT_SOURCE_DIR}/app/src/ui/MainComponent.cpp"
    LIBS fd_engine fd::testkit_device juce::juce_recommended_config_flags
    JUCE)
target_compile_definitions(fd_integration_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

if(TARGET fd_plugin_utility_VST3)
    # Path of our built VST3, for the "VST3 hosted in tracktion" null test.
    add_dependencies(fd_integration_tests fd_plugin_utility_VST3)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/fd_test_paths.h" CONTENT
        "#define FD_UTILITY_VST3_PATH \"$<GENEX_EVAL:$<TARGET_PROPERTY:fd_plugin_utility_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>\"\n")
    target_include_directories(fd_integration_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    target_compile_definitions(fd_integration_tests PRIVATE FD_HAVE_TEST_PATHS=1)
endif()
