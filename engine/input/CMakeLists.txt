# nt_input -- input polling with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_input
        nt_input.c
        web/nt_input_web.c
        nt_input.h
        nt_input_internal.h
    )
else()
    nt_add_module(nt_input
        nt_input.c
        native/nt_input_native.c
        nt_input.h
        nt_input_internal.h
    )
endif()

if(EMSCRIPTEN)
    # Web: nt_input_web.c reads g_nt_window.dpr for coordinate mapping
    target_link_libraries(nt_input PUBLIC nt_core nt_window)
else()
    # Native: nt_input_native.c reads g_nt_window.dpr at flush time
    target_link_libraries(nt_input PUBLIC nt_core)
    target_link_libraries(nt_input PRIVATE nt_window)
endif()

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_input_stub
    nt_input.c
    stub/nt_input_stub.c
    nt_input.h
)

target_link_libraries(nt_input_stub PUBLIC nt_core)
