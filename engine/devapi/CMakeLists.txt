# Every devapi command group is an OPTIONAL module ("set of modules / use only what you need"). Each
# NT_DEVAPI_GROUP_<X> switch (defined at the project root, before the engine subdirs) gates the WHOLE
# group: its TU is compiled, its commands registered, its lifecycle hooks wired, its engine deps
# linked — ALL only when ON. OFF -> the devapi core/net carry ZERO symbols of that group or its deps.
# Fail-fast is accepted: ON without the matching engine impl in the exe fails at the final link, which
# is the developer's responsibility (no graceful no-op fallback).

# nt_devapi -- transport-agnostic, self-describing command core (dev-only, gated by NT_DEVAPI_ENABLED).
# The ALWAYS-compiled base is just the framework: dispatch + self-describing registry + lifecycle-hook
# registry. Command groups are added by the per-group if() blocks below.
nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi.c
    nt_devapi_registry.c
    nt_devapi.h
    nt_devapi_internal.h
    nt_devapi_stub.h
)

# cjson is PUBLIC: the nt_devapi_handler_fn typedef in the public header takes cJSON* params, so
# cJSON.h must be visible to every consumer of nt_devapi.h. nt_core + nt_log_interface are
# cross-cutting (the registry/dispatch framework needs them regardless of which groups are built).
# nt_app_interface is ALSO a base dep: the deferred-response queue in nt_devapi.c (a core dispatch
# feature, not a group) reads g_nt_app.frame / g_nt_app.time for its frame/time deadlines. The
# g_nt_app symbol resolves at the final-exe link (the consumer links nt_app[_stub]). Per-group engine
# deps are linked under their own if() block below, so a group-OFF devapi carries none of that group's
# link or symbols.
target_link_libraries(nt_devapi PUBLIC nt_core cjson PRIVATE nt_log_interface nt_app_interface)

# Master gate: this CMakeLists only runs when enabled, so define is always 1 here.
target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)

# Core group: ping / engine.info / view. nt_window_interface is its only engine dep (view reads
# g_nt_window). OFF -> the core/net carry no window symbols and the core handlers compile out.
if(NT_DEVAPI_GROUP_CORE)
    target_sources(nt_devapi PRIVATE nt_devapi_core.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_CORE=1)
    target_link_libraries(nt_devapi PRIVATE nt_window_interface)
endif()

# Time group (time.*/render.*/frame.*): owns the nt_gfx header dep (render.info reads draw_calls via
# nt_gfx_get_frame_draw_calls). The handlers also forward to nt_app_*, but nt_app_interface is already
# a base dep (the deferred queue), so only nt_gfx_interface is added here. Those symbols resolve at the
# final-exe link (the consumer links nt_gfx[_stub]) — nt_devapi stays a dev-only static lib with no
# impl. OFF -> the core/net carry no nt_gfx_* link or symbols.
if(NT_DEVAPI_GROUP_TIME)
    target_sources(nt_devapi PRIVATE nt_devapi_time.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_TIME=1)
    target_link_libraries(nt_devapi PRIVATE nt_gfx_interface)
endif()

# Discovery group: endpoints / command.describe / features — the self-describing surface a client
# reads without source. Reads only the registry (no engine dep). OFF -> the introspection commands
# compile out and the registrar is not called (registry #ifdef in nt_devapi_registry.c).
if(NT_DEVAPI_GROUP_DISCOVERY)
    target_sources(nt_devapi PRIVATE nt_devapi_discovery.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_DISCOVERY=1)
endif()

# Input group: one flag gates EVERYTHING. nt_devapi_input.c is added (so the schedule, advance clock,
# lifecycle hooks + command handlers compile) only here; NT_DEVAPI_GROUP_INPUT compile define is set only here (so
# the registrar wires the group); nt_input_interface is linked + NT_INPUT_INJECT_ENABLED=1 set only
# here (so the exe's nt_input carries the inject pipeline; PUBLIC so the value matches the consumer's
# nt_input). OFF -> none of this; the core/net never reference an input symbol (the per-tick + reset
# hooks are generic). The core dispatches the group purely through the registered lifecycle hooks.
if(NT_DEVAPI_GROUP_INPUT)
    target_sources(nt_devapi PRIVATE nt_devapi_input.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_INPUT=1)
    target_compile_definitions(nt_devapi PUBLIC NT_INPUT_INJECT_ENABLED=1)
    target_link_libraries(nt_devapi PRIVATE nt_input_interface)
endif()

# engine.info pulls version/build/preset from nt_core (nt_engine_*_string()).

# nt_devapi_net -- native loopback TCP transport, the sole socket boundary. Kept SEPARATE from the
# core so a core-only consumer (tests, future non-transport tooling) pulls no socket code or ws2_32
# ("use only what you need"). Native-only: excluded from the Emscripten/web build (a web ccall
# transport would be a sibling target, not part of this one).
if(NOT EMSCRIPTEN)
    nt_add_module(nt_devapi_net LOG_DOMAIN "devapi_net"
        nt_devapi_net.c
        nt_devapi_net.h
    )
    # PUBLIC nt_devapi: the transport wraps the core dispatch and re-exposes NT_DEVAPI_ENABLED.
    target_link_libraries(nt_devapi_net PUBLIC nt_devapi)
    # Winsock: WSAStartup/socket/closesocket on Windows. PUBLIC so an executable linking
    # nt_devapi_net inherits it (a static lib's PRIVATE link does not propagate).
    if(WIN32)
        target_link_libraries(nt_devapi_net PUBLIC ws2_32)
    endif()
endif()
