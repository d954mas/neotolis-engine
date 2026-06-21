# nt_metrics -- Layer-1 perf collector.
# One TU always compiles; the 0/1 NT_METRICS_ENABLED define (PUBLIC so consumers
# see the same value) selects real bodies vs zero-footprint no-op stubs for the
# release/OFF mirror. Independent of NT_DEVAPI_ENABLED.

nt_add_module(nt_metrics LOG_DOMAIN "metrics"
    nt_metrics.c
    nt_metrics.h
)

# Pure storage + math: the host pushes raw per-frame scalars (nt_metrics_frame_t) and
# sets user counters; nt_metrics references no other engine module. Only nt_core (types
# + assert) and nt_hash (full-name keying) — no exe-link-time provider contract.
target_link_libraries(nt_metrics PUBLIC
    nt_core
    nt_hash
)

if(NT_METRICS_ENABLED)
    target_compile_definitions(nt_metrics PUBLIC NT_METRICS_ENABLED=1)
else()
    target_compile_definitions(nt_metrics PUBLIC NT_METRICS_ENABLED=0)
endif()
