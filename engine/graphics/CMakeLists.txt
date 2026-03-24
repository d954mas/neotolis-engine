# nt_gfx -- graphics rendering
#
# GL backend: WebGL 2 via Emscripten (GLES 3.0) or OpenGL 3.3 Core via glad (desktop).
# Basis Universal transcoder is optional — disable with -DNT_BASISU=OFF for smaller binaries.

option(NT_BASISU "Link Basis Universal transcoder for GPU texture compression" ON)

if(EMSCRIPTEN)
    nt_add_module(nt_gfx LOG_DOMAIN "gfx"
        nt_gfx.c
        gl/nt_gfx_gl.c
        gl/nt_gfx_gl_ctx_web.c
        nt_gfx.h
        nt_gfx_internal.h
        gl/nt_gfx_gl_ctx.h
    )
    target_link_libraries(nt_gfx PUBLIC nt_core nt_pool nt_window nt_log nt_hash)
else()
    nt_add_module(nt_gfx LOG_DOMAIN "gfx"
        nt_gfx.c
        gl/nt_gfx_gl.c
        gl/nt_gfx_gl_ctx_native.c
        nt_gfx.h
        nt_gfx_internal.h
        gl/nt_gfx_gl_ctx.h
    )
    target_link_libraries(nt_gfx PUBLIC nt_core nt_pool nt_window nt_log nt_hash)
    target_link_libraries(nt_gfx PRIVATE glad)
    find_package(OpenGL REQUIRED)
    target_link_libraries(nt_gfx PRIVATE OpenGL::GL)
endif()

if(NT_BASISU)
    target_link_libraries(nt_gfx PUBLIC nt_basisu_transcoder)
    target_compile_definitions(nt_gfx PRIVATE NT_HAS_BASISU=1)
endif()

# Stub implementation for headless builds (CI, builder, tests)
# No transcoder in stub — compressed textures return dummy handle
nt_add_module(nt_gfx_stub LOG_DOMAIN "gfx_stub"
    nt_gfx.c
    stub/nt_gfx_stub.c
    nt_gfx.h
    nt_gfx_internal.h
)

target_link_libraries(nt_gfx_stub PUBLIC nt_core nt_pool nt_log_stub nt_hash)
