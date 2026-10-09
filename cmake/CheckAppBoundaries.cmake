# ADR 0018: app/src/ui and app/src/model must not use tracktion; app/src/model must not use JUCE.
# ADR 0021: the MCP code reaches the engine only through engine::Session (no tracktion anywhere
# in app/src/mcp), and its protocol and transports are JUCE-free.
# Run as: cmake -DROOT=<app/src> -P CheckAppBoundaries.cmake
set(failed "")
file(GLOB_RECURSE ui_files "${ROOT}/ui/*.h" "${ROOT}/ui/*.cpp" "${ROOT}/model/*.h" "${ROOT}/model/*.cpp"
    "${ROOT}/mcp/*.h" "${ROOT}/mcp/*.cpp")
foreach(f IN LISTS ui_files)
    file(READ "${f}" content)
    if(content MATCHES "#include <tracktion|tracktion::|[^a-z_]te::")
        list(APPEND failed "${f} uses tracktion")
    endif()
endforeach()
file(GLOB_RECURSE model_files "${ROOT}/model/*.h" "${ROOT}/model/*.cpp" "${ROOT}/mcp/protocol/*.h"
    "${ROOT}/mcp/protocol/*.cpp" "${ROOT}/mcp/transport/*.h" "${ROOT}/mcp/transport/*.cpp" "${ROOT}/mcp/Conventions.h"
    "${ROOT}/mcp/Conventions.cpp")
foreach(f IN LISTS model_files)
    file(READ "${f}" content)
    if(content MATCHES "#include <juce|juce::")
        list(APPEND failed "${f} uses JUCE")
    endif()
endforeach()
if(failed)
    string(REPLACE ";" "\n  " failed "${failed}")
    message(FATAL_ERROR "App module boundaries broken:\n  ${failed}")
endif()
