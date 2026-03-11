# nt_platform_web -- web-specific shell interactions (WASM only)

nt_add_module(nt_platform_web
    nt_platform_web.c
    nt_platform_web.h
)

target_link_libraries(nt_platform_web PUBLIC nt_core)
