add_library(nt_engine STATIC
    runtime/core/nt_core.c
)

target_include_directories(nt_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nt_engine PUBLIC nt_shared)

# Vendored cglm integration
if(TARGET cglm_headers)
    target_link_libraries(nt_engine PUBLIC cglm_headers)
endif()

# Platform compile definitions
target_compile_definitions(nt_engine PUBLIC
    $<$<BOOL:${EMSCRIPTEN}>:NT_PLATFORM_WEB>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:NT_PLATFORM_NATIVE>
    $<$<CONFIG:Debug>:NT_CONFIG_DEBUG>
    $<$<CONFIG:Debug>:NT_ENABLE_ASSERTS>
)

# Strict warning and sanitizer policy for engine code
nt_set_warning_flags(nt_engine)
nt_set_sanitizer_flags(nt_engine)

# Per-project output: build/engine/[preset]/
set_target_properties(nt_engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
)
