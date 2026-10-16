fd_add_test(NAME fd_conformance
    SOURCES DeviceConformance.cpp
    LIBS fd::registry fd::testkit_device)
# The SPECs, for comparing each parameter's description with its device's SPEC table (B22).
target_compile_definitions(fd_conformance PRIVATE FD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Every instrument's factory presets across the keyboard (ADR 0069): about a minute a set in
# Release and an hour for all of them in Debug, so the Release suite runs it (ctest -L presets);
# fd-presets make runs the same check before it writes a set.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_test(NAME "conformance: every instrument's factory preset plays cleanly anywhere on the keyboard"
             COMMAND fd_conformance "[.presets]")
    set_tests_properties("conformance: every instrument's factory preset plays cleanly anywhere on the keyboard"
                         PROPERTIES LABELS "presets;conformance" TIMEOUT 3600)
endif()
