fd_add_test(NAME fd_conformance
    SOURCES DeviceConformance.cpp
    LIBS fd::registry fd::testkit_device)
