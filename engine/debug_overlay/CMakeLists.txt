# The overlay now CONSUMES nt_metrics for fps/cpu/gpu/draws + user counters. With the inspector +
# overlay enabled (NT_UI_DEBUG_TOOLS) but metrics OFF, the overlay would format from no-op metrics
# stubs (always 0 fps / N/A gpu / no counters) — a vacuously-empty HUD. Hard-fail so the two move
# together; mirrors the GROUP_OBS guard. The OFF mirror (NT_UI_DEBUG_TOOLS=OFF) defaults metrics OFF
# too, so it never trips.
if(NT_UI_DEBUG_TOOLS AND NOT NT_METRICS_ENABLED)
    message(FATAL_ERROR "NT_UI_DEBUG_TOOLS requires NT_METRICS_ENABLED (the debug overlay now consumes "
        "nt_metrics for fps/cpu/gpu/draws + user counters; with metrics OFF it would read no-op stubs — "
        "an empty HUD). Enable both, e.g. -DNT_METRICS_ENABLED=ON (NT_UI_DEBUG_TOOLS defaults it ON).")
endif()

nt_add_module(nt_debug_overlay LOG_DOMAIN "debug_overlay"
    nt_debug_overlay.c
    nt_debug_overlay.h
)
# Pure consumer of nt_metrics: reads fps/cpu/gpu/draws + user counters from it and formats the HUD.
# No gfx/time/hash deps anymore — the host pushes raw data into nt_metrics, the overlay only reads.
target_link_libraries(nt_debug_overlay PUBLIC nt_core nt_font nt_material nt_text_renderer)
target_link_libraries(nt_debug_overlay PRIVATE nt_metrics)
