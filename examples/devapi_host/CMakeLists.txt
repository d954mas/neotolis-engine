add_executable(devapi_host main.c)
# nt_gfx_stub resolves render.info's nt_gfx_get_frame_draw_calls (the source of draw_calls). This
# host issues no real draw (placeholder frame), so draw_calls is genuinely 0 — the stub is correct
# here. A host with a real renderer would link nt_gfx instead.
# GNU ld is single-pass: nt_devapi (references nt_gfx_get_frame_draw_calls for render.info) must
# precede nt_gfx_stub (which defines it), else ld drops the stub object before seeing the reference.
# nt_devapi is already pulled transitively by nt_devapi_net; listing it explicitly here pins the order.
# nt_ui_stub resolves the devapi ui group's probe symbols (nt_ui_probe_collect / nt_ui_id /
# nt_ui_context_uses_raycast). This host registers no UI context yet (Plan 05 wires a real nt_ui
# ctx + swaps this to nt_ui); until then the stub keeps the link green with an empty tree. Same
# single-pass ordering rule: nt_devapi (consumer) precedes nt_ui_stub (provider).
target_link_libraries(devapi_host PRIVATE nt_core nt_app nt_input nt_window nt_log nt_devapi_net nt_devapi nt_ui_stub nt_gfx_stub)
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
