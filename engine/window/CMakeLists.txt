# nt_window -- canvas/window abstraction
# Plan 01: shared DPR math only (no platform backends yet)
# Plan 02 adds: web/nt_window_web.c, native/nt_window_native.c

nt_add_module(nt_window
    nt_window.c
    nt_window.h
)

target_link_libraries(nt_window PUBLIC nt_core)
