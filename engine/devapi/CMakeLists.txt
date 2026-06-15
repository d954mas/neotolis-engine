# nt_devapi -- self-describing command layer (dev-only, gated by NT_DEVAPI_ENABLED)
nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi.c
    nt_devapi_core.c
    nt_devapi_window.c
    nt_devapi_discovery.c
    nt_devapi_registry.c
    nt_devapi.h
    nt_devapi_internal.h
    nt_devapi_types.h
    nt_devapi_stub.h
)

# cjson is PUBLIC: the nt_devapi_handler_fn typedef in the public header takes
# cJSON* params, so cJSON.h must be visible to every consumer of nt_devapi.h.
target_link_libraries(nt_devapi PUBLIC nt_core cjson PRIVATE nt_log)

# Master gate: this CMakeLists only runs when enabled, so define is always 1 here.
target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)

# Per-group compile-time opt-in: handlers are wrapped in #ifdef NT_DEVAPI_REGISTER_<group>.
target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_core=1)

# Window group (view): the only group that reads g_nt_window. Gate + nt_window link are
# tied to this option, so NT_DEVAPI_WINDOW=OFF builds devapi without the window dependency.
option(NT_DEVAPI_WINDOW "devapi: include the window group (view; links nt_window)" ON)
if(NT_DEVAPI_WINDOW)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_window=1)
    target_link_libraries(nt_devapi PRIVATE nt_window)
endif()

# engine.info build/preset facts (version comes from nt_core's nt_engine_version_string()).
target_compile_definitions(nt_devapi PRIVATE
    NT_PRESET_NAME="${NT_PRESET_NAME}"
    NT_DEVAPI_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
