# nt_crash -- opt-in crash record (see nt_crash.h). Nothing depends on it, so it has no
# interface/stub: a game that does not link it pays nothing. It reads the GPU snapshot, so the
# executable also links nt_gfx or nt_gfx_stub.
if(EMSCRIPTEN)
    nt_add_module(nt_crash LOG_DOMAIN "crash" nt_crash.c web/nt_crash_web.c nt_crash.h nt_crash_internal.h)
else()
    nt_add_module(nt_crash LOG_DOMAIN "crash" nt_crash.c native/nt_crash_native.c nt_crash.h nt_crash_internal.h)
    if(NOT WIN32)
        target_compile_definitions(nt_crash PRIVATE _GNU_SOURCE) # REG_RIP, dl_iterate_phdr
    endif()
endif()
target_link_libraries(nt_crash PRIVATE nt_core nt_log_interface nt_version nt_gfx_interface)
