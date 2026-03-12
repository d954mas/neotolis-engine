# nt_gfx -- graphics rendering with unified GL backend
#
# Unified GL backend (gl/nt_gfx_gl.c) handles both WebGL 2 (Emscripten)
# and OpenGL 3.3 Core (native desktop) using #ifdef NT_PLATFORM_WEB
# for the handful of platform differences.

nt_add_module(nt_gfx
    nt_gfx.c
    gl/nt_gfx_gl.c
    nt_gfx.h
    nt_gfx_internal.h
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
