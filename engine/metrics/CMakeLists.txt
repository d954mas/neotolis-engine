# nt_metrics -- Layer-1 perf collector.
# One TU always compiles; the 0/1 NT_METRICS_ENABLED define (PUBLIC so consumers
# see the same value) selects real bodies vs zero-footprint no-op stubs for the
# release/OFF mirror. Independent of NT_DEVAPI_ENABLED.

nt_add_module(nt_metrics LOG_DOMAIN "metrics"
    nt_metrics.c
    nt_metrics.h
)

# Reads existing sources: overlay getters, gfx draw_calls, scratch hwm/used,
# nt_platform memory. nt_add_module already gives the engine/ include root, so the
# headers resolve from nt_core alone; nt_gfx_interface adds gfx's include path.
# The actual getter symbols resolve at exe/test link time (the app picks the impls),
# mirroring nt_window's "header-here, symbol-at-exe-link" contract.
target_link_libraries(nt_metrics PUBLIC
    nt_core
    nt_gfx_interface
    nt_hash
)

if(NT_METRICS_ENABLED)
    target_compile_definitions(nt_metrics PUBLIC NT_METRICS_ENABLED=1)
else()
    target_compile_definitions(nt_metrics PUBLIC NT_METRICS_ENABLED=0)
endif()
