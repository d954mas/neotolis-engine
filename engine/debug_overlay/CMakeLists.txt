nt_add_module(nt_debug_overlay LOG_DOMAIN "debug_overlay"
    nt_debug_overlay.c
    nt_debug_overlay.h
)
target_link_libraries(nt_debug_overlay PUBLIC nt_core nt_font nt_material nt_text_renderer)
target_link_libraries(nt_debug_overlay PRIVATE nt_time nt_hash nt_log_interface)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)
