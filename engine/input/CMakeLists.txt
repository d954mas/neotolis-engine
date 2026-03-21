# nt_input -- input polling with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_input LOG_DOMAIN "input"
        nt_input.c
        web/nt_input_web.c
        nt_input.h
        nt_input_internal.h
    )
else()
    nt_add_module(nt_input LOG_DOMAIN "input"
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
    # Native: nt_input_native.c reads g_nt_window.dpr at flush time.
    # The header is found via the shared engine/ include root; the symbol
    # g_nt_window resolves at executable link time (app links nt_window).
    target_link_libraries(nt_input PUBLIC nt_core)
endif()

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_input_stub LOG_DOMAIN "input_stub"
    nt_input.c
    stub/nt_input_stub.c
    nt_input.h
)

target_link_libraries(nt_input_stub PUBLIC nt_core)
