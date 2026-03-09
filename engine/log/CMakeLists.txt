# Swappable log module
# API header: nt_log.h (shared by both implementations)
# Real impl:  default/nt_log.c (printf-based output)
# Stub impl:  stub/nt_log_stub.c (no-op empty bodies)

nt_add_module(nt_log
    default/nt_log.c
    nt_log.h
)

nt_add_module(nt_log_stub
    stub/nt_log_stub.c
    nt_log.h
)
