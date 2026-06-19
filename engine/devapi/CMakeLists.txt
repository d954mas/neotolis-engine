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

# Input group: sources + NT_DEVAPI_GROUP_INPUT + NT_INPUT_AUTOMATION_ENABLED (PUBLIC, so the value
# matches the consumer's nt_input) + nt_input dep.
if(NT_DEVAPI_GROUP_INPUT)
    target_sources(nt_devapi PRIVATE nt_devapi_input.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_INPUT=1)
    target_compile_definitions(nt_devapi PUBLIC NT_INPUT_AUTOMATION_ENABLED=1)
    target_link_libraries(nt_devapi PRIVATE nt_input_interface)
endif()

# UI group (ui.tree/ui.element/ui.contexts): the L2 read veneer over nt_ui_probe_collect. Links the
# HEADER-ONLY nt_ui_interface (not the heavy nt_ui impl chain — the exe picks nt_ui or nt_ui_stub,
# exactly like nt_input_interface) + nt_input_interface (Plan 05's ui.click reuses inject) + PUBLIC
# NT_INPUT_AUTOMATION_ENABLED so the value matches the consumer's nt_input + nt_window_interface for
# the D-10 metadata (g_nt_window, resolves at exe link like the core group).
if(NT_DEVAPI_GROUP_UI)
    target_sources(nt_devapi PRIVATE nt_devapi_ui.c)
    target_compile_definitions(nt_devapi PRIVATE NT_DEVAPI_GROUP_UI=1)
    target_compile_definitions(nt_devapi PUBLIC NT_INPUT_AUTOMATION_ENABLED=1)
    target_link_libraries(nt_devapi PRIVATE nt_input_interface nt_ui_interface nt_window_interface)
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
