# Swappable log module
# API header: nt_log.h (shared by both implementations)
# Real impl:  default/nt_log.c (printf-based output)
# Stub impl:  stub/nt_log_stub.c (no-op empty bodies)

nt_add_module(nt_log LOG_DOMAIN "log"
    default/nt_log.c
    nt_log.h
)

# Real impl dedups nt_log_write_unique by xxHash64 of the message; the stub stays dep-free.
target_link_libraries(nt_log PRIVATE nt_hash)

nt_add_module(nt_log_stub LOG_DOMAIN "log_stub"
    stub/nt_log_stub.c
    nt_log.h
)
