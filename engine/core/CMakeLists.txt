nt_add_module(nt_core LOG_DOMAIN "core"
    nt_core.c
    nt_core.h
    nt_types.h
    nt_platform.h
)

# nt_shared headers used in nt_core's public API
target_link_libraries(nt_core PUBLIC nt_shared nt_log)
