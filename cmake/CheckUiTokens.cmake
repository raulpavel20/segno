# ADR 0023 (docs/design/ui-foundations.md): views use colour roles, text styles and shared
# components from fd_ui. Outside modules/fd_ui, UI code never mixes colours, picks fonts
# directly or uses literal colours.
# Run as: cmake -DROOT=<repository root> -P CheckUiTokens.cmake
set(failed "")
file(GLOB_RECURSE files
    "${ROOT}/app/src/ui/*.h" "${ROOT}/app/src/ui/*.cpp"
    "${ROOT}/modules/fd_device_ui/*.h" "${ROOT}/modules/fd_device_ui/*.cpp"
    "${ROOT}/devices/*/ui/*.h" "${ROOT}/devices/*/ui/*.cpp")
set(rules
    "fonts::font\\(|fonts::mono\\(" "a direct font call (use ui::font(TextStyle))"
    "interpolatedWith\\(|withAlpha\\(|withMultipliedAlpha\\(|brighter\\(|darker\\(|withBrightness\\(" "colour mixing (use a colour role)"
    "juce::Colour\\(0x|Colours::[a-su-z]" "a literal colour (use a colour role)")
foreach(f IN LISTS files)
    # Split into lines, keeping empty ones (for line numbers); protect list separators first.
    file(READ "${f}" content)
    string(REPLACE "\\" "/" content "${content}")
    string(REPLACE ";" "<sc>" content "${content}")
    string(REPLACE "[" "<lb>" content "${content}")
    string(REPLACE "]" "<rb>" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")
    set(n 0)
    foreach(line IN LISTS lines)
        math(EXPR n "${n} + 1")
        set(i 0)
        list(LENGTH rules count)
        while(i LESS count)
            list(GET rules ${i} pattern)
            math(EXPR j "${i} + 1")
            list(GET rules ${j} why)
            if(line MATCHES "${pattern}")
                file(RELATIVE_PATH rel "${ROOT}" "${f}")
                list(APPEND failed "${rel}:${n}: ${why}")
            endif()
            math(EXPR i "${i} + 2")
        endwhile()
    endforeach()
endforeach()
if(failed)
    list(LENGTH failed total)
    string(REPLACE ";" "\n  " failed "${failed}")
    message(FATAL_ERROR "UI lint: ${total} problem(s):\n  ${failed}")
endif()
