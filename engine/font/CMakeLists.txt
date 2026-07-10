nt_add_module(nt_font LOG_DOMAIN "font"
    nt_font.c
    nt_font.h
    nt_font_hot.h
    nt_font_internal.h
)
target_link_libraries(nt_font PUBLIC nt_resource nt_hash nt_pool nt_gfx_interface nt_math)
target_link_libraries(nt_font PRIVATE nt_log_interface nt_utf8)
nt_set_static_crt(nt_font)
