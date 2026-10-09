fd_add_test(NAME fd_conformance
    SOURCES DeviceConformance.cpp
    LIBS fd::registry fd::testkit_device)
# The SPECs, for comparing each parameter's description with its device's SPEC table (B22).
target_compile_definitions(fd_conformance PRIVATE FD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
