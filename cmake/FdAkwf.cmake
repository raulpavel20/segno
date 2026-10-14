# The Wavetable's AKWF factory tables (docs/design/table-editor.md §7): the single cycles their
# recipe lists (devices/wavetable/tables/akwf.json) are fetched one by one from AKWF-FREE at the
# commit Dependencies.cmake pins (FD_AKWF_COMMIT), each checked by its SHA-256, into the CPM
# cache; the whole repository is 2 GB. Then they're embedded, as their WAV bytes, in a generated
# source the device's DSP compiles.
#
#   fd_generate_akwf_tables(RECIPE <json> OUTPUT <cpp>)

function(fd_generate_akwf_tables)
    cmake_parse_arguments(ARG "" "RECIPE;OUTPUT" "" ${ARGN})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARG_RECIPE}")
    if(CPM_SOURCE_CACHE)
        set(cache "${CPM_SOURCE_CACHE}/akwf/${FD_AKWF_COMMIT}")
    else()
        set(cache "${CMAKE_BINARY_DIR}/_deps/akwf/${FD_AKWF_COMMIT}")
    endif()
    set(base "https://raw.githubusercontent.com/KristofferKarlAxelEkstrand/AKWF-FREE/${FD_AKWF_COMMIT}")

    file(READ "${ARG_RECIPE}" recipe)
    string(JSON tableCount LENGTH "${recipe}" tables)
    math(EXPR lastTable "${tableCount} - 1")
    set(arrays "")
    set(lists "")
    set(entries "")
    set(n 0)
    foreach(t RANGE ${lastTable})
        string(JSON id GET "${recipe}" tables ${t} id)
        string(JSON name GET "${recipe}" tables ${t} name)
        string(JSON category GET "${recipe}" tables ${t} category)
        string(JSON frames GET "${recipe}" tables ${t} frames)
        string(JSON cycleCount LENGTH "${recipe}" tables ${t} cycles)
        math(EXPR lastCycle "${cycleCount} - 1")
        set(cycles "")
        foreach(c RANGE ${lastCycle})
            string(JSON file GET "${recipe}" tables ${t} cycles ${c} 0)
            string(JSON hash GET "${recipe}" tables ${t} cycles ${c} 1)
            set(local "${cache}/${file}")
            set(have "")
            if(EXISTS "${local}")
                file(SHA256 "${local}" have)
            endif()
            if(NOT have STREQUAL hash)
                file(DOWNLOAD "${base}/${file}" "${local}" EXPECTED_HASH SHA256=${hash} TLS_VERIFY ON STATUS status)
                list(GET status 0 code)
                if(NOT code EQUAL 0)
                    message(FATAL_ERROR "Couldn't fetch the AKWF cycle ${file}: ${status}")
                endif()
            endif()
            file(READ "${local}" bytes HEX)
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
            string(APPEND arrays "constexpr unsigned char c${n}[] = {${bytes}};\n")
            string(APPEND cycles "{c${n}, sizeof c${n}}, ")
            math(EXPR n "${n} + 1")
        endforeach()
        string(APPEND lists "constexpr AkwfCycle t${t}[] = {${cycles}};\n")
        string(APPEND entries "        {\"${id}\", \"${name}\", \"${category}\", ${frames}, t${t}},\n")
    endforeach()

    file(CONFIGURE OUTPUT "${ARG_OUTPUT}" CONTENT [[
// Generated from devices/wavetable/tables/akwf.json by cmake/FdAkwf.cmake. Do not edit.
// The cycles are AKWF-FREE's (CC0), as fetched.
#include "AkwfTables.h"

namespace fd::devices::wavetable
{
namespace
{
// clang-format off
@arrays@
@lists@
// clang-format on
} // namespace

std::span<const AkwfTable> akwfTables()
{
    static constexpr AkwfTable tables[] = {
@entries@    };
    return tables;
}
} // namespace fd::devices::wavetable
]] @ONLY)
endfunction()
