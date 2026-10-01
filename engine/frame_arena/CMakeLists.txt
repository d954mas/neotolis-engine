nt_add_module(nt_frame_arena LOG_DOMAIN "frame_arena"
    nt_frame_arena.c
    nt_frame_arena.h
)
target_link_libraries(nt_frame_arena PUBLIC nt_core nt_gfx_interface)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_fake)
