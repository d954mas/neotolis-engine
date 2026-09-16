nt_add_module(nt_anim LOG_DOMAIN "anim"
    nt_anim.c
    nt_anim.h
)

target_link_libraries(nt_anim PUBLIC nt_core nt_hash)

if(NOT NT_ANIM_CHECKS STREQUAL "")
    target_compile_definitions(nt_anim PUBLIC NT_ANIM_CHECKS=${NT_ANIM_CHECKS})
endif()
