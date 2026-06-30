add_executable(devapi_host main.c)
# The host wires a REAL nt_ui "hud" so ui.* resolve against a probe-able tree; nt_ui pulls its full
# renderer chain (resource/font/material/atlas/sprite+text/nt_gfx) which must resolve at link even
# though the host never calls nt_ui_walk. nt_gfx also supplies render.info's draw_calls (0 here, no draw).
# GNU ld is single-pass — list each CONSUMER before its PROVIDER: nt_devapi before nt_ui+nt_gfx, nt_ui
# before its renderer chain. nt_devapi is transitive via nt_devapi_net; pin it explicitly.
# Transport split (mirrors engine/devapi/CMakeLists.txt): web links nt_devapi_web (push/pull ccall
# bridge, NO socket) + nt_platform_web; native links nt_devapi_net (TCP). Both consume nt_devapi_default;
# web also pulls nt_gfx for the deferred-capture seam.
if(EMSCRIPTEN)
    set(DEVAPI_HOST_TRANSPORT nt_devapi_web nt_platform_web)
else()
    set(DEVAPI_HOST_TRANSPORT nt_devapi_net)
endif()
target_link_libraries(devapi_host PRIVATE
    nt_core nt_app nt_input nt_window nt_log ${DEVAPI_HOST_TRANSPORT} nt_devapi_default
    nt_ui
    nt_sprite_renderer nt_text_renderer nt_material nt_font nt_atlas nt_resource
    nt_http nt_fs
    # Obs providers must trail their consumers above for GNU ld single-pass.
    nt_metrics nt_log_ring
    # entity.set reaches these via describe()/apply(); only nt_entity is transitive, so list explicitly.
    nt_transform_comp nt_drawable_comp nt_comp_storage nt_entity nt_math
    nt_platform
    nt_gfx)
if(WIN32)
    # ws2_32 is inherited transitively from nt_devapi_net (PUBLIC link); nt_devapi (core) pulls it via nt_devapi_net.
    # getenv() for the NT_DEVAPI_PORT override (single-threaded host startup).
    target_compile_definitions(devapi_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(EMSCRIPTEN)
    # fpng auto-detects non-x86 (EMSCRIPTEN) and takes its scalar path, but pin FPNG_NO_SSE=1 explicitly
    # so the WASM build never reaches the SSE intrinsics.
    target_compile_definitions(nt_fpng PRIVATE FPNG_NO_SSE=1)

    # Emit index.html via the shared shell; export the two KEEPALIVE transport wrappers + main, and the
    # ccall/UTF8ToString runtime methods the window.__devapi bridge needs. WebGL2 for the GL
    # context the capture seam reads, mirroring tests/browser/app browser_smoke.
    set_target_properties(devapi_host PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(devapi_host TITLE "DevAPI Host - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(devapi_host PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_nt_devapi_web_submit','_nt_devapi_web_poll']"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']"
    )
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(devapi_host)
nt_set_sanitizer_flags(devapi_host)

# Per-project output: build/examples/devapi_host/[preset]/
set_target_properties(devapi_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/devapi_host/${NT_PRESET_NAME}"
)
