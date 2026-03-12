# nt_gfx -- graphics rendering
#
# GL backend: WebGL 2 via Emscripten (GLES 3.0).  Desktop GL requires
# a loader (glad) which is not yet vendored -- native builds use stub only.

if(EMSCRIPTEN)
    nt_add_module(nt_gfx
        nt_gfx.c
        gl/nt_gfx_gl.c
        gl/nt_gfx_gl_ctx_web.c
        nt_gfx.h
        nt_gfx_internal.h
        gl/nt_gfx_gl_ctx.h
    )
    target_link_libraries(nt_gfx PUBLIC nt_core nt_window nt_log)
endif()

# Stub implementation for headless builds (CI, builder, tests, native)
nt_add_module(nt_gfx_stub
    nt_gfx.c
    stub/nt_gfx_stub.c
    nt_gfx.h
    nt_gfx_internal.h
)

target_link_libraries(nt_gfx_stub PUBLIC nt_core nt_log_stub)
