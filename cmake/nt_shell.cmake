# cmake/nt_shell.cmake
#
# nt_configure_shell(<target> [TITLE <title>] [SHELL_FILE <path>] [FULLSCREEN_BUTTON])
#
# Configures an HTML shell template for WASM builds. Processes the template
# through configure_file(@ONLY) to resolve @VAR@ placeholders, then applies
# --shell-file to the target for Emscripten to resolve {{{ SCRIPT }}}.
#
# Parameters:
#   target            (positional, required) The CMake target to configure
#   TITLE <title>     (optional) Page title. Defaults to ${target}.
#   SHELL_FILE <path> (optional) Custom shell template path. Also processed
#                     through configure_file(@ONLY) for @VAR@ resolution.
#   FULLSCREEN_BUTTON (optional flag) When present, enables a fullscreen
#                     toggle bar below the canvas. OFF by default.
#
# Template variables set for configure_file:
#   @NT_SHELL_TITLE@           - Page title
#   @NT_SHELL_FULLSCREEN_BLOCK@ - HTML/CSS/JS for fullscreen bar (or empty)
#
# Example:
#   nt_configure_shell(hello TITLE "Hello - Neotolis Engine")
#   nt_configure_shell(my_game TITLE "My Game" FULLSCREEN_BUTTON)
#   nt_configure_shell(my_game SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/custom_shell.html.in")

function(nt_configure_shell target)
    if(NOT EMSCRIPTEN)
        return()
    endif()

    cmake_parse_arguments(SHELL "FULLSCREEN_BUTTON" "TITLE;SHELL_FILE" "" ${ARGN})

    # Default title to target name
    if(NOT SHELL_TITLE)
        set(SHELL_TITLE "${target}")
    endif()

    # Set template variables for configure_file
    set(NT_SHELL_TITLE "${SHELL_TITLE}")

    # Handle FULLSCREEN_BUTTON: inject HTML/CSS/JS block or empty string
    if(SHELL_FULLSCREEN_BUTTON)
        set(NT_SHELL_FULLSCREEN_BLOCK [=[
<style>canvas { height: calc(100% - 32px) !important; }</style>
<div id="fullscreen-bar" style="width:100%;height:32px;background:#111;display:flex;align-items:center;justify-content:flex-end;padding:0 8px;">
    <button onclick="document.getElementById('canvas').requestFullscreen()" style="background:#222;color:#ccc;border:1px solid #444;padding:4px 12px;cursor:pointer;font-size:13px;">Fullscreen</button>
</div>]=])
    else()
        set(NT_SHELL_FULLSCREEN_BLOCK "")
    endif()

    # Determine template source
    if(SHELL_SHELL_FILE)
        set(_shell_src "${SHELL_SHELL_FILE}")
    else()
        set(_shell_src "${CMAKE_SOURCE_DIR}/engine/platform/web/shell.html.in")
    endif()

    # Configure template -> build dir
    set(_shell_out "${CMAKE_CURRENT_BINARY_DIR}/${target}_shell.html")
    configure_file("${_shell_src}" "${_shell_out}" @ONLY)

    # Apply --shell-file to target
    target_link_options(${target} PRIVATE "SHELL:--shell-file ${_shell_out}")
endfunction()
