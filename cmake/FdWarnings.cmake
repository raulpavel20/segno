# fd::warnings — warning flags for our own targets only (never applied to dependencies).

add_library(fd_warnings INTERFACE)
add_library(fd::warnings ALIAS fd_warnings)

if(MSVC)
    target_compile_options(fd_warnings INTERFACE /W4 /permissive- /utf-8)
    if(FD_WARNINGS_AS_ERRORS)
        target_compile_options(fd_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(fd_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wimplicit-fallthrough
        -Wcast-align -Woverloaded-virtual -Wnull-dereference
        # Designated initializers that omit defaulted fields are the intended style (ParamSpec).
        -Wno-missing-field-initializers)
    if(FD_WARNINGS_AS_ERRORS)
        target_compile_options(fd_warnings INTERFACE -Werror)
    endif()
endif()
