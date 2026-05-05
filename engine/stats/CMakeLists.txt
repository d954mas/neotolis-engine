nt_add_module(nt_stats LOG_DOMAIN "stats"
    nt_stats.c
    nt_stats.h
)
target_link_libraries(nt_stats PUBLIC nt_core nt_font nt_material nt_text_renderer)
target_link_libraries(nt_stats PRIVATE nt_time nt_hash nt_log)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)
