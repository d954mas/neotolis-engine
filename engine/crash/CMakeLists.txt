# nt_crash -- opt-in crash record (see nt_crash.h). Nothing depends on it, so it has no
# interface/stub: a game that does not link it pays nothing.
if(EMSCRIPTEN)
    nt_add_module(nt_crash LOG_DOMAIN "crash" nt_crash.c web/nt_crash_web.c nt_crash.h nt_crash_internal.h)
    target_link_libraries(nt_crash PUBLIC nt_core nt_log_interface nt_version)
endif()
