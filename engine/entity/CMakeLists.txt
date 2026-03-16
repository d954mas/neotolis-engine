nt_add_module(nt_entity
    nt_entity.c
    nt_entity.h
)
target_link_libraries(nt_entity PUBLIC nt_core nt_log)
