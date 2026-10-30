nt_add_module(nt_memory LOG_DOMAIN "memory"
    nt_arena.c
    nt_arena.h
    nt_mem_scratch.h
)
target_link_libraries(nt_memory PUBLIC nt_core)
