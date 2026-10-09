# Global sanitizer flags, applied to everything (including dependencies) so that
# instrumented and uninstrumented code never mix.
#
#   -DFD_SANITIZE="address;undefined"   (asan preset)
#   -DFD_SANITIZE="realtime"            (rtsan preset, clang 20+; functions marked FD_NONBLOCKING)
#   -DFD_SANITIZE="thread"              (by hand: the MCP server's threads, fd_mcp_tests)

if(NOT FD_SANITIZE)
    return()
endif()

if(MSVC)
    if("address" IN_LIST FD_SANITIZE)
        add_compile_options(/fsanitize=address)
    endif()
    return()
endif()

set(_fd_san_flags "")
foreach(_san IN LISTS FD_SANITIZE)
    if(_san STREQUAL "address")
        list(APPEND _fd_san_flags -fsanitize=address)
    elseif(_san STREQUAL "undefined")
        list(APPEND _fd_san_flags -fsanitize=undefined -fno-sanitize-recover=undefined)
    elseif(_san STREQUAL "thread")
        list(APPEND _fd_san_flags -fsanitize=thread)
    elseif(_san STREQUAL "realtime")
        list(APPEND _fd_san_flags -fsanitize=realtime)
        add_compile_definitions(FD_RTSAN=1)
    else()
        message(FATAL_ERROR "Unknown sanitizer '${_san}' in FD_SANITIZE")
    endif()
endforeach()

add_compile_options(${_fd_san_flags} -fno-omit-frame-pointer)
add_link_options(${_fd_san_flags})
message(STATUS "segno: sanitizers enabled: ${FD_SANITIZE}")
