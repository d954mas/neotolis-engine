nt_add_module(nt_sort LOG_DOMAIN "sort"
    nt_sort.c
    nt_sort.h
)
target_link_libraries(nt_sort PUBLIC nt_core)
