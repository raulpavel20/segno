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
        GIT_SHALLOW ON)

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
                "${CMAKE_CURRENT_LIST_DIR}/patches/tracktion_engine-clip-playback-hook.patch")

    # The MCP server's HTTP transport (ADR 0021). Header-only; built without TLS or compression
    # (it serves the loopback interface only).
    CPMAddPackage(
        NAME cpp_httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.57.1.tar.gz
        URL_HASH SHA256=5c9e56b6638eb415dc451ac57531e84931fcb50e261ca3dd74eec6b484fb4712
        DOWNLOAD_ONLY YES)
    find_package(Threads REQUIRED)
    add_library(fd_dep_httplib INTERFACE)
    target_include_directories(fd_dep_httplib SYSTEM INTERFACE "${cpp_httplib_SOURCE_DIR}")
    target_link_libraries(fd_dep_httplib INTERFACE Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32>)
endif()
