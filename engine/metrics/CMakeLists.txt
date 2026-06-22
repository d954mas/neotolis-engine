# nt_metrics -- Layer-1 perf collector.
# One TU always compiles; the 0/1 NT_METRICS_ENABLED define (PUBLIC so consumers
# see the same value) selects real bodies vs zero-footprint no-op stubs for the
# release/OFF mirror. Independent of NT_DEVAPI_ENABLED.

nt_add_module(nt_metrics LOG_DOMAIN "metrics"
    nt_metrics.c
    nt_metrics.h
)

# Pure storage + math: the host pushes raw per-frame scalars (nt_metrics_frame_t) and
# sets user counters; nt_metrics references no other engine module. nt_core (types + assert)
# is PUBLIC — the header uses its types; nt_hash is PRIVATE (an impl detail used only in
# nt_metrics.c for full-name keying; static-lib PRIVATE still propagates to the exe link).
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
