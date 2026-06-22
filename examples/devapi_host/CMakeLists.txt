add_executable(devapi_host main.c)
# The host now wires a REAL nt_ui "hud" context so ui.tree / ui.element / ui.click resolve against a
# genuine probe-able tree (the stub returned an empty tree). Real nt_ui pulls its renderer chain
# (nt_resource/nt_font/nt_material/nt_atlas/nt_sprite_renderer/nt_text_renderer + real nt_gfx) — the
# host never calls nt_ui_walk, but those symbols must resolve at the final link. nt_gfx (real, not the
# stub) also supplies render.info's nt_gfx_get_frame_draw_calls; the host issues no draw, so draw_calls
# stays 0.
# GNU ld is single-pass: list every CONSUMER before its PROVIDER. nt_devapi (references the gfx +
# nt_ui probe symbols) precedes nt_ui + nt_gfx; nt_ui (references the renderer/resource chain) precedes
# those providers. nt_devapi is already pulled transitively by nt_devapi_net; listing it explicitly
# pins the order.
target_link_libraries(devapi_host PRIVATE
    nt_core nt_app nt_input nt_window nt_log nt_devapi_net nt_devapi
    nt_ui
    nt_sprite_renderer nt_text_renderer nt_material nt_font nt_atlas nt_resource
    nt_http nt_fs
    # Obs L1 providers: nt_metrics (the perf store the host pushes into) + nt_log_ring (log.tail).
    # Consumers (main.c + nt_devapi's obs group) are listed above; these trail them. nt_gfx is required
    # by render.info + the nt_ui renderer chain (the host renders nothing itself); nt_platform supplies
    # the memory probe.
    nt_metrics nt_log_ring
    nt_platform
    nt_gfx)
if(WIN32)
    # ws2_32 is inherited transitively from nt_devapi_net (PUBLIC link); nt_devapi (core) pulls it via nt_devapi_net.
    # getenv() for the NT_DEVAPI_PORT override (single-threaded host startup).
    target_compile_definitions(devapi_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(devapi_host)
nt_set_sanitizer_flags(devapi_host)

# Per-project output: build/examples/devapi_host/[preset]/
set_target_properties(devapi_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/devapi_host/${NT_PRESET_NAME}"
)
