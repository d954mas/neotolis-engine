# nt_gfx -- graphics rendering
#
# GL backend: WebGL 2 via Emscripten (GLES 3.0) or OpenGL 3.3 Core via glad (desktop).
# Basis Universal transcoder is swappable: nt_gfx links only its interface,
# the exe picks nt_basisu_transcoder or nt_basisu_transcoder_stub.

# Header-only interface carries nt_gfx.h's include root; the exe selects the impl.
nt_declare_interface(nt_gfx)

if(EMSCRIPTEN)
    nt_add_module(nt_gfx LOG_DOMAIN "gfx"
        nt_gfx.c
        gl/nt_gfx_gl.c
        gl/nt_gfx_gl_ctx_web.c
        nt_gfx.h
        nt_gfx_internal.h
        gl/nt_gfx_gl_ctx.h
    )
    target_link_libraries(nt_gfx PUBLIC nt_core nt_pool nt_window_interface nt_log_interface nt_hash nt_gfx_interface nt_basisu_transcoder_interface nt_meshwire_interface)
else()
    nt_add_module(nt_gfx LOG_DOMAIN "gfx"
        nt_gfx.c
        gl/nt_gfx_gl.c
        gl/nt_gfx_gl_ctx_native.c
        nt_gfx.h
        nt_gfx_internal.h
        gl/nt_gfx_gl_ctx.h
    )
    target_link_libraries(nt_gfx PUBLIC nt_core nt_pool nt_window_interface nt_log_interface nt_hash nt_gfx_interface nt_basisu_transcoder_interface nt_meshwire_interface)
    target_link_libraries(nt_gfx PRIVATE glad)
    find_package(OpenGL REQUIRED)
    target_link_libraries(nt_gfx PRIVATE OpenGL::GL)
endif()

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_gfx_stub LOG_DOMAIN "gfx_stub"
    nt_gfx.c
    stub/nt_gfx_stub.c
    nt_gfx.h
    nt_gfx_internal.h
)

target_link_libraries(nt_gfx_stub PUBLIC nt_core nt_pool nt_log_stub nt_hash nt_gfx_interface nt_basisu_transcoder_interface nt_meshwire_interface)
