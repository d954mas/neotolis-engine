# nt_platform_web -- web-specific shell interactions (WASM only)
# OBJECT library: EM_JS symbols need direct linking, not static archive.

add_library(nt_platform_web OBJECT nt_platform_web.c)
target_include_directories(nt_platform_web PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
nt_set_warning_flags(nt_platform_web)
nt_set_sanitizer_flags(nt_platform_web)
target_link_libraries(nt_platform_web PUBLIC nt_core)
