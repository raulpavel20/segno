# Every third-party dependency, pinned by tag or commit (ADR 0004).
# Never use a floating branch. Bumps follow the `bump-deps` checklist and are a
# deliberate, separate commit.
#
# | Dependency            | Version / commit | License            | Used by                      |
# |-----------------------|------------------|--------------------|------------------------------|
# | nlohmann/json         | v3.12.0          | MIT                | libs/device (state JSON)     |
# | dr_libs (wav,flac,mp3)| dfe83776         | MIT-0 / Unlicense  | libs/audiofile (decoding)    |
# | elliptic-blep         | 77bf9866         | MIT                | libs/dsp (oscillators)       |
# | Catch2                | v3.16.1          | BSL-1.0            | tests                        |
# | JUCE                  | 9.0.3            | AGPL-3.0 / comm.   | modules, plugins, app        |
# | clap-juce-extensions  | 7adee3a1         | MIT                | plugins (CLAP)               |
# | tracktion_engine      | 6e4423f1 (develop)| GPL-3.0+ / comm.  | app (+ cmake/patches/)       |
# | Inter (font)          | 4.1              | OFL-1.1            | modules/fd_ui (bundled font) |
# | JetBrains Mono (font) | 2.304            | OFL-1.1            | modules/fd_ui (bundled font) |
# | cpp-httplib           | v0.57.1          | MIT                | app (MCP server, ADR 0021)   |
# | AKWF-FREE (160 cycles)| 8de90bf9         | CC0-1.0            | devices/wavetable (tables)   |
# | libFLAC               | 1.5.0            | BSD-3-Clause       | tools/library (encoding)     |
# | VSCO 2 CE (samples)   | 28092772 (1.1.0) | CC0-1.0            | the Library (segno-library)  |
# | VCSL (samples)        | c1ea7bcc         | CC0-1.0            | the Library (segno-library)  |
# | LAME (MP3 encoder)    | 3.100            | LGPL-2.0-or-later  | app (MP3 export, export.md §17) |

# --- Core (always) ---------------------------------------------------------

CPMAddPackage(
    NAME nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")

CPMAddPackage(
    NAME dr_libs
    GITHUB_REPOSITORY mackron/dr_libs
    GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
    DOWNLOAD_ONLY YES)
add_library(fd_dep_dr_libs INTERFACE)
target_include_directories(fd_dep_dr_libs SYSTEM INTERFACE "${dr_libs_SOURCE_DIR}")

# Signalsmith elliptic BLEP: band-limited oscillators (ADC24 talk code, header-only).
CPMAddPackage(
    NAME elliptic_blep
    GITHUB_REPOSITORY Signalsmith-Audio/elliptic-blep
    GIT_TAG 77bf9866b705ddffe4870b40020411cf9192cf3b
    DOWNLOAD_ONLY YES)
add_library(fd_dep_elliptic_blep INTERFACE)
target_include_directories(fd_dep_elliptic_blep SYSTEM INTERFACE "${elliptic_blep_SOURCE_DIR}")

# AKWF-FREE: single-cycle waveforms, the cycles the Wavetable's AKWF factory tables are made of.
# Only the files devices/wavetable/tables/akwf.json lists are fetched, each checked by its SHA-256
# (cmake/FdAkwf.cmake): the whole repository is 2 GB.
set(FD_AKWF_COMMIT 8de90bf94376670947369e69de0af6b9fbd19286)
include(${CMAKE_CURRENT_LIST_DIR}/FdAkwf.cmake)

# libFLAC: the Library's samples are written as FLAC (tools/library, ADR 0055); the tool only. The
# samples themselves are fetched by the segno-library target, never by a plain build
# (cmake/FdLibrarySources.cmake, tools/library/recipe.json).
if(FD_BUILD_TOOLS)
    CPMAddPackage(
        NAME flac
        URL https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz
        URL_HASH SHA256=f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920
        OPTIONS "BUILD_CXXLIBS OFF" "BUILD_PROGRAMS OFF" "BUILD_EXAMPLES OFF" "BUILD_TESTING OFF" "BUILD_DOCS OFF"
                "INSTALL_MANPAGES OFF" "INSTALL_PKGCONFIG_MODULES OFF" "INSTALL_CMAKE_CONFIG_MODULE OFF"
                "WITH_OGG OFF" "BUILD_SHARED_LIBS OFF")
endif()

if(FD_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        VERSION 3.16.1
        OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_EXTRAS OFF")
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    include(Catch)
endif()

# --- JUCE line ------------------------------------------------------------

set(FD_NEEDS_JUCE OFF)
if(FD_BUILD_PLUGINS OR FD_BUILD_APP)
    set(FD_NEEDS_JUCE ON)
endif()

if(FD_NEEDS_JUCE)
    CPMAddPackage(
        NAME JUCE
        GITHUB_REPOSITORY juce-framework/JUCE
        GIT_TAG 9.0.3
        GIT_SHALLOW ON
        # Local fixes until upstreamed; each patch file explains itself.
        PATCHES "${CMAKE_CURRENT_LIST_DIR}/patches/juce-borderless-maximise.patch")

    # The bundled UI fonts (ui-principles §6): identical rendering on every OS. Inter for names and
    # text; JetBrains Mono for values, readouts and tiny section labels. Both under the OFL.
    CPMAddPackage(
        NAME inter_font
        URL https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
        URL_HASH SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
        DOWNLOAD_ONLY YES)
    CPMAddPackage(
        NAME jetbrains_mono_font
        URL https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
        URL_HASH SHA256=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf
        DOWNLOAD_ONLY YES)
endif()

if(FD_BUILD_PLUGINS)
    CPMAddPackage(
        NAME clap-juce-extensions
        GITHUB_REPOSITORY free-audio/clap-juce-extensions
        GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d)
endif()

if(FD_BUILD_APP)
    # A GitHub archive contains no submodules, which avoids TE's SSH JUCE submodule.
    # We use our own JUCE (above); TE's modules are added with juce_add_modules().
    CPMAddPackage(
        NAME tracktion_engine
        URL https://github.com/Tracktion/tracktion_engine/archive/6e4423f1465bd4260345532ddf1c58d6c09cdaec.tar.gz
        DOWNLOAD_ONLY YES
        # Local fixes until upstreamed; each patch file explains itself.
        PATCHES "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-atomic-state-init.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-instrument-audio-passthrough.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-balance-pan-law.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-automation-playback.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-fader-taper.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-per-track-inputs.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-loop-record-lead-in.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-clip-playback-hook.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-midi-note-continuity.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-input-note-offs.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-modifier-base-value.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-custom-modifiers.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-midi-playback-filter.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-modifier-sidechain.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-master-modifiers.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-warped-clips.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-reduced-monitoring-latency.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-midi-input-consumers.patch"
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-submix-sidechain.patch")

    # The MCP server's HTTP transport (ADR 0021). Header-only; built without TLS or compression
    # (it serves the loopback interface only).
    CPMAddPackage(
        NAME cpp_httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.57.1.tar.gz
        URL_HASH SHA256=5c9e56b6638eb415dc451ac57531e84931fcb50e261ca3dd74eec6b484fb4712
        DOWNLOAD_ONLY YES)
    # The MP3 encoder (export.md §17, EX7): LAME's library built from its pinned source and linked
    # in, so MP3 exports work the same on every machine. LGPL-2.0-or-later, which the GPL (and so
    # Segno's AGPL) can take; its source is the tarball below, unchanged. The encoder only (no
    # mpglib decoder, front end or assembly); cmake/lame/config.h stands in for its configure.
    CPMAddPackage(
        NAME lame
        URL https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
        URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
        DOWNLOAD_ONLY YES)
    set(lame_lib "${lame_SOURCE_DIR}/libmp3lame")
    add_library(fd_dep_lame STATIC
        "${lame_lib}/VbrTag.c" "${lame_lib}/bitstream.c" "${lame_lib}/encoder.c" "${lame_lib}/fft.c"
        "${lame_lib}/gain_analysis.c" "${lame_lib}/id3tag.c" "${lame_lib}/lame.c" "${lame_lib}/newmdct.c"
        "${lame_lib}/presets.c" "${lame_lib}/psymodel.c" "${lame_lib}/quantize.c" "${lame_lib}/quantize_pvt.c"
        "${lame_lib}/reservoir.c" "${lame_lib}/set_get.c" "${lame_lib}/tables.c" "${lame_lib}/takehiro.c"
        "${lame_lib}/util.c" "${lame_lib}/vbrquantize.c" "${lame_lib}/version.c")
    target_include_directories(fd_dep_lame
        PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lame" "${lame_lib}"
        SYSTEM PUBLIC "${lame_SOURCE_DIR}/include")
    target_compile_definitions(fd_dep_lame PRIVATE HAVE_CONFIG_H NDEBUG)
    # Third-party C: its own warnings are its own.
    target_compile_options(fd_dep_lame PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-w> $<$<C_COMPILER_ID:MSVC>:/w>)
    set_target_properties(fd_dep_lame PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT WIN32)
        target_link_libraries(fd_dep_lame PRIVATE m)
    endif()

    find_package(Threads REQUIRED)
    add_library(fd_dep_httplib INTERFACE)
    target_include_directories(fd_dep_httplib SYSTEM INTERFACE "${cpp_httplib_SOURCE_DIR}")
    target_link_libraries(fd_dep_httplib INTERFACE Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32>)
endif()
