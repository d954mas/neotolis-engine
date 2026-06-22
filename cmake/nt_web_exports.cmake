# cmake/nt_web_exports.cmake
#
# nt_web_exports(<target>)
#
# Applies the COMPLETE, authoritative emcc export superset for any web executable
# that links the web nt_http and/or nt_clipboard backends. Call this on the exe
# instead of relying on per-module INTERFACE += flags.
#
# WHY a single helper and not per-module INTERFACE +=:
#   emcc collapses -sEXPORTED_RUNTIME_METHODS / -sEXPORTED_FUNCTIONS to ONE
#   effective value on the link line. A module's `target_link_options(<m> INTERFACE
#   "SHELL:-s...+=X")` does NOT reliably survive into the final glue: verified on a
#   single-module example (textured_quad links nt_http alone) the `+=HEAPU8` lands
#   in `unexportedSymbols` -- i.e. NOT exported. The only durable form is one
#   authoritative `=` list per flag on the exe. This helper IS that list, shared so
#   it can't be forgotten or partially copied per example.
#
# Superset contents (keep in sync with the web backends + the showcase test hooks):
#   Runtime methods:
#     ccall          - generic call surface
#     HEAPU8         - nt_http web backend (EM_JS reads request bytes)
#     stringToNewUTF8- nt_clipboard web backend (paste string -> C heap)
#     UTF8ToString   - nt_clipboard + window.__nt readouts decode C strings
#   Functions:
#     _main          - entry (emcc default, listed explicitly so the `=` doesn't drop it)
#     _malloc,_free  - back stringToNewUTF8 + the paste listener's free
#
# KEEPALIVE C exports (e.g. _nt_input_web_*, _nt_test_*) are auto-added by
# EMSCRIPTEN_KEEPALIVE in the C sources and do NOT need listing here.

function(nt_web_exports target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    target_link_options(${target} PRIVATE
        "SHELL:-sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,stringToNewUTF8,UTF8ToString"
        "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free"
    )
endfunction()
