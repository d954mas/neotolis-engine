nt_add_module(nt_resource
    nt_resource.c
    nt_resource.h
)
target_link_libraries(nt_resource PUBLIC nt_core nt_shared nt_log)
