add_executable(capture_host main.c)

# Owns a REAL nt_gfx context (devapi_host inits none) for the pre-swap capture seam. Links only the
# group libs it uses (capture + core + discovery), so it pulls nt_gfx + nt_fpng, never the ui/obs/...
# group libs. (nt_input here is the engine input MODULE for ESC handling, not the devapi input group.)
# GNU ld is single-pass: each group lib (consumer) precedes its providers; nt_gfx's IO chain trails last.
target_link_libraries(capture_host PRIVATE
    nt_core nt_app nt_input nt_window nt_log nt_devapi_net
    nt_devapi_capture nt_devapi_core nt_devapi_discovery
    nt_gfx nt_fpng
    nt_resource nt_http nt_fs
    nt_platform nt_basisu_transcoder_stub nt_meshwire_stub)
if(WIN32)
    # ws2_32 is inherited transitively from nt_devapi_net (PUBLIC link).
    # getenv() for the NT_DEVAPI_PORT override (single-threaded host startup).
    target_compile_definitions(capture_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

nt_set_warning_flags(capture_host)
nt_set_sanitizer_flags(capture_host)

set_target_properties(capture_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/capture_host/${NT_PRESET_NAME}"
)
