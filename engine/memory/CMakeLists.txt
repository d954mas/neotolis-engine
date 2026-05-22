nt_add_module(nt_mem_scratch LOG_DOMAIN "mem_scratch"
    nt_mem_scratch.c
    nt_mem_scratch.h
)
target_link_libraries(nt_mem_scratch PUBLIC nt_core)
