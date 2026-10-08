# fd_add_test(NAME <name> SOURCES ... [LIBS ...])
#   Catch2 test executable; each TEST_CASE becomes a CTest test, and its Catch2 tags
#   become CTest labels (e.g. [fast][conformance] -> labels "fast" and "conformance").
#   Tag JUCE-free tests [fast] (CLAUDE.md).
function(fd_add_test)
    if(NOT FD_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;LIBS" ${ARGN})
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE Catch2::Catch2WithMain fd::warnings ${ARG_LIBS})
    # Catch2 exits with 4 when every selected test was skipped (SKIP()).
    catch_discover_tests(${ARG_NAME}
        TEST_PREFIX "${ARG_NAME}: "
        ADD_TAGS_AS_LABELS
        PROPERTIES SKIP_RETURN_CODE 4
        DISCOVERY_MODE PRE_TEST)
endfunction()
