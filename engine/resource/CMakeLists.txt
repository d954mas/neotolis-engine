# Resource is fixed; nt_declare_interface marks a module as swappable.
add_library(nt_resource_interface INTERFACE)
target_link_libraries(nt_resource_interface INTERFACE nt_core)
target_compile_definitions(nt_resource_interface INTERFACE NT_RESOURCE_TIMING_ENABLED=$<BOOL:${NT_RESOURCE_TIMING_ENABLED}>)

nt_add_module(nt_resource LOG_DOMAIN "resource"
    nt_resource.c
    nt_resource.h
)
target_link_libraries(nt_resource PUBLIC nt_resource_interface nt_core nt_shared nt_log_interface nt_http_interface nt_fs_interface nt_time nt_hash)
target_compile_definitions(nt_resource PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
