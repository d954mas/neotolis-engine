nt_add_module(nt_core LOG_DOMAIN "core"
    nt_assert.c
    nt_core.c
    nt_core.h
    nt_assert.h
    nt_types.h
    nt_platform.h
)

# nt_shared headers used in nt_core's public API
target_link_libraries(nt_core PUBLIC nt_shared nt_log_interface)

# Build identity for nt_engine_build_string / _preset_string (used by engine.info, logs, game).
target_compile_definitions(nt_core PRIVATE
    NT_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    NT_PRESET_NAME="${NT_PRESET_NAME}"
)
