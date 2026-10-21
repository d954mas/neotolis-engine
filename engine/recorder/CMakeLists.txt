# nt_recorder -- dev-only video recorder (see nt_recorder.h). Like nt_crash it has no option and no
# stub: a game that does not link it pays nothing. A platform without an encoder backend defines no
# target, so linking it fails (at configure through nt::recorder).
if(EMSCRIPTEN)
    nt_add_module(nt_recorder LOG_DOMAIN "recorder" nt_recorder.c web/nt_recorder_web.c nt_recorder.h nt_recorder_internal.h)
    # Outside Closure: WebCodecs and Mediabunny property names must survive wasm-release minification.
    set(_nt_recorder_js "${NT_ENGINE_ROOT}/deps/mediabunny/nt_mediabunny.js" "${CMAKE_CURRENT_SOURCE_DIR}/web/nt_recorder_web.js")
    foreach(_js IN LISTS _nt_recorder_js)
        target_link_options(nt_recorder INTERFACE "SHELL:--extern-pre-js \"${_js}\"")
    endforeach()
    set_property(TARGET nt_recorder APPEND PROPERTY INTERFACE_LINK_DEPENDS ${_nt_recorder_js})
else()
    return()
endif()
target_link_libraries(nt_recorder PRIVATE nt_core nt_log_interface nt_window_interface)
