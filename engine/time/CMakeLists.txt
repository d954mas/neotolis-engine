nt_add_module(nt_time
    nt_time.c
    nt_time.h
)
target_link_libraries(nt_time PUBLIC nt_core)

if(WIN32)
    target_link_libraries(nt_time PRIVATE winmm)
endif()
