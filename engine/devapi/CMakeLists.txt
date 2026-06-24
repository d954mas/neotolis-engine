# Each NT_DEVAPI_GROUP_<X> switch gates a whole group (sources + defines + deps).
# OFF -> the core/net carry zero symbols of that group. ON without its engine impl fails at link.

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

# cjson PUBLIC: the public handler typedef takes cJSON* params. nt_app_interface is a base dep --
# the core deferred-response queue reads g_nt_app.frame/time (resolves at the final-exe link).
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

# Time group (time.*/render.*/frame.*): nt_gfx_interface is its only added dep (render.info reads
# draw_calls); nt_app_interface is already a base dep.
if(NT_DEVAPI_GROUP_TIME)
    target_sources(nt_devapi PRIVATE nt_devapi_time.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_TIME=1)
    target_link_libraries(nt_devapi PRIVATE nt_gfx_interface)
endif()

# Discovery group: endpoints / command.describe / features. Reads only the registry (no engine dep).
if(NT_DEVAPI_GROUP_DISCOVERY)
    target_sources(nt_devapi PRIVATE nt_devapi_discovery.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_DISCOVERY=1)
endif()

# The ui group delegates inject scheduling to the input group's single scheduler (one scheduler, one
# cap <= the immediate buffer — so two sibling groups can never over-subscribe the 256-slot buffer).
# That hard dependency is explicit: ui without input would link-fail on the reuse wrappers.
if(NT_DEVAPI_GROUP_UI AND NOT NT_DEVAPI_GROUP_INPUT)
    message(FATAL_ERROR "NT_DEVAPI_GROUP_UI requires NT_DEVAPI_GROUP_INPUT (ui.click/drag/scroll reuse the input group's inject scheduler).")
endif()

# The ui READ surface (ui.tree/ui.element) reads through nt_ui_probe_collect, which only has a real
# impl under NT_UI_DEBUG_TOOLS (else a 0-node stub). Hard-fail so GROUP_UI can never compile to a
# vacuously-passing probe (a false green when DEBUG_TOOLS is OFF).
if(NT_DEVAPI_GROUP_UI AND NOT NT_UI_DEBUG_TOOLS)
    message(FATAL_ERROR "NT_DEVAPI_GROUP_UI requires NT_UI_DEBUG_TOOLS (the ui probe is DEBUG_TOOLS-gated).")
endif()

# NT_INPUT_AUTOMATION_ENABLED is driven by (input OR ui): both groups need nt_input's inject symbols
# (input directly, ui via the input scheduler's reuse wrappers), so the value must be set once when
# EITHER is on. PUBLIC so it matches the consumer's nt_input.
if(NT_DEVAPI_GROUP_INPUT OR NT_DEVAPI_GROUP_UI)
    target_compile_definitions(nt_devapi PUBLIC NT_INPUT_AUTOMATION_ENABLED=1)
    target_link_libraries(nt_devapi PRIVATE nt_input_interface)
endif()

# Input group: sources + NT_DEVAPI_GROUP_INPUT. (NT_INPUT_AUTOMATION_ENABLED + nt_input dep are set
# by the OR block above, shared with the ui group.)
if(NT_DEVAPI_GROUP_INPUT)
    target_sources(nt_devapi PRIVATE nt_devapi_input.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_INPUT=1)
endif()

# UI group: read veneer over nt_ui_probe_collect + write veneer that delegates inject scheduling to the
# input group's scheduler. Links HEADER-ONLY nt_ui_interface (the exe picks nt_ui or nt_ui_stub, like
# nt_input_interface) + nt_window_interface for the coordinate-space metadata (g_nt_window).
if(NT_DEVAPI_GROUP_UI)
    target_sources(nt_devapi PRIVATE nt_devapi_ui.c)
    # PUBLIC: nt_devapi_ui_register_context (host-facing) is gated by NT_DEVAPI_GROUP_UI in the public
    # nt_devapi.h, so a consumer (the host) must see the same gate to call it. (The input group has no
    # host-facing registrar, hence its gate stays PRIVATE.)
    target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_GROUP_UI=1)
    target_link_libraries(nt_devapi PRIVATE nt_ui_interface nt_window_interface)
endif()

# The obs READ surface (log.tail / perf.stats) serializes the dev-only nt_log_ring + nt_metrics. With
# either dep OFF those compile to no-op stubs (empty log.tail, zero perf.stats) — a vacuously-passing
# "false green". Hard-fail so GROUP_OBS can never link the stub bodies; enable both deps with it.
if(NT_DEVAPI_GROUP_OBS AND (NOT NT_LOG_RING_ENABLED OR NOT NT_METRICS_ENABLED OR NOT NT_INTROSPECT_ENABLED))
    message(FATAL_ERROR "NT_DEVAPI_GROUP_OBS requires NT_LOG_RING_ENABLED, NT_METRICS_ENABLED and "
        "NT_INTROSPECT_ENABLED (the obs group reads the dev-only log ring + metrics collector and walks "
        "entities through nt_introspect; with any dep OFF it would link no-op stubs — empty log.tail / "
        "zero perf.stats / core-fields-only entity.list — a false green). Enable all, e.g. "
        "-DNT_LOG_RING_ENABLED=ON -DNT_METRICS_ENABLED=ON -DNT_INTROSPECT_ENABLED=ON (or "
        "NT_UI_DEBUG_TOOLS=ON, which defaults all three).")
endif()

# Obs group (log.*/perf.*/entity.*/resource.*): pure immediate reads. entity.* walks via nt_introspect
# and pulls no specific component — components self-register a describe(), so a host/test links the
# concrete ones. GNU-ld single-pass: nt_devapi (consumer) precedes these provider libs on the exe link
# line, nt_core/nt_log_* last — see the per-test link lines in tests/CMakeLists.txt.
if(NT_DEVAPI_GROUP_OBS)
    target_sources(nt_devapi PRIVATE nt_devapi_obs.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_OBS=1)
    target_link_libraries(nt_devapi PRIVATE
        nt_log_ring nt_metrics
        nt_entity nt_resource nt_introspect nt_hash
        nt_material # j_ref resolves an NT_REF_MATERIAL handle to its create-time label
    )
endif()

# Entity-write group (entity.set): a dev-only DEBUG write over the component apply() hooks. Hard-requires
# NT_INTROSPECT_WRITE_ENABLED (else every component is read-only — see the guard below). Links nt_entity
# (resolution + the storage registry holding the apply hooks); the host/test links the writable components.
if(NT_DEVAPI_GROUP_ENTITY_WRITE AND NOT NT_INTROSPECT_WRITE_ENABLED)
    message(FATAL_ERROR "NT_DEVAPI_GROUP_ENTITY_WRITE requires NT_INTROSPECT_WRITE_ENABLED (the component "
        "apply() hooks). With it OFF every component is read-only, so entity.set could only return "
        "bad_params. Enable it, e.g. -DNT_INTROSPECT_WRITE_ENABLED=ON (or NT_UI_DEBUG_TOOLS=ON).")
endif()

if(NT_DEVAPI_GROUP_ENTITY_WRITE)
    target_sources(nt_devapi PRIVATE nt_devapi_entity_write.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_ENTITY_WRITE=1)
    target_link_libraries(nt_devapi PRIVATE nt_entity)
endif()

# engine.info pulls version/build/preset from nt_core (nt_engine_*_string()).

# nt_devapi_net -- native loopback TCP transport. Separate target so a core-only consumer pulls no
# socket code/ws2_32. Native-only: excluded from the web build.
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
