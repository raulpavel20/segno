# The Library's sources (ADR 0055, docs/design/factory-library.md §6): every file
# tools/library/recipe.json names (a drum's every microphone too, drum-kit.md §27.3), fetched from
# its repository at the commit the recipe pins and checked by its SHA-256, into
# SOURCES/<source>/<commit>/<path>. Files already there and right are left alone, so it runs once;
# the first time it fetches a few GB. The list comes from `fd-library files`, so the recipe is
# parsed once, however many files it names.
#
#   cmake -DTOOL=<fd-library> -DRECIPE=<recipe.json> -DSOURCES=<cache> -P FdLibrarySources.cmake

execute_process(COMMAND "${TOOL}" files "${RECIPE}" OUTPUT_VARIABLE listing RESULT_VARIABLE listed)
if(NOT listed EQUAL 0)
    message(FATAL_ERROR "Couldn't list the Library's sources from ${RECIPE}")
endif()
string(REPLACE ";" "\;" listing "${listing}")
string(REPLACE "\n" ";" lines "${listing}")

set(fetched 0)
set(checked 0)
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    string(REPLACE "\t" ";" fields "${line}")
    list(GET fields 0 base)
    list(GET fields 1 where)
    list(GET fields 2 file)
    list(GET fields 3 hash)
    set(local "${SOURCES}/${where}/${file}")
    if(EXISTS "${local}")
        file(SHA256 "${local}" have)
        if(have STREQUAL hash)
            math(EXPR checked "${checked} + 1")
            continue()
        endif()
    endif()
    # The path in a URL: the characters these libraries' names use, escaped.
    string(REPLACE "%" "%25" url "${file}")
    string(REPLACE " " "%20" url "${url}")
    string(REPLACE "#" "%23" url "${url}")
    string(REPLACE "&" "%26" url "${url}")
    string(REPLACE "+" "%2B" url "${url}")
    string(REPLACE "?" "%3F" url "${url}")
    string(REPLACE "," "%2C" url "${url}")
    file(DOWNLOAD "${base}/${url}" "${local}" EXPECTED_HASH SHA256=${hash} TLS_VERIFY ON STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Couldn't fetch ${where}/${file}: ${status}")
    endif()
    math(EXPR fetched "${fetched} + 1")
    if(fetched EQUAL 1 OR fetched MATCHES "00$")
        message(STATUS "The Library's sources: ${fetched} fetched")
    endif()
endforeach()
message(STATUS "The Library's sources: ${fetched} fetched, ${checked} already here")
