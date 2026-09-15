nt_add_module(nt_anim LOG_DOMAIN "anim"
    nt_anim.c
    nt_anim.h
)

target_link_libraries(nt_anim PUBLIC nt_core nt_hash)
