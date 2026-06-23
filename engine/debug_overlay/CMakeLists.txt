# Overlay reads all stats from nt_metrics; with metrics OFF it would format no-op stubs (empty HUD),
# so hard-fail to keep the two enabled together.
if(NT_UI_DEBUG_TOOLS AND NOT NT_METRICS_ENABLED)
    message(FATAL_ERROR "NT_UI_DEBUG_TOOLS requires NT_METRICS_ENABLED (the debug overlay now consumes "
        "nt_metrics for fps/cpu/gpu/draws + user counters; with metrics OFF it would read no-op stubs — "
        "an empty HUD). Enable both, e.g. -DNT_METRICS_ENABLED=ON (NT_UI_DEBUG_TOOLS defaults it ON).")
endif()

nt_add_module(nt_debug_overlay LOG_DOMAIN "debug_overlay"
    nt_debug_overlay.c
    nt_debug_overlay.h
)
# Pure consumer of nt_metrics: reads all stats from it, so no gfx/time/hash deps.
target_link_libraries(nt_debug_overlay PUBLIC nt_core nt_font nt_material nt_text_renderer)
target_link_libraries(nt_debug_overlay PRIVATE nt_metrics)
