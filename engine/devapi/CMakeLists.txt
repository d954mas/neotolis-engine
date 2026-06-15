# nt_devapi -- self-describing command layer (dev-only, gated by NT_DEVAPI_ENABLED)
nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi.c
    nt_devapi_core.c
    nt_devapi_discovery.c
    nt_devapi_registry.c
    nt_devapi.h
    nt_devapi_internal.h
    nt_devapi_stub.h
)

# cjson is PUBLIC: the nt_devapi_handler_fn typedef in the public header takes
# cJSON* params, so cJSON.h must be visible to every consumer of nt_devapi.h.
target_link_libraries(nt_devapi PUBLIC nt_core cjson PRIVATE nt_window nt_log)

# Master gate: this CMakeLists only runs when enabled, so define is always 1 here.
target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)

# Per-group compile-time opt-in: handlers are wrapped in
# #ifdef NT_DEVAPI_REGISTER_core; later groups clone this pattern.
target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_core=1)

# engine.info pulls version/build/preset from nt_core (nt_engine_*_string()).
