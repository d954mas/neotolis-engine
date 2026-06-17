nt_add_module(nt_stats LOG_DOMAIN "stats"
    nt_stats.c
    nt_stats.h
)
target_link_libraries(nt_stats PUBLIC nt_core nt_font nt_material nt_text_renderer)
target_link_libraries(nt_stats PRIVATE nt_time nt_hash nt_log_interface)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)

# Headless counter stub: only nt_stats_get_draw_calls (-> 0). Lets devapi `render.info` tests
# link without the nt_font/nt_material/nt_text_renderer/nt_gfx chain. Mirrors nt_window_stub.
nt_add_module(nt_stats_stub LOG_DOMAIN "stats_stub"
    stub/nt_stats_stub.c
)
target_link_libraries(nt_stats_stub PUBLIC nt_core)
