nt_add_module(nt_pool LOG_DOMAIN "pool"
    nt_pool.c
    nt_pool.h
)
target_link_libraries(nt_pool PUBLIC nt_core)
