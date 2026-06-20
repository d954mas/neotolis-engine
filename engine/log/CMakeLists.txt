# Swappable log module
# API header: nt_log.h (shared by both implementations)
# Real impl:  default/nt_log.c (printf-based output)
# Stub impl:  stub/nt_log_stub.c (no-op empty bodies)
# Interface:  nt_log_interface (header-only; modules link this, exe picks an impl)

# Header-only interface carries nt_log.h's include root; the exe selects the impl.
nt_declare_interface(nt_log)

nt_add_module(nt_log LOG_DOMAIN "log"
    default/nt_log.c
    nt_log.h
)
target_link_libraries(nt_log PUBLIC nt_log_interface)

# Real impl dedups nt_log_write_unique by xxHash64 of the message; the stub stays dep-free.
# nt_core supplies nt_assert_handler for the NT_ASSERT in nt_log_add_sink (host-call invariant).
target_link_libraries(nt_log PRIVATE nt_hash nt_core)

nt_add_module(nt_log_stub LOG_DOMAIN "log_stub"
    stub/nt_log_stub.c
    nt_log.h
)
target_link_libraries(nt_log_stub PUBLIC nt_log_interface)

# Reusable dev-only log-tail ring (OBS-01/02). One TU always compiles; the
# 0/1 NT_LOG_RING_ENABLED define (PUBLIC so consumers see the same value)
# selects real bodies vs zero-footprint no-op stubs for the release/OFF mirror.
nt_add_module(nt_log_ring LOG_DOMAIN "log_ring"
    nt_log_ring.c
    nt_log_ring.h
)
target_link_libraries(nt_log_ring PUBLIC nt_log_interface)
if(NT_LOG_RING_ENABLED)
    target_compile_definitions(nt_log_ring PUBLIC NT_LOG_RING_ENABLED=1)
else()
    target_compile_definitions(nt_log_ring PUBLIC NT_LOG_RING_ENABLED=0)
endif()
