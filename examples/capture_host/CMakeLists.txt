add_executable(capture_host main.c)

# capture_host owns a REAL nt_gfx context (devapi_host inits none) and calls the pre-swap capture seam
# (nt_devapi_capture_install_seam, from the PUBLIC devapi/nt_devapi_capture.h). It links ONLY the devapi
# group libs it wires -- capture + core/discovery/time/input -- so it pulls nt_gfx + nt_fpng and NEVER
# nt_ui/metrics/entity. That is exactly why it links cleanly in ANY config (no all-groups collision and
# no EXCLUDE_FROM_ALL): a minimal host links a minimal set of group libs.
# GNU ld is single-pass: each group lib (consumer) precedes its providers; nt_gfx's asset-activator IO
# chain and core/log trail last.
target_link_libraries(capture_host PRIVATE
    nt_core nt_app nt_input nt_window nt_log nt_devapi_net
    nt_devapi_capture nt_devapi_core nt_devapi_discovery nt_devapi_time nt_devapi_input
    nt_gfx nt_fpng
    nt_resource nt_http nt_fs
    nt_platform)
if(WIN32)
    # ws2_32 is inherited transitively from nt_devapi_net (PUBLIC link).
    # getenv() for the NT_DEVAPI_PORT override (single-threaded host startup).
    target_compile_definitions(capture_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(capture_host)
nt_set_sanitizer_flags(capture_host)

# Per-project output: build/examples/capture_host/[preset]/
set_target_properties(capture_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/capture_host/${NT_PRESET_NAME}"
)
