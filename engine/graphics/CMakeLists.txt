# nt_gfx -- graphics rendering with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_gfx
        nt_gfx.c
        webgl/nt_gfx_webgl.c
        nt_gfx.h
        nt_gfx_internal.h
    )
else()
    nt_add_module(nt_gfx
        nt_gfx.c
        native/nt_gfx_native.c
        nt_gfx.h
        nt_gfx_internal.h
    )
endif()

target_link_libraries(nt_gfx PUBLIC nt_core nt_window)

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_gfx_stub
    nt_gfx.c
    stub/nt_gfx_stub.c
    nt_gfx.h
    nt_gfx_internal.h
)

target_link_libraries(nt_gfx_stub PUBLIC nt_core)
