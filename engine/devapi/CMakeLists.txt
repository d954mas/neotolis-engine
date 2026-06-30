# Devapi is a framework lib (nt_devapi) + one library per command group (nt_devapi_<group>) + a bundle
# (nt_devapi_default). A host links nt_devapi plus only the groups it wants, so it pulls only those
# groups' engine deps (a capture-only host links nt_devapi_capture -> nt_gfx/nt_fpng, never nt_ui). A
# full host links nt_devapi_default (every compiled-in group + nt_devapi_register_default()). Each
# NT_DEVAPI_GROUP_<X> option gates building that group's library.

# nt_devapi -- the framework ONLY: dispatch + self-describing registry + lifecycle-hook registry + the
# host-facing registration API (nt_devapi_groups.h). It references NO group, so any set of group libs
# can link it without conflict (this is what removed the all-groups link collision).
nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi.c
    nt_devapi_registry.c
    nt_devapi.h
    nt_devapi_groups.h
    nt_devapi_internal.h
    nt_devapi_stub.h
)

# cjson PUBLIC: the public handler typedef takes cJSON* params. nt_app_interface is a base dep --
# the core deferred-response queue reads g_nt_app.frame/time (resolves at the final-exe link).
target_link_libraries(nt_devapi PUBLIC nt_core cjson PRIVATE nt_log_interface nt_app_interface)

# Master gate: this CMakeLists only runs when enabled, so define is always 1 here.
target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)

# --- Per-group libraries. Each PUBLIC-links nt_devapi, carries its own engine deps, and sets its
#     NT_DEVAPI_GROUP_<X> define PUBLIC so a consumer (and nt_devapi_default below) sees the gate. The
#     group's deps propagate to the linking exe (a static lib's PRIVATE deps become link-only usage
#     requirements), so the exe needs only to pick the swappable *_interface impls (nt_gfx/nt_ui/...).

# Core group: ping / engine.info / view. view reads g_nt_window -> nt_window_interface.
if(NT_DEVAPI_GROUP_CORE)
    nt_add_module(nt_devapi_core LOG_DOMAIN "devapi" nt_devapi_core.c)
    target_link_libraries(nt_devapi_core PUBLIC nt_devapi PRIVATE nt_window_interface)
    target_compile_definitions(nt_devapi_core PUBLIC NT_DEVAPI_GROUP_CORE=1)
endif()

# Time group (time.*/render.*/frame.*): render.info reads draw_calls -> nt_gfx_interface.
if(NT_DEVAPI_GROUP_TIME)
    nt_add_module(nt_devapi_time LOG_DOMAIN "devapi" nt_devapi_time.c nt_devapi_time_internal.h)
    target_link_libraries(nt_devapi_time PUBLIC nt_devapi PRIVATE nt_gfx_interface)
    target_compile_definitions(nt_devapi_time PUBLIC NT_DEVAPI_GROUP_TIME=1)
endif()

# Discovery group: endpoints / command.describe / features. Reads only the registry (no engine dep).
if(NT_DEVAPI_GROUP_DISCOVERY)
    nt_add_module(nt_devapi_discovery LOG_DOMAIN "devapi" nt_devapi_discovery.c)
    target_link_libraries(nt_devapi_discovery PUBLIC nt_devapi)
    target_compile_definitions(nt_devapi_discovery PUBLIC NT_DEVAPI_GROUP_DISCOVERY=1)
endif()

# The ui group delegates inject scheduling to the input group's single scheduler (one scheduler, one
# cap <= the immediate buffer), so nt_devapi_ui hard-links nt_devapi_input. Fail early if UI lacks INPUT.
if(NT_DEVAPI_GROUP_UI AND NOT NT_DEVAPI_GROUP_INPUT)
    message(FATAL_ERROR "NT_DEVAPI_GROUP_UI requires NT_DEVAPI_GROUP_INPUT (ui.click/drag/scroll reuse the input group's inject scheduler).")
endif()

# The ui READ surface (ui.tree/ui.element) reads through nt_ui_probe_collect, which only has a real
# impl under NT_UI_DEBUG_TOOLS (else a 0-node stub). Hard-fail so GROUP_UI can never compile to a
# vacuously-passing probe (a false green when DEBUG_TOOLS is OFF).
if(NT_DEVAPI_GROUP_UI AND NOT NT_UI_DEBUG_TOOLS)
    message(FATAL_ERROR "NT_DEVAPI_GROUP_UI requires NT_UI_DEBUG_TOOLS (the ui probe is DEBUG_TOOLS-gated).")
endif()

# Input group: the synthetic-input scheduler + input.* veneer. NT_INPUT_AUTOMATION_ENABLED is PUBLIC so
# every consumer (and the ui group, which reuses the scheduler) sees nt_input's inject API decls.
if(NT_DEVAPI_GROUP_INPUT)
    nt_add_module(nt_devapi_input LOG_DOMAIN "devapi" nt_devapi_input.c nt_devapi_input_internal.h)
    target_link_libraries(nt_devapi_input PUBLIC nt_devapi nt_input_interface)
    target_compile_definitions(nt_devapi_input PUBLIC NT_DEVAPI_GROUP_INPUT=1 NT_INPUT_AUTOMATION_ENABLED=1)
endif()

# UI group: read veneer over nt_ui_probe_collect + write veneer delegating to the input scheduler.
# PUBLIC nt_devapi_input (the reuse wrappers live there); nt_ui_interface header-only (exe picks the
# impl); nt_window_interface for the coordinate-space metadata. NT_DEVAPI_GROUP_UI is PUBLIC because the
# host-facing nt_devapi_ui_register_context is gated by it in the public nt_devapi.h.
if(NT_DEVAPI_GROUP_UI)
    nt_add_module(nt_devapi_ui LOG_DOMAIN "devapi" nt_devapi_ui.c)
    target_link_libraries(nt_devapi_ui PUBLIC nt_devapi nt_devapi_input nt_ui_interface PRIVATE nt_window_interface)
    target_compile_definitions(nt_devapi_ui PUBLIC NT_DEVAPI_GROUP_UI=1)
endif()

# The obs READ surface (log.tail / perf.stats) serializes the dev-only nt_log_ring + nt_metrics. With
# any dep OFF those compile to no-op stubs (a vacuously-passing "false green"). Hard-fail so GROUP_OBS
# can never link the stub bodies; enable all the deps with it.
if(NT_DEVAPI_GROUP_OBS AND (NOT NT_LOG_RING_ENABLED OR NOT NT_METRICS_ENABLED OR NOT NT_INTROSPECT_ENABLED))
    message(FATAL_ERROR "NT_DEVAPI_GROUP_OBS requires NT_LOG_RING_ENABLED, NT_METRICS_ENABLED and "
        "NT_INTROSPECT_ENABLED (the obs group reads the dev-only log ring + metrics collector and walks "
        "entities through nt_introspect; with any dep OFF it would link no-op stubs — empty log.tail / "
        "zero perf.stats / core-fields-only entity.list — a false green). Enable all, e.g. "
        "-DNT_LOG_RING_ENABLED=ON -DNT_METRICS_ENABLED=ON -DNT_INTROSPECT_ENABLED=ON (or "
        "NT_UI_DEBUG_TOOLS=ON, which defaults all three).")
endif()

# Obs group (log.*/perf.*/entity.*/resource.*): pure immediate reads. entity.* walks via nt_introspect
# and pulls no specific component — components self-register a describe(), so the host/test links the
# concrete ones. nt_introspect precedes nt_entity (consumer before provider, GNU-ld single pass).
if(NT_DEVAPI_GROUP_OBS)
    nt_add_module(nt_devapi_obs LOG_DOMAIN "devapi" nt_devapi_obs.c)
    target_link_libraries(nt_devapi_obs PUBLIC nt_devapi PRIVATE
        nt_log_ring nt_metrics
        nt_introspect nt_entity nt_resource nt_hash
        nt_material # j_ref resolves an NT_REF_MATERIAL handle to its create-time label
    )
    target_compile_definitions(nt_devapi_obs PUBLIC NT_DEVAPI_GROUP_OBS=1)
endif()

# Entity-write group (entity.set): a dev-only DEBUG write over the component apply() hooks. Hard-requires
# NT_INTROSPECT_WRITE_ENABLED (else every component is read-only). Links nt_entity (resolution + the
# storage registry holding the apply hooks); the host/test links the writable components.
if(NT_DEVAPI_GROUP_ENTITY_WRITE AND NOT NT_INTROSPECT_WRITE_ENABLED)
    message(FATAL_ERROR "NT_DEVAPI_GROUP_ENTITY_WRITE requires NT_INTROSPECT_WRITE_ENABLED (the component "
        "apply() hooks). With it OFF every component is read-only, so entity.set could only return "
        "bad_params. Enable it, e.g. -DNT_INTROSPECT_WRITE_ENABLED=ON (or NT_UI_DEBUG_TOOLS=ON).")
endif()

if(NT_DEVAPI_GROUP_ENTITY_WRITE)
    nt_add_module(nt_devapi_entity_write LOG_DOMAIN "devapi" nt_devapi_entity_write.c)
    target_link_libraries(nt_devapi_entity_write PUBLIC nt_devapi PRIVATE nt_entity)
    target_compile_definitions(nt_devapi_entity_write PUBLIC NT_DEVAPI_GROUP_ENTITY_WRITE=1)
endif()

# Capture group (capture.frame / capture.region): the first DEFERRED data-returning commands. The seam
# producer chains nt_gfx_read_pixels (readback) -> RGB strip + box-average -> nt_fpng (PNG encode) ->
# nt_base64. nt_devapi_capture.h is the host-facing pre-swap-seam header.
if(NT_DEVAPI_GROUP_CAPTURE)
    nt_add_module(nt_devapi_capture LOG_DOMAIN "devapi" nt_devapi_capture.c nt_devapi_capture.h nt_devapi_capture_internal.h)
    target_link_libraries(nt_devapi_capture PUBLIC nt_devapi PRIVATE nt_gfx_interface nt_fpng nt_base64 nt_window_interface)
    target_compile_definitions(nt_devapi_capture PUBLIC NT_DEVAPI_GROUP_CAPTURE=1)
endif()

# nt_devapi_default -- the "register every compiled-in group" bundle. Links every enabled group lib (so
# a host linking it gets all groups + their deps) and provides nt_devapi_register_default(). The group
# libs' PUBLIC NT_DEVAPI_GROUP_<X> defines reach nt_devapi_default.c here, gating its register calls.
nt_add_module(nt_devapi_default LOG_DOMAIN "devapi" nt_devapi_default.c)
target_link_libraries(nt_devapi_default PUBLIC nt_devapi)
foreach(_grp core time discovery input ui obs entity_write capture)
    if(TARGET nt_devapi_${_grp})
        target_link_libraries(nt_devapi_default PUBLIC nt_devapi_${_grp})
    endif()
endforeach()

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
