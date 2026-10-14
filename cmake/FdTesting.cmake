# fd_add_test(NAME <name> SOURCES ... [LIBS ...] [JUCE])
#   Catch2 test executable; each TEST_CASE becomes a CTest test, and its Catch2 tags
#   become CTest labels (e.g. [fast][conformance] -> labels "fast" and "conformance").
#   Tag JUCE-free tests [fast] (CLAUDE.md). Pass JUCE for tests that compile JUCE/tracktion
#   sources, so they use JUCE's warning set (fd::warnings_juce).
function(fd_add_test)
    if(NOT FD_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(ARG "JUCE" "NAME" "SOURCES;LIBS" ${ARGN})
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    if(ARG_JUCE)
        set(warnings fd::warnings_juce)
    else()
        set(warnings fd::warnings)
    endif()
    target_link_libraries(${ARG_NAME} PRIVATE Catch2::Catch2WithMain ${warnings} ${ARG_LIBS})
    # Catch2 exits with 4 when every selected test was skipped (SKIP()).
    # Tests never read or write your presets (ADR 0044) or your Segno folder (the browser, ADR
    # 0046): a folder of their own (the presets' parent is the Segno folder), unless a test sets
    # another. Nor do they find a Library installed on the machine (ADR 0055): one that isn't
    # there, unless a test sets its own.
    catch_discover_tests(${ARG_NAME}
        TEST_PREFIX "${ARG_NAME}: "
        ADD_TAGS_AS_LABELS
        PROPERTIES SKIP_RETURN_CODE 4 ENVIRONMENT "SEGNO_PRESETS_FOLDER=${CMAKE_BINARY_DIR}/test-segno/Presets"
            ENVIRONMENT_MODIFICATION "SEGNO_LIBRARY=set:${CMAKE_BINARY_DIR}/test-segno/no-library"
        DISCOVERY_MODE PRE_TEST)
endfunction()
