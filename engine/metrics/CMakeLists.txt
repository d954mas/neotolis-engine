# nt_metrics -- Layer-1 perf collector. NT_METRICS_ENABLED is PUBLIC so consumers
# see the same value (real bodies vs no-op stubs). Independent of NT_DEVAPI_ENABLED.

nt_add_module(nt_metrics LOG_DOMAIN "metrics"
    nt_metrics.c
    nt_metrics.h
)

# nt_core PUBLIC (header uses its types); nt_hash PRIVATE (impl-only, full-name keying).
target_link_libraries(nt_metrics
    PUBLIC nt_core
    PRIVATE nt_hash
)

# ring_stats uses ceil() (nearest-rank percentiles); on Linux/GNU ld libm is separate from libc
# (Windows/macOS fold it into libc). Mirrors engine/math + engine/window.
if(NOT WIN32)
    target_link_libraries(nt_metrics PRIVATE m)
endif()

if(NT_METRICS_ENABLED)
    target_compile_definitions(nt_metrics PUBLIC NT_METRICS_ENABLED=1)
else()
    target_compile_definitions(nt_metrics PUBLIC NT_METRICS_ENABLED=0)
endif()
