nt_add_module(nt_resource
    nt_resource.c
    nt_resource.h
)
target_link_libraries(nt_resource PUBLIC nt_core nt_shared nt_log nt_http nt_fs nt_time)
target_compile_definitions(nt_resource PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
