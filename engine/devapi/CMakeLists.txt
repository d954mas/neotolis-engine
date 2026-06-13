# nt_devapi -- self-describing command layer (dev-only, gated by NT_DEVAPI_ENABLED)
#
# Source list is intentionally minimal for Plan 01 (only files that exist).
# Plans 02/03 extend it: add nt_devapi.c (submit/dispatch), nt_devapi_core.c
# (ping/engine.info/view), nt_devapi_discovery.c (endpoints/command.describe/features).
nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi_registry.c
    nt_devapi.h
    nt_devapi_internal.h
    nt_devapi_stub.h
)

# cjson is PUBLIC: the nt_devapi_handler_fn typedef in the public header takes
# cJSON* params, so cJSON.h must be visible to every consumer of nt_devapi.h.
target_link_libraries(nt_devapi PUBLIC nt_core cjson PRIVATE nt_app nt_window nt_log)

# Master gate: this CMakeLists only runs when enabled, so define is always 1 here.
target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)

# D-09: per-group compile-time opt-in. Plans 02/03 wrap their core handlers in
# #ifdef NT_DEVAPI_REGISTER_core; 65-69 clone this pattern per group.
target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_core=1)

# engine.info version sourcing (consumed in Plan 02), injected now so the def
# exists from module creation — mirrors the Clay-pin compile-def precedent.
target_compile_definitions(nt_devapi PRIVATE
    NT_DEVAPI_ENGINE_VERSION="1.9-dev"
    NT_PRESET_NAME="${NT_PRESET_NAME}"
    NT_DEVAPI_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
