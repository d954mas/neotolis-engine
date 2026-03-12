# nt_gfx -- graphics rendering with unified GL backend
#
# GL backend (gl/nt_gfx_gl.c) is platform-agnostic -- all GL calls are
# identical between WebGL 2 and OpenGL 3.3 Core.  Platform-specific context
# management lives in gl/nt_gfx_gl_ctx_{web,native}.c.

if(EMSCRIPTEN)
    set(GFX_GL_CTX gl/nt_gfx_gl_ctx_web.c)
else()
    set(GFX_GL_CTX gl/nt_gfx_gl_ctx_native.c)
endif()

nt_add_module(nt_gfx
    nt_gfx.c
    gl/nt_gfx_gl.c
    ${GFX_GL_CTX}
    nt_gfx.h
    nt_gfx_internal.h
    gl/nt_gfx_gl_ctx.h
)

target_link_libraries(nt_gfx PUBLIC nt_core nt_window)

if(WIN32)
    target_link_libraries(nt_gfx PRIVATE opengl32)
elseif(NOT EMSCRIPTEN)
    target_link_libraries(nt_gfx PRIVATE GL)
endif()

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_gfx_stub
    nt_gfx.c
    stub/nt_gfx_stub.c
    nt_gfx.h
    nt_gfx_internal.h
)

target_link_libraries(nt_gfx_stub PUBLIC nt_core)
