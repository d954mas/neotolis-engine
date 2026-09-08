# cmake/nt_wasm_app.cmake
#
# nt_configure_wasm_app(<target> [TITLE <title>])
#
# The emscripten link policy every browser target shares: index.html emitted through the shared
# shell, a WebGL2-only context, debug diagnostics, and the analysis-build name section. No-op off
# Emscripten, so callers need no if(EMSCRIPTEN) guard.
#
# Parameters:
#   target        (positional, required) The CMake target to configure
#   TITLE <title> (optional) Page title, forwarded to nt_configure_shell
#
# Example:
#   nt_configure_wasm_app(hello TITLE "Hello - Neotolis Engine")

function(nt_configure_wasm_app target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    cmake_parse_arguments(ARG "" "TITLE" "" ${ARGN})

    set_target_properties(${target} PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(${target} TITLE "${ARG_TITLE}" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")

    target_link_options(${target} PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(${target} PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
    # Without the name section scripts/size-report.sh has function indices, not symbols, to report.
    if(NT_WASM_ANALYSIS)
        target_link_options(${target} PRIVATE "SHELL:--profiling-funcs")
    endif()
endfunction()
