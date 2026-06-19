# The input group is an OPTIONAL devapi module ("set of modules / use only what you need"). The
# NT_DEVAPI_GROUP_INPUT switch (defined at the project root, before the engine subdirs) gates the
# WHOLE group: its TU is compiled, its commands registered, its lifecycle hooks wired, the nt_input
# inject pipeline linked + enabled — ALL only when ON. OFF -> the devapi core/net carry ZERO input
# symbols. Fail-fast is accepted: ON without an nt_input impl in the exe fails at the final link,
# which is the developer's responsibility (no graceful no-op fallback).

# nt_devapi -- transport-agnostic, self-describing command core (dev-only, gated by NT_DEVAPI_ENABLED).
nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi.c
    nt_devapi_core.c
    nt_devapi_discovery.c
    nt_devapi_registry.c
    nt_devapi_time.c
    nt_devapi.h
    nt_devapi_internal.h
    nt_devapi_stub.h
)

# cjson is PUBLIC: the nt_devapi_handler_fn typedef in the public header takes
# cJSON* params, so cJSON.h must be visible to every consumer of nt_devapi.h.
# nt_app_interface + nt_gfx_interface make the time group's app/gfx header deps explicit (same
# pattern as nt_log). render.info reads draw_calls from its source (nt_gfx), not the nt_debug_overlay overlay.
# The nt_app_* / nt_gfx_get_frame_draw_calls symbols resolve at the final-exe link (the consumer
# links nt_app[_stub] + nt_gfx[_stub]) — nt_devapi stays a dev-only static lib with no impl.
# Order is kept consistent for GNU ld single-pass. The input group adds nt_input_interface + its
# inject define ONLY when ON (below), so a group-OFF devapi has no input link or symbols.
target_link_libraries(nt_devapi PUBLIC nt_core cjson PRIVATE nt_window_interface nt_log_interface nt_app_interface nt_gfx_interface)

# Master gate: this CMakeLists only runs when enabled, so define is always 1 here.
target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)

# Per-group compile-time opt-in: handlers are wrapped in
# #ifdef NT_DEVAPI_REGISTER_core; later groups clone this pattern.
target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_core=1)
target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_time=1)

# Input group: one flag gates EVERYTHING. nt_devapi_input.c is added (so the schedule, advance clock,
# lifecycle hooks + command handlers compile) only here; NT_DEVAPI_REGISTER_input is set only here (so
# the registrar wires the group); nt_input_interface is linked + NT_INPUT_INJECT_ENABLED=1 set only
# here (so the exe's nt_input carries the inject pipeline; PUBLIC so the value matches the consumer's
# nt_input). OFF -> none of this; the core/net never reference an input symbol (the per-tick + reset
# hooks are generic). The core dispatches the group purely through the registered lifecycle hooks.
if(NT_DEVAPI_GROUP_INPUT)
    target_sources(nt_devapi PRIVATE nt_devapi_input.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_REGISTER_input=1)
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
