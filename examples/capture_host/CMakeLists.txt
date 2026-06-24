add_executable(capture_host main.c)

# capture_host owns a REAL nt_gfx context (devapi_host inits none, Pitfall 2) and calls the pre-swap
# capture seam (nt_devapi_capture_on_pre_swap, declared in devapi/nt_devapi_internal.h — reachable via
# nt_devapi's PUBLIC engine/ include root). The capture group itself is gated by NT_DEVAPI_GROUP_CAPTURE
# on the nt_devapi build, so configure with -DNT_DEVAPI_GROUP_CAPTURE=ON.
#
# GNU ld is single-pass: list every CONSUMER before its PROVIDER. nt_devapi (references nt_gfx readback,
# nt_fpng encode, the window symbols) precedes nt_gfx / nt_fpng; core/log trail last (Pitfall 3).
# capture_host is a CAPTURE-focused host: it wires only the capture seam + a real GL context, so it
# links only the providers the CORE/DISCOVERY/TIME/CAPTURE groups reference. Build it in a config with
# just those groups ON (the other groups' TUs would pull nt_ui/metrics/etc. that this host never inits).
# GNU ld single-pass: every consumer precedes its provider; nt_gfx's asset-activator IO chain trails it.
target_link_libraries(capture_host PRIVATE
    nt_core nt_app nt_input nt_window nt_log nt_devapi_net nt_devapi
    nt_gfx nt_fpng
    nt_resource nt_http nt_fs
    nt_platform)
if(WIN32)
    # ws2_32 is inherited transitively from nt_devapi_net (PUBLIC link).
    # getenv() for the NT_DEVAPI_PORT override (single-threaded host startup).
    target_compile_definitions(capture_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The shared nt_devapi lib compiles in whatever groups are enabled; with GROUP_UI / GROUP_OBS /
# GROUP_ENTITY_WRITE on, linking nt_devapi pulls those groups' TUs whose externals (nt_ui_*, metrics,
# entity comps) this CAPTURE-focused host deliberately does not provide. Keep the target CONFIGURED so
# main.c stays in compile_commands.json (the project's "every .c is compiled" tidy invariant), but drop
# it from the default `all` target in any non-capture-focused config so `cmake --build` does not try to
# link it (GNU ld single-pass would fail). The capture-smoke CI job builds it explicitly with
# `--target capture_host` (which ignores EXCLUDE_FROM_ALL); the all-groups native-debug-test exercises
# the capture group via the test_devapi_capture unit test instead.
if(NT_DEVAPI_GROUP_UI OR NT_DEVAPI_GROUP_OBS OR NT_DEVAPI_GROUP_ENTITY_WRITE)
    set_target_properties(capture_host PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(capture_host)
nt_set_sanitizer_flags(capture_host)

# Per-project output: build/examples/capture_host/[preset]/
set_target_properties(capture_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/capture_host/${NT_PRESET_NAME}"
)
