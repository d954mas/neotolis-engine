nt_add_module(nt_postfx_blur LOG_DOMAIN "postfx_blur"
    nt_postfx_blur.c
    nt_postfx_blur.h
)
target_link_libraries(nt_postfx_blur PUBLIC nt_core nt_gfx_interface)
if(NOT WIN32)
    target_link_libraries(nt_postfx_blur PUBLIC m)
endif()
